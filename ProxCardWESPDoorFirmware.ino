/*
    This sketch shows the Ethernet event usage

*/
#include "secret.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Wire.h>
#include <ADS7828.h>
#include "FS.h"
#include <LittleFS.h>
#include <static_files.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include "one_param_rewrite.h"
#define FORMAT_LITTLEFS_IF_FAILED true

// Authentication type definition
#define DIGEST_AUTH "Digest"

// Mutex for file access synchronization
SemaphoreHandle_t access_log_mutex = NULL;

// Important to be defined BEFORE including ETH.h for ETH.begin() to work.

#define ETH_PHY_TYPE ETH_PHY_RTL8201
#define ETH_PHY_ADDR -1
#define ETH_PHY_MDC 16
#define ETH_PHY_MDIO 17
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN
#include <ETH.h>
static bool eth_connected = false;

#define SOLENOID_A_PIN 13
#define SOLENOID_B_PIN 18

#define SENSOR_SDA 15
#define SENSOR_SCL 4

#define RS485_DI 33
#define RS485_RO 39
#define RS485_DE 12
#define RS485_RE 36

#define READER_W0 35
#define READER_W1 34

// Global variable to store wigand bits
unsigned long long bitw = 0;
// Bit RX'd Timestamp
unsigned int timeout = 1000;
// Count of RX'd bits in wiegand burst
int bitcnt = 0;

#define LED_D6 14
#define LED_D7 5

ADS7828 adc;
#define ADC_READER_FUSE_FB 0
#define ADC_STRIKE_FB0 1        // Lowside of the coil goes through a VDIV, \
                                // should read ~12v when coil is connected
#define ADC_STRIKE_FB1 2        // Lowside of the coil goes through a VDIV, \
                                // should read ~12v when coil is connected
#define ADC_STRIKE_0_CURRENT 3  // Current through Strike 0
#define ADC_DC_CONNECTOR_FB 4   // Voltage at Aux DC in conn.
#define ADC_12V_FB 5            // Voltage of 12V BUS
#define ADC_STRIKE_1_CURRENT 6  // Current through Strike 1
#define ADC_READER_CURRENT 7    // Current to Card Reader
#define vdiv_scale_f 5.7
#define adc_to_v 0.00061035156

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;
const int daylightOffset_sec = 3600;


AsyncWebServer server(80);
static AsyncAuthenticationMiddleware basicAuth;

// Timer handle for solenoid reset
TimerHandle_t solenoidResetTimer = NULL;

// Timer callback function
void solenoidResetCallback(TimerHandle_t xTimer) {
  int strike = (int)pvTimerGetTimerID(xTimer);
  set_strike(strike, true);
}

void debug_dump_params(AsyncWebServerRequest *request) {
  int params = request->params();
  Serial.println("Params: ");
  for (int i = 0; i < params; i++) {
    const AsyncWebParameter *p = request->getParam(i);
    if (p->isFile()) {  //p->isPost() is also true
      Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
    } else if (p->isPost()) {
      Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
    } else {
      Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
    }
  }  // for(int i=0;i<params;i++)
}

void copy_static_files_to_filesystem() {
  Serial.println("Initializing Web Files");
  File file = LittleFS.open("/index.html", FILE_WRITE);
  file.write(index_html, index_html_len);
  file.close();

  file = LittleFS.open("/access_log.html", FILE_WRITE);
  file.write(access_log_html, access_log_html_len);
  file.close();

  file = LittleFS.open("/diagnostics.html", FILE_WRITE);
  file.write(diagnostics_html, diagnostics_html_len);
  file.close();
}

// WARNING: onEvent is called from a separate FreeRTOS task (thread)!
void onEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      // The hostname must be set after the interface is started, but needs
      // to be set before DHCP, so set it from the event handler thread.
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED: Serial.println("ETH Connected"); break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("ETH Got IP");
      Serial.println(ETH);
      eth_connected = true;
      //init and get the time
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println("ETH Lost IP");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default: break;
  }
}



