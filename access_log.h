#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class AccessLog {
public:
    AccessLog();
    ~AccessLog();

    // Initialize the access log
    bool begin();

    // Add a card access entry to the log
    bool addCardAccess(unsigned long cardNumber, bool accessGranted);

    // Add a custom message to the log
    bool addMessage(const String& message);

    // Get the current log contents
    String getLogContents();

private:
    // File path
    static constexpr const char* LOG_PATH = "/access_log";

    // Mutex for file access synchronization
    SemaphoreHandle_t mutex;

    // Helper functions
    bool takeMutex();
    void giveMutex();
    bool initializeFile();
    String getTimestamp();
    bool writeToLog(const String& entry);
}; 