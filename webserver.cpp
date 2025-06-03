#include "webserver.h"
#include "static_files.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ADS7828.h>
#include "secret.h"
#include "card_database.h"

// External declarations
extern SemaphoreHandle_t access_log_mutex;
extern bool set_strike(int strike, bool state);
extern float adc_to_v;
extern float vdiv_scale_f;
extern ADS7828 adc;

// ADC channel definitions
#define ADC_READER_FUSE_FB 0
#define ADC_STRIKE_FB0 1
#define ADC_STRIKE_FB1 2
#define ADC_STRIKE_0_CURRENT 3
#define ADC_DC_CONNECTOR_FB 4
#define ADC_12V_FB 5
#define ADC_STRIKE_1_CURRENT 6
#define ADC_READER_CURRENT 7

// Remove static instance since we have a global one
// static CardReaderWebServer webServerInstance;

CardReaderWebServer::CardReaderWebServer(CardReader* readers, size_t numReaders, DoorStrike* strikes, size_t numStrikes, CardDatabase& cardDb, AccessLog& accessLog)
    : server(80), readers(readers), numReaders(numReaders), strikes(strikes), numStrikes(numStrikes), cardDb(cardDb), accessLog(accessLog) {
}

CardReaderWebServer::~CardReaderWebServer() {
}

void CardReaderWebServer::begin() {
    setupAuthentication();
    setupRoutes();
    copyStaticFiles();
    server.begin();
    Serial.println("HTTP server started");
}

void CardReaderWebServer::update() {
    // Nothing to do in update for now
}

void CardReaderWebServer::setupAuthentication() {
    basicAuth.setUsername(www_username);
    basicAuth.setPassword(www_password);
    basicAuth.setRealm("MyApp");
    basicAuth.setAuthFailureMessage("Authentication failed");
    basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
    basicAuth.generateHash();
}

void CardReaderWebServer::copyStaticFiles() {
    Serial.println("Initializing Web Files");
    
    File file = LittleFS.open(INDEX_HTML, FILE_WRITE);
    file.write(index_html, index_html_len);
    file.close();

    file = LittleFS.open(ACCESS_LOG_HTML, FILE_WRITE);
    file.write(access_log_html, access_log_html_len);
    file.close();

    file = LittleFS.open(DIAGNOSTICS_HTML, FILE_WRITE);
    file.write(diagnostics_html, diagnostics_html_len);
    file.close();
}

void CardReaderWebServer::setupRoutes() {
    // Add rewrites for path parameters
    server.addRewrite(new OneParamRewrite("/card/{f}", "/card?number={f}"));
    server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/status", "/diagnostics/strike/status?number={f}"));
    server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/current", "/diagnostics/strike/current?number={f}"));
    server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/connected", "/diagnostics/strike/connected?number={f}"));
    server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/actuate", "/diagnostics/strike/actuate?number={f}"));
    server.addRewrite(new OneParamRewrite("/diagnostics/cardreader/{f}/current", "/diagnostics/cardreader/current?number={f}"));
    server.addRewrite(new OneParamRewrite("/diagnostics/cardreader/{f}/fuse", "/diagnostics/cardreader/fuse?number={f}"));

    // Backward compatibility rewrites for old reader endpoints
    server.addRewrite(new AsyncWebRewrite("/diagnostics/cardreader/current", "/diagnostics/cardreader/current?number=0"));
    server.addRewrite(new AsyncWebRewrite("/diagnostics/cardreader/fuse", "/diagnostics/cardreader/fuse?number=0"));

    // Main pages
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleRoot(request);
    }).addMiddleware(&basicAuth);

    server.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleRoot(request);
    }).addMiddleware(&basicAuth);

    server.on("/access_log.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleAccessLog(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleDiagnostics(request);
    }).addMiddleware(&basicAuth);

    // Card management endpoints
    server.on("/card", HTTP_PUT, [this](AsyncWebServerRequest *request) {
        handleAddCard(request);
    }).addMiddleware(&basicAuth);

    server.on("/card", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        handleRemoveCard(request);
    }).addMiddleware(&basicAuth);

    server.on("/card", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "");
    }).addMiddleware(&basicAuth);

    server.on("/cards", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleListCards(request);
    }).addMiddleware(&basicAuth);

    // Diagnostics endpoints
    server.on("/diagnostics/strike/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleStrikeStatus(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics/strike/current", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleStrikeCurrent(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics/strike/connected", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleStrikeConnected(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics/strike/actuate", HTTP_PUT, [this](AsyncWebServerRequest *request) {
        handleStrikeActuate(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics/strikes", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleStrikeList(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics/cardreader/current", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleCardReaderCurrent(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics/cardreader/fuse", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleCardReaderFuse(request);
    }).addMiddleware(&basicAuth);

    server.on("/diagnostics/cardreader/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleCardReaderList(request);
    }).addMiddleware(&basicAuth);

    // Access log endpoint
    server.on("/access", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleAccessLogGet(request);
    }).addMiddleware(&basicAuth);
}

