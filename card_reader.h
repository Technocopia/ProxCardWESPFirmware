#pragma once

#include <Arduino.h>
#include <ADS7828.h>
#include <map>
#include <driver/gpio.h>  // For ESP32 GPIO register access

class CardReader {
public:
    CardReader(ADS7828& adc, uint8_t data0Pin, uint8_t data1Pin, 
               uint8_t fuseFeedbackChannel, uint8_t currentChannel);
    ~CardReader();
    
    void begin();
    bool isCardPresent() const;
    long getCardId();
    float getCurrent() const;
    bool isFuseGood() const;
    
    // Get the reader's pin numbers
    uint8_t getData0Pin() const { return data0Pin; }
    uint8_t getData1Pin() const { return data1Pin; }
    
    // Debug function to print internal state
    void printDebug() const;
    
private:
    // Wiegand protocol variables - now instance members
    unsigned long long bitw;
    unsigned int timeout;
    int bitcnt;
    
    // Timing tracking
    unsigned long lastBitTime;  // Time of last bit received
    unsigned long bitTimes[100]; // Array to store timing of each bit
    
    // Pin definitions
    const uint8_t data0Pin;
    const uint8_t data1Pin;
    
    // ADC channels
    const uint8_t fuseFeedbackChannel;
    const uint8_t currentChannel;
    
    // Reference to ADC
    ADS7828& adc;
    
    // Static map to store interrupt handlers for each reader
    static std::map<uint8_t, CardReader*> pinToReader;
    
    // Static interrupt handlers - now take a void* parameter
    static void IRAM_ATTR onData0ISR(void* arg);
    static void IRAM_ATTR onData1ISR(void* arg);
    
    // Non-static handlers that do the actual work
    void IRAM_ATTR onData0();
    void IRAM_ATTR onData1();
    
    // Helper functions
    static int calculateParity(unsigned long int x);
    
    // Mutex for thread safety
    static SemaphoreHandle_t mutex;
    
    // Mutex helper functions
    bool takeMutex();
    void giveMutex();
}; 