#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class CardDatabase {
public:
    CardDatabase();
    ~CardDatabase();

    // Initialize the database
    bool begin();

    // Card management functions
    bool addCard(unsigned long cardNumber);
    bool removeCard(unsigned long cardNumber);
    bool hasCard(unsigned long cardNumber);
    String getAllCards();

private:
    // File path
    static constexpr const char* DATABASE_PATH = "/card_database";

    // Mutex for file access synchronization
    SemaphoreHandle_t mutex;

    // Helper functions
    bool takeMutex();
    void giveMutex();
    bool initializeFile();
    bool writeCardToFile(unsigned long cardNumber);
    bool removeCardFromFile(unsigned long cardNumber);
}; 