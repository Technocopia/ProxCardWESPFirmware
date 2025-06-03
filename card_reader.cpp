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
      bitw(0), timeout(0), bitcnt(0), lastBitTime(0) {
    
    // Create mutex if it doesn't exist
    if (mutex == NULL) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Error creating card reader mutex");
        }
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

void CardReader::begin() {
    // Register this reader's pins in the map
    pinToReader[data0Pin] = this;
    pinToReader[data1Pin] = this;
    
    // Re-initialize pins to ensure they're in the correct state
    pinMode(data0Pin, INPUT_PULLUP);
    pinMode(data1Pin, INPUT_PULLUP);
    
    // Attach interrupts using static ISR handlers
    attachInterruptArg(digitalPinToInterrupt(data0Pin), onData0ISR, this, FALLING);
    attachInterruptArg(digitalPinToInterrupt(data1Pin), onData1ISR, this, FALLING);
    
    Serial.print("Card reader initialized on pins ");
    Serial.print(data0Pin);
    Serial.print(", ");
    Serial.println(data1Pin);
}

void IRAM_ATTR CardReader::onData0ISR(void* arg) {
    CardReader* reader = static_cast<CardReader*>(arg);
    if (reader != nullptr) {
        reader->onData0();
    }
}

void IRAM_ATTR CardReader::onData1ISR(void* arg) {
    CardReader* reader = static_cast<CardReader*>(arg);
    if (reader != nullptr) {
        reader->onData1();
    }
}

void IRAM_ATTR CardReader::onData0() {
    unsigned long currentTime = micros();

    bitTimes[bitcnt] = currentTime - lastBitTime;
    lastBitTime = currentTime;
    
    // Cast pin number to gpio_num_t and check pin state
    if (gpio_get_level((gpio_num_t)data0Pin) == 0) {  // Check if pin is LOW
        bitw = (bitw << 1) | 0x0;
        bitcnt++;
        timeout = millis();
    }
}

void IRAM_ATTR CardReader::onData1() {
    unsigned long currentTime = micros();
    bitTimes[bitcnt] = currentTime - lastBitTime;

    lastBitTime = currentTime;
    
    // Cast pin number to gpio_num_t and check pin state
    if (gpio_get_level((gpio_num_t)data1Pin) == 0) {  // Check if pin is LOW
        bitw = (bitw << 1) | 0x1;
        bitcnt++;
        timeout = millis();
    }
}

bool CardReader::isCardPresent() const {
    return (((millis() - timeout) > 500) && bitcnt > 30);
}

long CardReader::getCardId() {
    if (!takeMutex()) {
        return -1;
    }
    
    unsigned long long bitwtmp = bitw;
    int bitcnttmp = bitcnt;
    unsigned long bitTimesCopy[32];
    memcpy(bitTimesCopy, bitTimes, sizeof(bitTimes));
    
    bitcnt = 0;
    bitw = 0;
    lastBitTime = 0;
    
    giveMutex();
    
    // Print debug info
    Serial.println("\nWiegand Read Analysis:");
    Serial.println("----------------------");
    Serial.print("Total bits: ");
    Serial.println(bitcnttmp);
    
    // Print bit timing
    Serial.println("Bit timing (microseconds):");
    for (int i = 0; i < bitcnttmp; i++) {
        Serial.print("Bit ");
        Serial.print(i);
        Serial.print(": ");
        Serial.print(bitTimesCopy[i]);
        Serial.print("us, Value: ");
        Serial.println((unsigned int)(bitwtmp >> (bitcnttmp - 1 - i) & 0x00000001));
    }
    
    // Extract card info
    unsigned int site = (bitwtmp >> 25) & 0x00007f;
    unsigned long int card = (bitwtmp >> 1) & 0xffffff;
    bool op = (bitwtmp >> 0) & 0x000001;
    bool ep = (bitwtmp >> 32) & 0x000001;
    
    // Calculate expected parity
    int calc_ep = calculateParity(site);
    int calc_op = calculateParity(card);
    
    // Validate parity
    if (calc_ep != ep || calc_op != op) {
        Serial.println("\nParity check failed!");
        Serial.print("Site: 0x");
        Serial.print(site, HEX);
        Serial.print(" (");
        // Print site code bits
        for(int i = 6; i >= 0; i--) {
            Serial.print((site >> i) & 1);
        }
        Serial.print(") EP: calc=");
        Serial.print(calc_ep);
        Serial.print(" recv=");
        Serial.println(ep);
        
        Serial.print("Card: 0x");
        Serial.print(card, HEX);
        Serial.print(" (");
        // Print card number bits
        for(int i = 23; i >= 0; i--) {
            Serial.print((card >> i) & 1);
        }
        Serial.print(") OP: calc=");
        Serial.print(calc_op);
        Serial.print(" recv=");
        Serial.println(op);
        
        // Check for timing issues
        bool timingIssue = false;
        for (int i = 1; i < bitcnttmp; i++) {
            if (bitTimesCopy[i] < 50 || bitTimesCopy[i] > 2000) {  // Typical Wiegand timing is 100-1000us
                timingIssue = true;
                Serial.print("Timing issue on bit ");
                Serial.print(i);
                Serial.print(": ");
                Serial.print(bitTimesCopy[i]);
                Serial.println("us");
            }
        }
        if (timingIssue) {
            Serial.println("Bit timing outside normal Wiegand range!");
        }
        
        return -1;
    }
    
    // Print debug info
    Serial.print("\nValid read - Site: ");
    Serial.print(site);  // Already prints in decimal
    Serial.print(" (0x");
    Serial.print(site, HEX);  // Also show hex for reference
    Serial.print("), Card: ");
    Serial.print(card);  // Print in decimal
    Serial.print(" (0x");
    Serial.print(card, HEX);  // Also show hex for reference
    Serial.println(")");
    
    return card;
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
    Serial.println("\nCard Reader Debug Info:");
    Serial.println("----------------------");
    
    // Print Wiegand protocol state
    Serial.print("Wiegand State: bitw=0x");
    Serial.print(bitw, HEX);
    Serial.print(", bitcnt=");
    Serial.print(bitcnt);
    Serial.print(", timeout=");
    Serial.println(timeout);
    
    // Print card presence
    Serial.print("Card Present: ");
    Serial.println(isCardPresent() ? "YES" : "NO");
    
    // If we have a complete card read, show parity calculation
    if (bitcnt >= 26) {
        unsigned int site = (bitw >> 25) & 0x00007f;
        unsigned long int card = (bitw >> 1) & 0xffffff;
        bool op = (bitw >> 0) & 0x000001;
        bool ep = (bitw >> 32) & 0x000001;
        
        Serial.println("\nParity Calculation:");
        Serial.print("Site Code: ");
        Serial.print(site);  // Print in decimal
        Serial.print(" (0x");
        Serial.print(site, HEX);  // Also show hex for reference
        Serial.print(") (");
        // Print site code bits
        for(int i = 6; i >= 0; i--) {
            Serial.print((site >> i) & 1);
        }
        Serial.println(")");
        
        Serial.print("Card Number: ");
        Serial.print(card);  // Print in decimal
        Serial.print(" (0x");
        Serial.print(card, HEX);  // Also show hex for reference
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
        Serial.print("Parity Valid: ");
        Serial.println((calc_ep == ep && calc_op == op) ? "YES" : "NO");
    }
    
    Serial.println("----------------------\n");
} 