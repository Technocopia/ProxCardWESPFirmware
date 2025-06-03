#pragma once

#include <Arduino.h>
#include <ADS7828.h>
#include <map>
#include <driver/gpio.h>  // For ESP32 GPIO register access

// Structure to hold timing information for each bit
struct BitTiming {
    unsigned long fallTime;  // Time of falling edge relative to first bit
    unsigned long riseTime;  // Time of rising edge relative to first bit
    bool valid;             // Whether we captured both edges
};

// Structure to hold Wiegand burst information
struct WiegandBurstInfo {
    unsigned long long data;     // The actual data bits
    int bitCount;               // Number of bits received
    BitTiming timings[100];     // Timing information for each bit
    bool hasTimingError;        // Whether any timing errors were detected
    bool hasWidthError;         // Whether any bit width errors were detected
    bool hasSpacingError;       // Whether any inter-bit spacing errors were detected
    bool hasParityError;        // Whether parity check failed
    unsigned long firstBitTime; // Time of first falling edge
    unsigned long lastBitTime;  // Time of last rising edge
};

class CardReader {
public:
    // Maximum number of bits supported by the card reader
    static constexpr int MAX_BITS = 100;
    
    // Wiegand bit timing bounds (in microseconds)
    static constexpr unsigned long MIN_BIT_WIDTH = 20;    // Minimum valid bit width
    static constexpr unsigned long MAX_BIT_WIDTH = 2500;  // Maximum valid bit width
    static constexpr unsigned long TYPICAL_BIT_WIDTH = 100;  // Typical bit width
    
    // Inter-bit spacing timing bounds (in microseconds)
    static constexpr unsigned long MIN_BIT_SPACING = 100;   // Minimum time between bits
    static constexpr unsigned long MAX_BIT_SPACING = 5000;  // Maximum time between bits
    static constexpr unsigned long TYPICAL_BIT_SPACING = 200;  // Typical time between bits
    
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
    
    // Get Wiegand signal connection status
    bool isData0Connected() const { return gpio_get_level((gpio_num_t)data0Pin) == 1; }
    bool isData1Connected() const { return gpio_get_level((gpio_num_t)data1Pin) == 1; }
    bool isConnected() const { return isData0Connected() && isData1Connected(); }
    
    // Get information about the last Wiegand burst
    const WiegandBurstInfo& getLastBurstInfo() const { return lastBurstInfo; }
    
    // Debug function to print internal state
    void printDebug() const;
    
private:
    // Wiegand protocol variables - now instance members
    unsigned long long bitw;
    unsigned int timeout;
    int bitcnt;
    
    // Timing tracking
    unsigned long firstBitTime;     // Time of first falling edge
    BitTiming bitTimings[MAX_BITS]; // Array to store timing of each bit's edges
    bool waitingForRise;           // Whether we're waiting for a rising edge
    uint8_t currentBitPin;         // Which pin we're currently tracking
    
    // Last burst information
    WiegandBurstInfo lastBurstInfo;
    
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
    void IRAM_ATTR handleEdge(uint8_t pin, bool isFalling);
    
    // Helper functions
    static int calculateParity(unsigned long int x);
    void updateBurstInfo(bool hasTimingError, bool hasWidthError, 
                        bool hasSpacingError, bool hasParityError);
    void resetTiming();  // Add declaration for resetTiming
    
    // Mutex for thread safety
    static SemaphoreHandle_t mutex;
    
    // Mutex helper functions
    bool takeMutex();
    void giveMutex();
}; 