bool set_strike(int strike, bool state) {
  float v;
  switch (strike) {
    case 0:
      digitalWrite(SOLENOID_A_PIN, state);
      v = adc.read(ADC_STRIKE_FB0) * adc_to_v * vdiv_scale_f;
      break;
    case 1:
      digitalWrite(SOLENOID_B_PIN, state);
      v = adc.read(ADC_STRIKE_FB0) * adc_to_v * vdiv_scale_f;
      break;
  }
  if (state) {
    if (v > 11.0) return true;
  } else {
    if (v < 1.0) return true;
  }
  return false;
}


// Wiegand 0 bit ISR. Triggered by wiegand 0 wire.
void IRAM_ATTR W0ISR() {
  if (digitalRead(READER_W0))
    return;
  //portEXIT_CRITICAL(&mux);
  bitw = (bitw << 1) | 0x0;  // shift in a 0 bit.
  bitcnt++;                  // Increment bit count
  timeout = millis();        // Reset timeout
                             //portEXIT_CRITICAL(&mux);
}

// Wiegand 1 bit ISR. Triggered by wiegand 1 wire.

void IRAM_ATTR W1ISR() {
  if (digitalRead(READER_W1))
    return;
  //portEXIT_CRITICAL(&mux);
  bitw = (bitw << 1) | 0x1;  // shift in a 1 bit
  bitcnt++;                  // Increment bit count
  timeout = millis();        // Reset Timeout
                             //portEXIT_CRITICAL(&mux);
}

// Check for end of wiegand bitstream
// by waiting for last bit rx to be 500 ms ago and the rx of enough bits.
bool haveCard() {
  return (((millis() - timeout) > 500) && bitcnt > 30);
}

// Perform parity calculation
int parity(unsigned long int x) {
  unsigned long int y;
  y = x ^ (x >> 1);
  y = y ^ (y >> 2);
  y = y ^ (y >> 4);
  y = y ^ (y >> 8);
  y = y ^ (y >> 16);
  return y & 1;
}

// Decode card info and clear bit's RX'd.
long int getIDOfCurrentCard() {
  unsigned long long bitwtmp = bitw;
  int bitcnttmp = bitcnt;
  bitcnt = 0;
  bitw = 0;
  //portEXIT_CRITICAL(&mux);
  for (int i = bitcnttmp; i != 0; i--)
    Serial.print((unsigned int)(bitwtmp >> (i - 1) & 0x00000001));  // Print the card number in binary
  Serial.print(" (");
  Serial.print(bitcnttmp);
  Serial.println(")");
  boolean ep, op;
  unsigned int site;
  unsigned long int card;

  // Pick apart card.
  site = (bitwtmp >> 25) & 0x00007f;
  card = (bitwtmp >> 1) & 0xffffff;
  op = (bitwtmp >> 0) & 0x000001;
  ep = (bitwtmp >> 32) & 0x000001;

  // Check parity
  //if (parity(site) != ep)
  //	valid = false;
  //if (parity(card) == op)
  //	valid = false;

  // Print card info
  Serial.print("Got " + String());
  Serial.print("Site: ");
  Serial.println(site);
  Serial.print("Card: ");
  Serial.println(card);
  Serial.print("ep: ");
  Serial.print(parity(site));
  Serial.println(ep);
  Serial.print("op: ");
  Serial.print(parity(card));
  Serial.println(op);

  //valid = true;
  return card;
}


String db_path = "/card_database";

void initialize_card_database() {
  Serial.println("Initializing Database");
  // If the database file is not present, create it
  if (!LittleFS.exists(db_path)) {
    Serial.println("Database Missing, Creating an empty one");
    File file = LittleFS.open(db_path, FILE_WRITE);
    file.print("");
    file.close();
  }
}

bool card_in_database(unsigned long card) {
  File file = LittleFS.open(db_path, FILE_READ);
  while (file.available()) {
    String card_line = file.readStringUntil('\n');
    if (card_line.toInt() == card) {
      file.close();
      return true;
    }
  }
  file.close();
  return false;
}

void add_card_to_database(String card) {
  add_message_to_log("Adding card " + card + " to database");
  File file = LittleFS.open(db_path, FILE_APPEND);
  String san_card = String(card.toInt());
  file.println(san_card);
  file.close();
}

