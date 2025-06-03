#pragma once

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include "one_param_rewrite.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "card_reader.h"
#include "door_strike.h"
#include "card_database.h"
#include "access_log.h"

class CardReaderWebServer {
public:
    CardReaderWebServer(CardReader* readers, size_t numReaders, 
                       DoorStrike* strikes, size_t numStrikes,
                       CardDatabase& cardDb, AccessLog& accessLog);
    ~CardReaderWebServer();
    void begin();
    void update();
    
private:
    // Server instance
    AsyncWebServer server;
    AsyncAuthenticationMiddleware basicAuth;
    
    // Hardware components
    CardReader* readers;
    size_t numReaders;
    DoorStrike* strikes;
    size_t numStrikes;
    
    // Database and log references
    CardDatabase& cardDb;
    AccessLog& accessLog;
    
    // Helper functions
    void setupRoutes();
    void setupAuthentication();
    void copyStaticFiles();
    void debugDumpParams(AsyncWebServerRequest *request);
    
    // Route handlers
    void handleRoot(AsyncWebServerRequest *request);
    void handleCardManagement(AsyncWebServerRequest *request);
    void handleDiagnostics(AsyncWebServerRequest *request);
    void handleAccessLog(AsyncWebServerRequest *request);
    
    // Card management endpoints
    void handleAddCard(AsyncWebServerRequest *request);
    void handleRemoveCard(AsyncWebServerRequest *request);
    void handleListCards(AsyncWebServerRequest *request);
    
    // Diagnostics endpoints
    void handleStrikeStatus(AsyncWebServerRequest *request);
    void handleStrikeCurrent(AsyncWebServerRequest *request);
    void handleStrikeConnected(AsyncWebServerRequest *request);
    void handleStrikeActuate(AsyncWebServerRequest *request);
    void handleStrikeList(AsyncWebServerRequest *request);
    void handleCardReaderCurrent(AsyncWebServerRequest *request);
    void handleCardReaderFuse(AsyncWebServerRequest *request);
    void handleCardReaderList(AsyncWebServerRequest *request);
    void handleCardReaderBurst(AsyncWebServerRequest *request);
    
    // Access log endpoints
    void handleAccessLogGet(AsyncWebServerRequest *request);
    
    // Static file paths
    static constexpr const char* INDEX_HTML = "/index.html";
    static constexpr const char* ACCESS_LOG_HTML = "/access_log.html";
    static constexpr const char* DIAGNOSTICS_HTML = "/diagnostics.html";
    static constexpr const char* ACCESS_LOG_PATH = "/access_log";
}; 