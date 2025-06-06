#include "card_reader.h"
#include "adc_channels.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Initialize static members
std::map<uint8_t, CardReader*> CardReader::pinToReader;
SemaphoreHandle_t CardReader::mutex = NULL;

CardReader::CardReader(ADS7828& adc, uint8_t data0Pin, uint8_t data1Pin,
                      uint8_t fuseFeedbackChannel, uint8_t currentChannel,
                      bool ignoreParityErrors)
    : adc(adc), data0Pin(data0Pin), data1Pin(data1Pin),
      fuseFeedbackChannel(fuseFeedbackChannel), currentChannel(currentChannel),
      bitw(0), timeout(0), bitcnt(0), firstBitTime(0), waitingForRise(false),
      currentBitPin(0), ignoreParityErrors(ignoreParityErrors),
      currentBufferIndex(0), currentBufferCount(0) {
    
    // Create mutex if it doesn't exist
    if (mutex == NULL) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Error creating card reader mutex");
        }
    }
    
    // Initialize bit timings
    resetTiming();
    
    // Initialize current buffer with zeros
    for (int i = 0; i < CURRENT_BUFFER_SIZE; i++) {
        currentReadings[i] = 0.0;
    }
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
        // This is a falling edge
        if (bitcnt == 0) {
            // First bit of a new card - reset timing state
            firstBitTime = currentTime;
            waitingForRise = false;
            currentBitPin = 0;
            resetTiming();
            bitTimings[0].fallTime = 0;  // Relative to itself
        } else {
            // Record falling edge time relative to first bit
            bitTimings[bitcnt].fallTime = currentTime - firstBitTime;
        }
        
        // Set the bit value based on which pin triggered
        if (pin == data0Pin) {
            bitw = (bitw << 1) | 0x0;
        } else {
            bitw = (bitw << 1) | 0x1;
        }
        
        currentBitPin = pin;
        waitingForRise = true;
        timeout = millis();
    } else if (waitingForRise && pin == currentBitPin) {
        // This is a rising edge for the current bit
        bitTimings[bitcnt].riseTime = currentTime - firstBitTime;
        bitTimings[bitcnt].valid = true;
        bitcnt++;
        waitingForRise = false;
    }
}

bool CardReader::isCardPresent() const {
    return (((millis() - timeout) > 500) && bitcnt > 30);
}

long CardReader::getCardId() {
    if (!takeMutex()) {
        return -1;
    }
    
    // Store current state for validation
    int bitcnttmp = bitcnt;
    unsigned long long bitwtmp = bitw;
    BitTiming bitTimingsCopy[MAX_BITS];
    memcpy(bitTimingsCopy, bitTimings, sizeof(bitTimings));
    
    // Extract card info
    unsigned int site = (bitwtmp >> 25) & 0x00007f;
    unsigned long int card = (bitwtmp >> 1) & 0xffffff;
    bool op = (bitwtmp >> 0) & 0x000001;
    bool ep = (bitwtmp >> 32) & 0x000001;
    
    // Calculate expected parity
    int calc_ep = calculateParity(site);
    int calc_op = calculateParity(card);
    
    // Validate parity and timing
    bool timingIssue = false;
    bool widthIssue = false;
    bool spacingIssue = false;
    bool parityIssue = !ignoreParityErrors && (calc_ep != ep || calc_op != op);
    
    for (int i = 0; i < bitcnttmp && i < MAX_BITS; i++) {
        if (bitTimingsCopy[i].valid) {
            unsigned long bitWidth = bitTimingsCopy[i].riseTime - bitTimingsCopy[i].fallTime;
            unsigned long spacing = 0;
            if (i > 0 && bitTimingsCopy[i-1].valid) {
                spacing = bitTimingsCopy[i].fallTime - bitTimingsCopy[i-1].riseTime;
            }
            
            // Check bit width
            if (bitWidth < MIN_BIT_WIDTH || bitWidth > MAX_BIT_WIDTH) {
                widthIssue = true;
            }
            
            // Check inter-bit spacing (skip for first bit)
            if (i > 0 && (spacing < MIN_BIT_SPACING || spacing > MAX_BIT_SPACING)) {
                spacingIssue = true;
            }
        }
    }
    
    timingIssue = widthIssue || spacingIssue;
    
    // Create and store the burst data
    lastBurst.data = bitwtmp;
    lastBurst.bitCount = bitcnttmp;
    memcpy(lastBurst.timings, bitTimingsCopy, sizeof(lastBurst.timings));
    
    // Validate the burst
    if (lastBurst.bitCount >= 26) {  // Minimum bits for a valid Wiegand 26
        // Check parity (respecting ignoreParityErrors setting)
        lastBurst.valid = (!parityIssue && !timingIssue);
    } else {
        lastBurst.valid = false;
    }
    
    // If there are any issues, print debug info and return error
    if (parityIssue || timingIssue) {
        // Temporarily restore the state for debug printing
        bitw = bitwtmp;
        bitcnt = bitcnttmp;
        memcpy(bitTimings, bitTimingsCopy, sizeof(bitTimings));
        printDebug();
        card = -1; 
    }
    
    // Always reset state, even if we're going to return an error
    bitcnt = 0;
    bitw = 0;
    timeout = 0;  // Clear the timeout flag
    
    giveMutex();
    return card;
}

