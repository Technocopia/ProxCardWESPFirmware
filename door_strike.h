#pragma once

#include <Arduino.h>
#include "ADS7828.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

class DoorStrike {
public:
    // Polarity enum to determine if strike is active high or low
    enum class Polarity {
        ACTIVE_HIGH,  // HIGH = engaged, LOW = disengaged
        ACTIVE_LOW    // LOW = engaged, HIGH = disengaged
    };

    DoorStrike(ADS7828& adc, uint8_t strikeIndex, uint8_t solenoidPin, 
               uint8_t feedbackChannel, uint8_t currentChannel,
               Polarity polarity = Polarity::ACTIVE_HIGH);
    ~DoorStrike();

    bool begin();
    void setState(bool state);
    bool getState() const;
    bool isEngaged() const;
    bool isConnected() const;  // Now considers both voltage and actuation state
    float getCurrent() const;
    bool isFeedbackActive() const;
    float getVoltage() const;
    bool isActuating() const;  // New method to check actuation state
    
    // New function to engage strike with automatic disengagement
    void engageWithTimeout(uint32_t timeoutMs = 5000);

private:
    ADS7828& adc;
    const uint8_t strikeIndex;
    const uint8_t solenoidPin;
    const uint8_t feedbackChannel;
    const uint8_t currentChannel;
    const Polarity polarity;
    bool currentState;
    bool actuatingState;  // New flag to track actuation state
    float idleVoltage;    // Baseline voltage when solenoid is open
    TimerHandle_t disengageTimer;
    
    static void disengageTimerCallback(TimerHandle_t timer);
    void scheduleDisengage(uint32_t timeoutMs);
    
    // Helper to convert logical state to physical pin state
    bool getPhysicalState(bool logicalState) const;
}; 