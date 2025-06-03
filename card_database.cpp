#include "card_database.h"

CardDatabase::CardDatabase() : mutex(NULL) {
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        Serial.println("Error creating card database mutex");
    }
}

CardDatabase::~CardDatabase() {
    if (mutex != NULL) {
        vSemaphoreDelete(mutex);
    }
}

bool CardDatabase::begin() {
    if (!LittleFS.begin(false)) {  // Don't format if mount fails
        Serial.println("LittleFS mount failed");
        return false;
    }
    return initializeFile();
}

bool CardDatabase::takeMutex() {
    if (mutex == NULL) return false;
    return xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void CardDatabase::giveMutex() {
    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

bool CardDatabase::initializeFile() {
    if (!takeMutex()) return false;
    
    bool success = true;
    if (!LittleFS.exists(DATABASE_PATH)) {
        File file = LittleFS.open(DATABASE_PATH, FILE_WRITE);
        if (!file) {
            Serial.println("Failed to create card database file");
            success = false;
        }
        file.close();
    }
    
    giveMutex();
    return success;
}

bool CardDatabase::addCard(unsigned long cardNumber) {
    if (!takeMutex()) return false;
    
    bool success = writeCardToFile(cardNumber);
    giveMutex();
    return success;
}

bool CardDatabase::removeCard(unsigned long cardNumber) {
    if (!takeMutex()) return false;
    
    bool success = removeCardFromFile(cardNumber);
    giveMutex();
    return success;
}

bool CardDatabase::hasCard(unsigned long cardNumber) {
    if (!takeMutex()) return false;
    
    bool found = false;
    File file = LittleFS.open(DATABASE_PATH, FILE_READ);
    if (file) {
        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.toInt() == cardNumber) {
                found = true;
                break;
            }
        }
        file.close();
    }
    
    giveMutex();
    return found;
}

String CardDatabase::getAllCards() {
    if (!takeMutex()) return "";
    
    String cards;
    File file = LittleFS.open(DATABASE_PATH, FILE_READ);
    if (file) {
        while (file.available()) {
            cards += file.readStringUntil('\n') + "\n";
        }
        file.close();
    }
    
    giveMutex();
    return cards;
}

bool CardDatabase::writeCardToFile(unsigned long cardNumber) {
    // First check if card already exists
    if (hasCard(cardNumber)) {
        return true;  // Card already exists, consider it a success
    }
    
    File file = LittleFS.open(DATABASE_PATH, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open card database for writing");
        return false;
    }
    
    file.println(String(cardNumber));
    file.close();
    return true;
}

bool CardDatabase::removeCardFromFile(unsigned long cardNumber) {
    String cards;
    bool found = false;
    
    // Read all cards except the one to remove
    File file = LittleFS.open(DATABASE_PATH, FILE_READ);
    if (file) {
        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.toInt() != cardNumber) {
                cards += line + "\n";
            } else {
                found = true;
            }
        }
        file.close();
    }
    
    // If card wasn't found, return success
    if (!found) {
        return true;
    }
    
    // Write back all cards except the removed one
    file = LittleFS.open(DATABASE_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open card database for writing");
        return false;
    }
    
    file.print(cards);
    file.close();
    return true;
} 