void remove_card_from_database(String card) {
  add_message_to_log("Removing card " + card + " from database");
  String cards_file = "";
  File file = LittleFS.open(db_path, FILE_READ);
  while (file.available()) {
    String card_line = file.readStringUntil('\n');
    if (card_line.toInt() == card.toInt()) {

    } else {
      cards_file += card_line + "\n";
    }
  }
  file.close();

  file = LittleFS.open(db_path, FILE_WRITE);
  file.print(cards_file);
  file.close();
}

void initialize_access_log() {
  Serial.println("Initializing Database");
  // Create mutex for file access
  access_log_mutex = xSemaphoreCreateMutex();
  if (access_log_mutex == NULL) {
    Serial.println("Error creating access log mutex");
    return;
  }

  // If the database file is not present, create it
  if (!LittleFS.exists("/access_log")) {
    Serial.println("Access Log Missing, Creating an empty one");
    if (xSemaphoreTake(access_log_mutex, portMAX_DELAY) == pdTRUE) {
      File file = LittleFS.open("/access_log", FILE_WRITE);
      file.print("");
      file.close();
      xSemaphoreGive(access_log_mutex);
    }
  }
}

void add_card_to_log(unsigned long card, bool valid_card) {
  if (xSemaphoreTake(access_log_mutex, portMAX_DELAY) == pdTRUE) {
    File file = LittleFS.open("/access_log", FILE_APPEND);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
    } else {
      file.print(&timeinfo, "[ %B %d %Y %H:%M:%S ]    ");
    }
    file.print("Access ");
    file.print(valid_card ? "GRANTED" : "DENIED");
    file.println(" for " + String(card));
    file.close();
    xSemaphoreGive(access_log_mutex);
  }
}

void add_message_to_log(String message) {
  if (xSemaphoreTake(access_log_mutex, portMAX_DELAY) == pdTRUE) {
    File file = LittleFS.open("/access_log", FILE_APPEND);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
    } else {
      file.print(&timeinfo, "[ %B %d %Y %H:%M:%S ]    ");
    }
    file.println(message);
    file.close();
    xSemaphoreGive(access_log_mutex);
  }
}