float CardReader::getCurrent() const {
    // Return the rolling average (buffer updated via update() method)
    return calculateAverageCurrent();
}

void CardReader::update() {
    // Update the current readings buffer
    updateCurrentBuffer();
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
    Serial.println("\nCard Reader Debug Info:");
    Serial.println("----------------------");
    
    // Print Wiegand protocol state
    Serial.print("Wiegand State: bitw=0x");
    Serial.print(bitw, HEX);
    Serial.print(", bitcnt=");
    Serial.print(bitcnt);
    Serial.print(", timeout=");
    Serial.println(timeout);
    
    // Print timing state
    Serial.print("Timing State: waitingForRise=");
    Serial.print(waitingForRise ? "YES" : "NO");
    if (waitingForRise) {
        Serial.print(", currentBitPin=");
        Serial.println(currentBitPin);
    } else {
        Serial.println();
    }
    
    // Print card presence
    Serial.print("Card Present: ");
    Serial.println(isCardPresent() ? "YES" : "NO");
    
    // If we have bits, show detailed timing analysis
    if (bitcnt > 0) {
        Serial.println("\nBit Timing Analysis:");
        for (int i = 0; i < bitcnt && i < MAX_BITS; i++) {
            if (bitTimings[i].valid) {
                unsigned long bitWidth = bitTimings[i].riseTime - bitTimings[i].fallTime;
                unsigned long spacing = 0;
                if (i > 0 && bitTimings[i-1].valid) {
                    spacing = bitTimings[i].fallTime - bitTimings[i-1].riseTime;
                }
                
                Serial.print("Bit ");
                Serial.print(i);
                Serial.print(": Start=");
                Serial.print(bitTimings[i].fallTime);
                Serial.print("us,\t Rise=");
                Serial.print(bitTimings[i].riseTime);
                Serial.print("us,\t Width=");
                Serial.print(bitWidth);
                if (i > 0) {
                    Serial.print("us,\t Spacing=");
                    Serial.print(spacing);
                    Serial.print("us");
                }
                Serial.print("us,\t Value=");
                Serial.println((unsigned int)(bitw >> (bitcnt - 1 - i) & 0x00000001));
                
                // Check and report timing issues
                if (bitWidth < MIN_BIT_WIDTH || bitWidth > MAX_BIT_WIDTH) {
                    Serial.print("  WARNING: Bit width ");
                    Serial.print(bitWidth);
                    Serial.print("us outside expected range ");
                    Serial.print(MIN_BIT_WIDTH);
                    Serial.print("-");
                    Serial.print(MAX_BIT_WIDTH);
                    Serial.println("us");
                }
                
                if (i > 0 && (spacing < MIN_BIT_SPACING || spacing > MAX_BIT_SPACING)) {
                    Serial.print("  WARNING: Bit spacing ");
                    Serial.print(spacing);
                    Serial.print("us outside expected range ");
                    Serial.print(MIN_BIT_SPACING);
                    Serial.print("-");
                    Serial.print(MAX_BIT_SPACING);
                    Serial.println("us");
                }
            }
        }
        
        // If we have enough bits, show parity calculation
        if (bitcnt >= 26) {
            unsigned int site = (bitw >> 25) & 0x00007f;
            unsigned long int card = (bitw >> 1) & 0xffffff;
            bool op = (bitw >> 0) & 0x000001;
            bool ep = (bitw >> 32) & 0x000001;
            
            Serial.println("\nParity Calculation:");
            Serial.print("Site Code: ");
            Serial.print(site);
            Serial.print(" (0x");
            Serial.print(site, HEX);
            Serial.print(") (");
            // Print site code bits
            for(int i = 6; i >= 0; i--) {
                Serial.print((site >> i) & 1);
            }
            Serial.println(")");
            
            Serial.print("Card Number: ");
            Serial.print(card);
            Serial.print(" (0x");
            Serial.print(card, HEX);
            Serial.print(") (");
            // Print card number bits
            for(int i = 23; i >= 0; i--) {
                Serial.print((card >> i) & 1);
            }
            Serial.println(")");
            
            // Calculate and show parity
            int calc_ep = calculateParity(site);
            int calc_op = calculateParity(card);
            
            Serial.print("Even Parity (EP): Calculated=");
            Serial.print(calc_ep);
            Serial.print(", Received=");
            Serial.println(ep);
            
            Serial.print("Odd Parity (OP): Calculated=");
            Serial.print(calc_op);
            Serial.print(", Received=");
            Serial.println(op);
            
            // Show if parity is valid
            if (calc_ep != ep || calc_op != op) {
                Serial.println("ERROR: Parity check failed!");
            } else {
                Serial.println("Parity check passed");
            }
        }
    }
    
    Serial.println("----------------------\n");
}

