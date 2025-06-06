#include "door_strike.h"
#include "adc_channels.h"

DoorStrike::DoorStrike(ADS7828& adc, uint8_t strikeIndex, uint8_t solenoidPin, 
                       uint8_t feedbackChannel, uint8_t currentChannel,
                       Polarity polarity)
    : adc(adc), strikeIndex(strikeIndex), solenoidPin(solenoidPin),
      feedbackChannel(feedbackChannel), currentChannel(currentChannel),
      polarity(polarity), currentState(false), actuatingState(false), 
      idleVoltage(0.0), disengageTimer(NULL) {
    // Initialize GPIO immediately in constructor
    pinMode(solenoidPin, OUTPUT);
    // Set to inactive state based on polarity
    digitalWrite(solenoidPin, getPhysicalState(false));

}

DoorStrike::~DoorStrike() {
    if (disengageTimer != NULL) {
        xTimerDelete(disengageTimer, portMAX_DELAY);
    }
}

bool DoorStrike::begin() {
    pinMode(solenoidPin, OUTPUT);
    digitalWrite(solenoidPin, getPhysicalState(false));
    currentState = false;
    // Capture idle voltage when solenoid is off
    idleVoltage = adc.read(currentChannel) * ADC_TO_V * VDIV_SCALE_F;
    return true;
}

void DoorStrike::setState(bool state) {
    digitalWrite(solenoidPin, getPhysicalState(state));
    currentState = state;
}

bool DoorStrike::getState() const {
    return currentState;
}

bool DoorStrike::isEngaged() const {
    return currentState;
}

float DoorStrike::getCurrent() const {
    float currentVoltage = adc.read(currentChannel) * ADC_TO_V * VDIV_SCALE_F;
    float voltageDifference = currentVoltage - idleVoltage;
    return voltageDifference * 0.1 * 1000; //
}

bool DoorStrike::isFeedbackActive() const {
    return (adc.read(feedbackChannel)  * ADC_TO_V * VDIV_SCALE_F) > 1.0; // Assuming feedback is active when voltage > 1.65V
}

void DoorStrike::disengageTimerCallback(TimerHandle_t timer) {
    DoorStrike* strike = static_cast<DoorStrike*>(pvTimerGetTimerID(timer));
    if (strike != NULL) {
        strike->setState(false);
        strike->actuatingState = false;  // Clear actuation state when timer expires
    }
}

void DoorStrike::scheduleDisengage(uint32_t timeoutMs) {
    if (disengageTimer != NULL) {
        xTimerStop(disengageTimer, portMAX_DELAY);
    } else {
        disengageTimer = xTimerCreate(
            "DisengageTimer",
            pdMS_TO_TICKS(timeoutMs),
            pdFALSE,  // One-shot timer
            this,     // Timer ID is this instance
            disengageTimerCallback
        );
    }
    
    if (disengageTimer != NULL) {
        xTimerStart(disengageTimer, portMAX_DELAY);
    }
}

void DoorStrike::engageWithTimeout(uint32_t timeoutMs) {
    // Don't re-actuate if already actuating
    if (actuatingState) {
        return;
    }
    
    actuatingState = true;  // Set actuation state before engaging
    setState(true);
    scheduleDisengage(timeoutMs);
}

bool DoorStrike::isConnected() const {
    // Consider both voltage and actuation state
    return (getVoltage() > 11.0) || actuatingState;
}

bool DoorStrike::isActuating() const {
    return actuatingState;
}

float DoorStrike::getVoltage() const {
    return adc.read(feedbackChannel) * ADC_TO_V * VDIV_SCALE_F;
}

bool DoorStrike::getPhysicalState(bool logicalState) const {
    return (polarity == Polarity::ACTIVE_HIGH) ? logicalState : !logicalState;
} 