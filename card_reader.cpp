#include "card_reader.h"
#include "adc_channels.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Initialize static members
std::map<uint8_t, CardReader*> CardReader::pinToReader;
SemaphoreHandle_t CardReader::mutex = NULL;

CardReader::CardReader(ADS7828& adc, uint8_t data0Pin, uint8_t data1Pin,
                      uint8_t fuseFeedbackChannel, uint8_t currentChannel)
    : adc(adc), data0Pin(data0Pin), data1Pin(data1Pin),
      fuseFeedbackChannel(fuseFeedbackChannel), currentChannel(currentChannel),
      bitw(0), timeout(0), bitcnt(0), firstBitTime(0), waitingForRise(false),
      currentBitPin(0) {
    
    // Create mutex if it doesn't exist
    if (mutex == NULL) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Error creating card reader mutex");
        }
    }
    
    // Initialize bit timings
    resetTiming();
}

CardReader::~CardReader() {
    // Remove this reader from the pin map
    pinToReader.erase(data0Pin);
    pinToReader.erase(data1Pin);
    
    // Detach interrupts if they were attached
    if (pinToReader.find(data0Pin) == pinToReader.end()) {
        detachInterrupt(digitalPinToInterrupt(data0Pin));
    }
    if (pinToReader.find(data1Pin) == pinToReader.end()) {
        detachInterrupt(digitalPinToInterrupt(data1Pin));
    }
    
    // Set pins back to inputs without pullups
    pinMode(data0Pin, INPUT);
    pinMode(data1Pin, INPUT);
}

void CardReader::resetTiming() {
    firstBitTime = 0;
    waitingForRise = false;
    currentBitPin = 0;
    for (int i = 0; i < MAX_BITS; i++) {
        bitTimings[i].fallTime = 0;
        bitTimings[i].riseTime = 0;
        bitTimings[i].valid = false;
    }
}

void CardReader::begin() {
    // Re-check connectivity before proceeding
    pinMode(data0Pin, INPUT);
    pinMode(data1Pin, INPUT);
    delay(10);
    
    bool data0Connected = gpio_get_level((gpio_num_t)data0Pin) == 1;
    bool data1Connected = gpio_get_level((gpio_num_t)data1Pin) == 1;
    
    if (!data0Connected || !data1Connected) {
        Serial.println("ERROR: Card reader not connected during begin()!");
        Serial.print("Data0 (pin ");
        Serial.print(data0Pin);
        Serial.print("): ");
        Serial.println(data0Connected ? "Connected" : "Not Connected");
        Serial.print("Data1 (pin ");
        Serial.print(data1Pin);
        Serial.print("): ");
        Serial.println(data1Connected ? "Connected" : "Not Connected");
        return;  // Don't proceed with initialization
    }
    
    // Register this reader's pins in the map
    pinToReader[data0Pin] = this;
    pinToReader[data1Pin] = this;
    
    // Configure pins for normal operation
    pinMode(data0Pin, INPUT_PULLUP);
    pinMode(data1Pin, INPUT_PULLUP);
    
    // Attach interrupts for both rising and falling edges
    attachInterruptArg(digitalPinToInterrupt(data0Pin), onData0ISR, this, CHANGE);
    attachInterruptArg(digitalPinToInterrupt(data1Pin), onData1ISR, this, CHANGE);
    
    Serial.print("Card reader initialized on pins ");
    Serial.print(data0Pin);
    Serial.print(", ");
    Serial.println(data1Pin);
}

void IRAM_ATTR CardReader::onData0ISR(void* arg) {
    CardReader* reader = static_cast<CardReader*>(arg);
    if (reader != nullptr) {
        reader->handleEdge(reader->data0Pin, gpio_get_level((gpio_num_t)reader->data0Pin) == 0);
    }
}

void IRAM_ATTR CardReader::onData1ISR(void* arg) {
    CardReader* reader = static_cast<CardReader*>(arg);
    if (reader != nullptr) {
        reader->handleEdge(reader->data1Pin, gpio_get_level((gpio_num_t)reader->data1Pin) == 0);
    }
}