CardReader::WiegandBurst CardReader::getLastBurst(const CardReader& reader) {
    return reader.lastBurst;  // Return the stored burst
}

void CardReader::WiegandBurst::toJson(JsonObject& obj) const {
    obj["valid"] = valid;
    obj["bitCount"] = bitCount;
    obj["data"] = String(data, HEX);  // Convert to hex string
    
    // Extract and add card info
    unsigned int site = (data >> 25) & 0x00007f;
    unsigned long int card = (data >> 1) & 0xffffff;
    bool op = (data >> 0) & 0x000001;
    bool ep = (data >> 32) & 0x000001;
    
    JsonObject cardInfo = obj.createNestedObject("cardInfo");
    cardInfo["siteCode"] = site;
    cardInfo["cardNumber"] = card;
    cardInfo["oddParity"] = op;
    cardInfo["evenParity"] = ep;
    
    // Add timing data
    JsonArray timingArray = obj.createNestedArray("timings");
    for (int i = 0; i < bitCount && i < MAX_BITS; i++) {
        if (timings[i].valid) {
            JsonObject bitTiming = timingArray.createNestedObject();
            bitTiming["bitIndex"] = i;
            bitTiming["fallTime"] = timings[i].fallTime;
            bitTiming["riseTime"] = timings[i].riseTime;
            bitTiming["width"] = timings[i].riseTime - timings[i].fallTime;
            
            // Calculate spacing (except for first bit)
            if (i > 0 && timings[i-1].valid) {
                bitTiming["spacing"] = timings[i].fallTime - timings[i-1].riseTime;
            }
            
            // Add bit value
            bitTiming["value"] = (unsigned int)(data >> (bitCount - 1 - i) & 0x00000001);
            
            // Add timing validation
            unsigned long bitWidth = timings[i].riseTime - timings[i].fallTime;
            bool widthValid = (bitWidth >= MIN_BIT_WIDTH && bitWidth <= MAX_BIT_WIDTH);
            bitTiming["widthValid"] = widthValid;
            
            if (i > 0 && timings[i-1].valid) {
                unsigned long spacing = timings[i].fallTime - timings[i-1].riseTime;
                bool spacingValid = (spacing >= MIN_BIT_SPACING && spacing <= MAX_BIT_SPACING);
                bitTiming["spacingValid"] = spacingValid;
            }
        }
    }
}

void CardReader::updateCurrentBuffer() {
    // Take immediate reading
    float currentReading = ((adc.read(currentChannel) * ADC_TO_V * VDIV_SCALE_F) - ZERO_VOLTAGE) * 0.1 * 1000;
    
    // Add to circular buffer
    currentReadings[currentBufferIndex] = currentReading;
    
    // Update indices
    currentBufferIndex = (currentBufferIndex + 1) % CURRENT_BUFFER_SIZE;
    if (currentBufferCount < CURRENT_BUFFER_SIZE) {
        currentBufferCount++;
    }
}

float CardReader::calculateAverageCurrent() const {
    if (currentBufferCount == 0) {
        // No readings yet, return immediate reading
        return ((adc.read(currentChannel) * ADC_TO_V * VDIV_SCALE_F) - ZERO_VOLTAGE) * 0.1 * 1000;
    }
    
    float sum = 0.0;
    for (int i = 0; i < currentBufferCount; i++) {
        sum += currentReadings[i];
    }
    
    return sum / currentBufferCount;
} 