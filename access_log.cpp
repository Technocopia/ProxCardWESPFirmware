#include "access_log.h"
#include <time.h>

AccessLog::AccessLog() : mutex(NULL) {
    mutex = xSemaphoreCreateMutex();
}

AccessLog::~AccessLog() {
    if (mutex != NULL) {
        vSemaphoreDelete(mutex);
    }
}

bool AccessLog::begin() {
    if (!initializeFile()) {
        Serial.println("Failed to initialize access log file");
        return false;
    }
    return true;
}

bool AccessLog::takeMutex() {
    if (mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void AccessLog::giveMutex() {
    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

bool AccessLog::initializeFile() {
    if (!takeMutex()) {
        return false;
    }

    bool success = true;
    if (!LittleFS.exists(LOG_PATH)) {
        File file = LittleFS.open(LOG_PATH, FILE_WRITE);
        if (!file) {
            Serial.println("Failed to create access log file");
            success = false;
        } else {
            file.print("");  // Create empty file
            file.close();
        }
    }

    giveMutex();
    return success;
}

String AccessLog::getTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Time not set";
    }
    
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStr);
}

bool AccessLog::writeToLog(const String& entry) {
    if (!takeMutex()) {
        return false;
    }

    bool success = false;
    File file = LittleFS.open(LOG_PATH, FILE_APPEND);
    if (file) {
        file.println(entry);
        file.close();
        success = true;
    }

    giveMutex();
    return success;
}

bool AccessLog::addCardAccess(unsigned long cardNumber, bool accessGranted) {
    String timestamp = getTimestamp();
    String status = accessGranted ? "GRANTED" : "DENIED";
    String entry = timestamp + " - Card " + String(cardNumber) + " - Access " + status;
    return writeToLog(entry);
}

bool AccessLog::addMessage(const String& message) {
    String timestamp = getTimestamp();
    String entry = timestamp + " - " + message;
    return writeToLog(entry);
}

String AccessLog::getLogContents() {
    if (!takeMutex()) {
        return "Error: Could not access log file";
    }

    String contents;
    File file = LittleFS.open(LOG_PATH, FILE_READ);
    if (file) {
        contents = file.readString();
        file.close();
    }

    giveMutex();
    return contents;
} 