void IRAM_ATTR CardReader::handleEdge(uint8_t pin, bool isFalling) {
    unsigned long currentTime = micros();
    
    if (isFalling) {
        // If this is the first bit of a new card, reset timing state
        if (bitcnt == 0) {
            firstBitTime = currentTime;
            waitingForRise = true;
            currentBitPin = pin;
            // Clear timing array for new data
            for (int i = 0; i < MAX_BITS; i++) {
                bitTimings[i].fallTime = 0;
                bitTimings[i].riseTime = 0;
                bitTimings[i].valid = false;
            }
        }
        
        // Record falling edge time relative to first bit
        if (bitcnt < MAX_BITS) {
            bitTimings[bitcnt].fallTime = currentTime - firstBitTime;
            bitTimings[bitcnt].valid = false;  // Not valid until we get rising edge
            currentBitPin = pin;  // Remember which pin triggered this bit
        }
    } else if (waitingForRise && pin == currentBitPin) {
        // Only process rising edge if it's from the same pin as the falling edge
        if (bitcnt < MAX_BITS) {
            bitTimings[bitcnt].riseTime = currentTime - firstBitTime;
            bitTimings[bitcnt].valid = true;
            waitingForRise = false;
        }
    }
}

void CardReader::updateBurstInfo(bool hasTimingError, bool hasWidthError, 
                               bool hasSpacingError, bool hasParityError) {
    lastBurstInfo.data = bitw;
    lastBurstInfo.bitCount = bitcnt;
    lastBurstInfo.hasTimingError = hasTimingError;
    lastBurstInfo.hasWidthError = hasWidthError;
    lastBurstInfo.hasSpacingError = hasSpacingError;
    lastBurstInfo.hasParityError = hasParityError;
    lastBurstInfo.firstBitTime = firstBitTime;
    lastBurstInfo.lastBitTime = bitcnt > 0 ? bitTimings[bitcnt-1].riseTime + firstBitTime : 0;
    
    // Copy timing data
    for (int i = 0; i < bitcnt && i < MAX_BITS; i++) {
        lastBurstInfo.timings[i] = bitTimings[i];
    }
}

long CardReader::getCardId() {
    if (!takeMutex()) {
        return -1;
    }
    
    // Reset state variables
    bitcnt = 0;
    bitw = 0;
    timeout = 0;
    
    // Wait for card read with timeout
    unsigned long startTime = millis();
    while (!timeout && (millis() - startTime < 1000)) {
        delay(1);
    }
    
    // If we got a timeout or no bits, return error
    if (!timeout || bitcnt == 0) {
        giveMutex();
        return -1;
    }
    
    // Check timing and parity
    bool timingIssue = false;
    bool widthIssue = false;
    bool spacingIssue = false;
    
    // Check timing of each bit
    for (int i = 0; i < bitcnt; i++) {
        if (!bitTimings[i].valid) {
            timingIssue = true;
            continue;
        }
        
        // Calculate bit width
        unsigned long width = bitTimings[i].riseTime - bitTimings[i].fallTime;
        if (width < MIN_BIT_WIDTH || width > MAX_BIT_WIDTH) {
            widthIssue = true;
        }
        
        // Check inter-bit spacing (except for first bit)
        if (i > 0) {
            unsigned long spacing = bitTimings[i].fallTime - bitTimings[i-1].riseTime;
            if (spacing < MIN_BIT_SPACING || spacing > MAX_BIT_SPACING) {
                spacingIssue = true;
            }
        }
    }
    
    // Extract card information
    unsigned long siteCode = (bitw >> 1) & 0x1FFF;  // 13 bits for site code
    unsigned long cardNumber = (bitw >> 14) & 0x3FFF;  // 14 bits for card number
    
    // Calculate expected parity
    int expectedParity = calculateParity(siteCode) ^ calculateParity(cardNumber);
    int receivedParity = (bitw >> 28) & 0x3;  // Last 2 bits are parity
    
    bool parityError = (receivedParity != expectedParity);
    
    // Update burst info with all error flags
    updateBurstInfo(timingIssue || widthIssue || spacingIssue, 
                   widthIssue, spacingIssue, parityError);
    
    // If we have any issues, print debug info and return error
    if (timingIssue || widthIssue || spacingIssue || parityError) {
        printDebug();
        giveMutex();
        return -1;
    }
    
    giveMutex();
    return cardNumber;
}

