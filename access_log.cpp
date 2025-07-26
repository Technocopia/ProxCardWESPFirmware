#include "access_log.h"
#include <time.h>
#include <vector>

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

bool AccessLog::pruneLog(int maxLines) {
    // Don't take mutex if we're already holding it
    bool mutexTaken = false;
    if (xSemaphoreGetMutexHolder(mutex) != xTaskGetCurrentTaskHandle()) {
        if (!takeMutex()) {
            return false;
        }
        mutexTaken = true;
    }

    bool success = false;
    
    // Read all lines from the file
    File file = LittleFS.open(LOG_PATH, FILE_READ);
    if (file) {
        std::vector<String> lines;
        String line;
        
        // Read all lines into memory
        while (file.available()) {
            line = file.readStringUntil('\n');
            if (line.length() > 0) {
                lines.push_back(line);
            }
        }
        file.close();
        
        // If we have more lines than the maximum, keep only the most recent ones
        if (lines.size() > maxLines) {
            // Keep only the last maxLines entries
            std::vector<String> recentLines;
            int startIndex = lines.size() - maxLines;
            
            for (int i = startIndex; i < lines.size(); i++) {
                recentLines.push_back(lines[i]);
            }
            
            // Write the pruned content back to the file
            file = LittleFS.open(LOG_PATH, FILE_WRITE);
            if (file) {
                for (size_t i = 0; i < recentLines.size(); i++) {
                    file.println(recentLines[i]);
                }
                file.close();
                success = true;
                
                Serial.print("Pruned access log from ");
                Serial.print(lines.size());
                Serial.print(" to ");
                Serial.print(recentLines.size());
                Serial.println(" lines");
            }
        } else {
            // No pruning needed
            success = true;
        }
    }

    if (mutexTaken) {
        giveMutex();
    }
    return success;
} 