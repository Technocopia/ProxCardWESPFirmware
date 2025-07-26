#pragma once

#include <Arduino.h>
#include <ADS7828.h>
#include <map>
#include <driver/gpio.h>  // For ESP32 GPIO register access
#include <ArduinoJson.h>  // For JSON serialization

// Structure to hold timing information for each bit
struct BitTiming {
    unsigned long fallTime;  // Time of falling edge relative to first bit
    unsigned long riseTime;  // Time of rising edge relative to first bit
    bool valid;             // Whether we captured both edges
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
    
    static constexpr float ZERO_VOLTAGE = 9.5;

    // Structure to hold a complete Wiegand burst
    struct WiegandBurst {
        BitTiming timings[MAX_BITS];  // Timing information for each bit
        unsigned long long data;       // The actual Wiegand data
        int bitCount;                 // Number of bits in the burst
        bool valid;                   // Whether the burst is valid (parity and timing)

        // Convert burst to JSON object
        void toJson(JsonObject& obj) const;
    };
    
    // Static function to get the last Wiegand burst
    static WiegandBurst getLastBurst(const CardReader& reader);
    
    // Constructor with optional ignoreParityErrors parameter
    CardReader(ADS7828& adc, uint8_t data0Pin, uint8_t data1Pin, 
               uint8_t fuseFeedbackChannel, uint8_t currentChannel,
               bool ignoreParityErrors = false);
    ~CardReader();
    
    void begin();
    bool isCardPresent() const;
    long getCardId();
    unsigned int getSiteCode();
    void decodeCard();
    float getCurrent() const;
    bool isFuseGood() const;
    
    // Update function to be called from main loop
    void update();
    
    // Get the reader's pin numbers
    uint8_t getData0Pin() const { return data0Pin; }
    uint8_t getData1Pin() const { return data1Pin; }
    
    // Debug function to print internal state
    void printDebug() const;
    
    // Methods to control parity error handling
    void setIgnoreParityErrors(bool ignore) { ignoreParityErrors = ignore; }
    bool getIgnoreParityErrors() const { return ignoreParityErrors; }
    
private:
    // Wiegand protocol variables - now instance members
    unsigned long long bitw;
    unsigned int timeout;
    int bitcnt;
    
    // Decoded card information
    unsigned long int decodedCardId;
    unsigned int decodedSiteCode;
    
    // Timing tracking
    unsigned long firstBitTime;     // Time of first falling edge
    BitTiming bitTimings[MAX_BITS]; // Array to store timing of each bit's edges
    bool waitingForRise;           // Whether we're waiting for a rising edge
    uint8_t currentBitPin;         // Which pin we're currently tracking
    
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
    void IRAM_ATTR handleEdge(uint8_t pin, bool isFalling);
    void resetTiming();
    
    // Mutex for thread safety
    static SemaphoreHandle_t mutex;
    
    // Mutex helper functions
    bool takeMutex();
    void giveMutex();
    
    // Store the last complete burst
    WiegandBurst lastBurst;
    
    // Parity error handling
    bool ignoreParityErrors;  // Whether to ignore parity errors
    
    // Rolling average for current measurement
    static constexpr int CURRENT_BUFFER_SIZE = 60;  // Store 60 readings (1 per second for 1 minute)
    float currentReadings[CURRENT_BUFFER_SIZE];
    int currentBufferIndex;
    int currentBufferCount;
    
    // Helper methods for rolling average
    void updateCurrentBuffer();
    float calculateAverageCurrent() const;
}; 