float CardReader::getCurrent() const {
    return (adc.read(currentChannel) * ADC_TO_V * VDIV_SCALE_F) / 0.1;
}

bool CardReader::isFuseGood() const {
    float fuse_v = adc.read(fuseFeedbackChannel) * ADC_TO_V * VDIV_SCALE_F;
    return fuse_v > 11.0;
}

int CardReader::calculateParity(unsigned long int x) {
    unsigned long int y;
    y = x ^ (x >> 1);
    y = y ^ (y >> 2);
    y = y ^ (y >> 4);
    y = y ^ (y >> 8);
    y = y ^ (y >> 16);
    return y & 1;
}

bool CardReader::takeMutex() {
    if (mutex == NULL) return false;
    return xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void CardReader::giveMutex() {
    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

void CardReader::printDebug() const {
    Serial.printf("Card Reader Debug (Pins: D0=%d, D1=%d):\n", data0Pin, data1Pin);
    Serial.printf("Connection Status: D0=%s, D1=%s\n", 
                 isData0Connected() ? "Connected" : "Disconnected",
                 isData1Connected() ? "Connected" : "Disconnected");
    
    if (lastBurstInfo.bitCount > 0) {
        Serial.printf("Last Burst Info:\n");
        Serial.printf("  Bits: %d\n", lastBurstInfo.bitCount);
        Serial.printf("  Data: 0x%llX\n", lastBurstInfo.data);
        Serial.printf("  First Bit Time: %lu us\n", lastBurstInfo.firstBitTime);
        Serial.printf("  Last Bit Time: %lu us\n", lastBurstInfo.lastBitTime);
        Serial.printf("  Errors: Timing=%d, Width=%d, Spacing=%d, Parity=%d\n",
                     lastBurstInfo.hasTimingError,
                     lastBurstInfo.hasWidthError,
                     lastBurstInfo.hasSpacingError,
                     lastBurstInfo.hasParityError);
        
        Serial.println("Bit Timing Analysis:");
        for (int i = 0; i < lastBurstInfo.bitCount; i++) {
            const BitTiming& timing = lastBurstInfo.timings[i];
            if (!timing.valid) {
                Serial.printf("  Bit %2d: Invalid (no rising edge)\n", i);
                continue;
            }
            
            unsigned long width = timing.riseTime - timing.fallTime;
            unsigned long spacing = (i > 0) ? 
                timing.fallTime - lastBurstInfo.timings[i-1].riseTime : 0;
            
            Serial.printf("  Bit %2d: Fall=%6lu, Rise=%6lu, Width=%4lu, Spacing=%4lu, Value=%d\n",
                         i, timing.fallTime, timing.riseTime, width, spacing,
                         (lastBurstInfo.data >> i) & 1);
        }
    } else {
        Serial.println("No card read in progress");
    }
    
    // Print current state
    Serial.printf("Current State: bitcnt=%d, timeout=%d\n", bitcnt, timeout);
    Serial.printf("Current: %.2f mA, Fuse: %s\n", 
                 getCurrent(), isFuseGood() ? "Good" : "Open");
}

bool CardReader::isCardPresent() const {
    // A card is considered present if:
    // 1. We have received more than 30 bits (typical Wiegand format)
    // 2. The timeout has occurred (indicating end of transmission)
    // 3. Less than 500ms has passed since the timeout
    return (bitcnt > 30) && timeout && ((millis() - timeout) < 500);
} 