void prune_access_log() {
  if (xSemaphoreTake(access_log_mutex, portMAX_DELAY) == pdTRUE) {
    long max_lines = 10000;
    File in_file = LittleFS.open("/access_log", FILE_READ);

    // count lines
    long lines = 0;
    while (in_file.available()) {
      String card_line = in_file.readStringUntil('\n');
      lines++;
    }

    long start_point = lines - max_lines;
    if (start_point < 1) start_point = 1;
    else {
      Serial.println("Access Log will be pruned");
    }

    in_file.close();

    in_file = LittleFS.open("/access_log", FILE_READ);
    File out_file = LittleFS.open("/access_log_prune", FILE_WRITE);
    lines = 0;
    while (in_file.available()) {
      String card_line = in_file.readStringUntil('\n');
      lines++;
      if (lines < start_point) continue;
      card_line.trim();
      out_file.println(card_line);
    }

    out_file.close();
    in_file.close();
    LittleFS.remove("/access_log");
    LittleFS.rename("/access_log_prune", "/access_log");

    xSemaphoreGive(access_log_mutex);
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Technocopia Card Reader Startup!");
  // setup GPIO for wiegand
  pinMode(READER_W0, INPUT_PULLUP);
  pinMode(READER_W1, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(READER_W0), W0ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(READER_W1), W1ISR, FALLING);

  // setup GPIO for Solenoids
  pinMode(SOLENOID_A_PIN, OUTPUT);
  pinMode(SOLENOID_B_PIN, OUTPUT);
  digitalWrite(SOLENOID_A_PIN, HIGH);
  digitalWrite(SOLENOID_B_PIN, HIGH);

  // Setup i2c for board sensors
  Wire.begin(SENSOR_SDA, SENSOR_SCL);
  adc.begin(0);
  adc.setpd(IREF_ON_AD_ON);

  // Print initial sensor readings
  Serial.print("Reader Fuse: ");
  Serial.print(adc.read(ADC_READER_FUSE_FB) * adc_to_v * vdiv_scale_f);
  Serial.println("v");

  Serial.print("12v BUS: ");
  Serial.print(adc.read(ADC_12V_FB) * adc_to_v * vdiv_scale_f);
  Serial.println("v");

  Serial.print("Strike 0: ");
  Serial.print(adc.read(ADC_STRIKE_FB0) * adc_to_v * vdiv_scale_f);
  Serial.println("v");

  Serial.print("Strike 1: ");
  Serial.print(adc.read(ADC_STRIKE_FB1) * adc_to_v * vdiv_scale_f);
  Serial.println("v");

  // Mount internal Filesystem
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("LittleFS Mount Failed");
    return;
  } else {
    Serial.println("LittleFS Mounted!");
  }
  initialize_card_database();
  initialize_access_log();
  copy_static_files_to_filesystem();

  Network.onEvent(onEvent);
  ETH.begin();

    // Create the solenoid reset timer
  solenoidResetTimer = xTimerCreate(
    "solenoidReset",       // Timer name
    pdMS_TO_TICKS(5000),   // 5 second period
    pdFALSE,               // One-shot timer
    (void *)0,             // Initial timer ID (strike number)
    solenoidResetCallback  // Callback function
  );

  if (solenoidResetTimer == NULL) {
    Serial.println("Error creating solenoid reset timer");
  }

    // basic authentication
  basicAuth.setUsername(www_username);
  basicAuth.setPassword(www_password);
  basicAuth.setRealm("MyApp");
  basicAuth.setAuthFailureMessage("Authentication failed");
  basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
  basicAuth.generateHash();  // precompute hash (optional but recommended)

  // Configure Async Web Server
  // Add rewrites to convert path parameters to query parameters
  server.addRewrite(new OneParamRewrite("/card/{f}", "/card?number={f}"));
  server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/status", "/diagnostics/strike/status?number={f}"));
  server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/current", "/diagnostics/strike/current?number={f}"));
  server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/connected", "/diagnostics/strike/connected?number={f}"));
  server.addRewrite(new OneParamRewrite("/diagnostics/strike/{f}/actuate", "/diagnostics/strike/actuate?number={f}"));

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  }).addMiddleware(&basicAuth);

  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  }).addMiddleware(&basicAuth);

  server.on("/access_log.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/access_log.html", "text/html");
  }).addMiddleware(&basicAuth);

  server.on("/diagnostics.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/diagnostics.html", "text/html");
  }).addMiddleware(&basicAuth);

  // Card management endpoints
  server.on("/card", HTTP_PUT, [](AsyncWebServerRequest *request) {
    debug_dump_params(request);
    if (!request->hasParam("number")) {
      request->send(400, "text/plain", "Missing card number parameter");
      return;
    }
    String card = request->getParam("number")->value();
    // Validate card number is numeric
    if (!card.toInt()) {
      request->send(400, "text/plain", "Invalid card number");
      return;
    }
    add_card_to_database(card);
    request->send(200, "text/plain", card);
  }).addMiddleware(&basicAuth);

  server.on("/card", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    debug_dump_params(request);
    if (!request->hasParam("number")) {
      request->send(400, "text/plain", "Missing card number parameter");
      return;
    }
    String card = request->getParam("number")->value();
    // Validate card number is numeric
    if (!card.toInt()) {
      request->send(400, "text/plain", "Invalid card number");
      return;
    }
    remove_card_from_database(card);
    request->send(200, "text/plain", card);
  }).addMiddleware(&basicAuth);

  server.on("/card", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "");
  }).addMiddleware(&basicAuth);

  server.on("/cards", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, db_path, "text/plain");
  }).addMiddleware(&basicAuth);

  // Diagnostics endpoints
  server.on("/diagnostics/strike/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
      request->send(400, "text/plain", "Missing strike number parameter");
      return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    // Validate strike number is 0 or 1
    if (strike > 1) {
      request->send(400, "text/plain", "Invalid strike number");
      return;
    }
    int adc_chan = (strike == 0) ? ADC_STRIKE_FB0 : ADC_STRIKE_FB1;
    float strike_v = adc.read(adc_chan) * adc_to_v * vdiv_scale_f;
    String status = (strike_v > 11.0) ? "Good Electrical Connection" : "Bad connection or burnt out";
    request->send(200, "text/plain", status);
  }).addMiddleware(&basicAuth);

  server.on("/diagnostics/strike/current", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
      request->send(400, "text/plain", "Missing strike number parameter");
      return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    // Validate strike number is 0 or 1
    if (strike > 1) {
      request->send(400, "text/plain", "Invalid strike number");
      return;
    }
    int adc_chan = (strike == 0) ? ADC_STRIKE_0_CURRENT : ADC_STRIKE_1_CURRENT;
    float strike_a = (adc.read(adc_chan) * adc_to_v * vdiv_scale_f) / 0.1;
    request->send(200, "text/plain", String(strike_a));
  }).addMiddleware(&basicAuth);

  server.on("/diagnostics/strike/connected", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
      request->send(400, "text/plain", "Missing strike number parameter");
      return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    // Validate strike number is 0 or 1
    if (strike > 1) {
      request->send(400, "text/plain", "Invalid strike number");
      return;
    }
    int adc_chan = (strike == 0) ? ADC_STRIKE_FB0 : ADC_STRIKE_FB1;
    float strike_v = adc.read(adc_chan) * adc_to_v * vdiv_scale_f;
    request->send(200, "text/plain", (strike_v > 11.0) ? "true" : "false");
  }).addMiddleware(&basicAuth);

  // Update the actuate endpoint to use the timer
  server.on("/diagnostics/strike/actuate", HTTP_PUT, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("number")) {
      request->send(400, "text/plain", "Missing strike number parameter");
      return;
    }
    String strikeStr = request->getParam("number")->value();
    unsigned int strike = strikeStr.toInt();
    // Validate strike number is 0 or 1
    if (strike > 1) {
      request->send(400, "text/plain", "Invalid strike number");
      return;
    }
    if (set_strike(strike, false)) {
      // Set the timer ID to the strike number
      vTimerSetTimerID(solenoidResetTimer, (void *)strike);
      // Start the timer
      if (xTimerStart(solenoidResetTimer, 0) != pdPASS) {
        Serial.println("Error starting solenoid reset timer");
        request->send(500, "text/plain", "Error scheduling strike reset");
        return;
      }
      request->send(200, "text/plain", "OK");
    } else {
      request->send(200, "text/plain", "Fail");
    }
  }).addMiddleware(&basicAuth);

  server.on("/diagnostics/strikes", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "0\n1\n");
  }).addMiddleware(&basicAuth);

  server.on("/diagnostics/cardreader/current", HTTP_GET, [](AsyncWebServerRequest *request) {
    float reader_a = (adc.read(ADC_READER_CURRENT) * adc_to_v * vdiv_scale_f) / 0.1;
    request->send(200, "text/plain", String(reader_a));
  }).addMiddleware(&basicAuth);

  server.on("/diagnostics/cardreader/fuse", HTTP_GET, [](AsyncWebServerRequest *request) {
    float fuse_v = (adc.read(ADC_READER_FUSE_FB) * adc_to_v * vdiv_scale_f);
    request->send(200, "text/plain", (fuse_v > 11.0) ? "true" : "false");
  }).addMiddleware(&basicAuth);

  server.on("/access", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/access_log", "text/plain");
  }).addMiddleware(&basicAuth);

  // Start the web server
  server.begin();
  Serial.println("HTTP server started");
}

long prune_counter = 0;
void loop() {
  prune_counter++;
  if (prune_counter > (10 * 60 * 60 * 12)) {
    prune_access_log();
    prune_counter = 0;
  }


  if (eth_connected) {
  }
  if (haveCard()) {
    Serial.println("I have a card!");
    unsigned long card = getIDOfCurrentCard();
    if (card_in_database(card)) {
      add_card_to_log(card, true);
      Serial.println("Entry Granted!");
      set_strike(0, false);
      set_strike(1, false);
      delay(5000);
      set_strike(0, true);
      set_strike(1, true);

    } else {
      add_card_to_log(card, false);
      Serial.println("INVALID Card!");
    }
    Serial.println(card);
  }
}