void CardReaderWebServer::debugDumpParams(AsyncWebServerRequest *request) {
    int params = request->params();
    Serial.println("Params: ");
    for (int i = 0; i < params; i++) {
        const AsyncWebParameter *p = request->getParam(i);
        if (p->isFile()) {
            Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
        } else if (p->isPost()) {
            Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        } else {
            Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
    }
}

void CardReaderWebServer::handleRoot(AsyncWebServerRequest *request) {
    request->send(LittleFS, INDEX_HTML, "text/html");
}

void CardReaderWebServer::handleAccessLog(AsyncWebServerRequest *request) {
    request->send(LittleFS, ACCESS_LOG_HTML, "text/html");
}

void CardReaderWebServer::handleDiagnostics(AsyncWebServerRequest *request) {
    request->send(LittleFS, DIAGNOSTICS_HTML, "text/html");
}

void CardReaderWebServer::handleAddCard(AsyncWebServerRequest *request) {
    debugDumpParams(request);
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing card number parameter");
        return;
    }
    String card = request->getParam("number")->value();
    if (!card.toInt()) {
        request->send(400, "text/plain", "Invalid card number");
        return;
    }
    
    if (cardDb.addCard(card.toInt())) {
        request->send(200, "text/plain", card);
    } else {
        request->send(500, "text/plain", "Failed to add card");
    }
}

void CardReaderWebServer::handleRemoveCard(AsyncWebServerRequest *request) {
    debugDumpParams(request);
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing card number parameter");
        return;
    }
    String card = request->getParam("number")->value();
    if (!card.toInt()) {
        request->send(400, "text/plain", "Invalid card number");
        return;
    }
    
    if (cardDb.removeCard(card.toInt())) {
        request->send(200, "text/plain", card);
    } else {
        request->send(500, "text/plain", "Failed to remove card");
    }
}

void CardReaderWebServer::handleListCards(AsyncWebServerRequest *request) {
    String cards = cardDb.getAllCards();
    request->send(200, "text/plain", cards);
}

void CardReaderWebServer::handleStrikeStatus(AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing strike number parameter");
        return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    if (strike >= numStrikes) {
        request->send(400, "text/plain", "Invalid strike number");
        return;
    }
    
    float strike_v = strikes[strike].getVoltage();
    String status = (strike_v > 11.0) ? "Good Electrical Connection" : "Bad connection or burnt out";
    request->send(200, "text/plain", status);
}

void CardReaderWebServer::handleStrikeCurrent(AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing strike number parameter");
        return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    if (strike >= numStrikes) {
        request->send(400, "text/plain", "Invalid strike number");
        return;
    }
    
    float strike_a = strikes[strike].getCurrent();
    request->send(200, "text/plain", String(strike_a));
}

void CardReaderWebServer::handleStrikeConnected(AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing strike number parameter");
        return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    if (strike >= numStrikes) {
        request->send(400, "text/plain", "Invalid strike number");
        return;
    }
    
    float strike_v = strikes[strike].getVoltage();
    request->send(200, "text/plain", (strike_v > 11.0) ? "true" : "false");
}

void CardReaderWebServer::handleStrikeActuate(AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing strike number parameter");
        return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    if (strike >= numStrikes) {
        request->send(400, "text/plain", "Invalid strike number");
        return;
    }
    
    strikes[strike].engageWithTimeout(5000); // 5 second timeout
    request->send(200, "text/plain", "OK");
}

void CardReaderWebServer::handleStrikeList(AsyncWebServerRequest *request) {
    String response;
    for (size_t i = 0; i < numStrikes; i++) {
        response += String(i) + "\n";
    }
    request->send(200, "text/plain", response);
}

void CardReaderWebServer::handleCardReaderCurrent(AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing reader number parameter");
        return;
    }
    String readerStr = request->getParam("number")->value();
    unsigned int reader = readerStr.toInt();
    if (reader >= numReaders) {
        request->send(400, "text/plain", "Invalid reader number");
        return;
    }
    
    float reader_a = readers[reader].getCurrent();
    request->send(200, "text/plain", String(reader_a));
}

void CardReaderWebServer::handleCardReaderFuse(AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
        request->send(400, "text/plain", "Missing reader number parameter");
        return;
    }
    String readerStr = request->getParam("number")->value();
    unsigned int reader = readerStr.toInt();
    if (reader >= numReaders) {
        request->send(400, "text/plain", "Invalid reader number");
        return;
    }
    
    bool fuseGood = readers[reader].isFuseGood();
    request->send(200, "text/plain", fuseGood ? "true" : "false");
}

void CardReaderWebServer::handleCardReaderList(AsyncWebServerRequest *request) {
    String response;
    for (size_t i = 0; i < numReaders; i++) {
        response += String(i) + "\n";
    }
    request->send(200, "text/plain", response);
}

void CardReaderWebServer::handleAccessLogGet(AsyncWebServerRequest *request) {
    String logContents = accessLog.getLogContents();
    request->send(200, "text/plain", logContents);
} 