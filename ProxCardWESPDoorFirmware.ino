/*
    This sketch shows the Ethernet event usage

*/

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Wire.h>
#include <ADS7828.h>
#include "FS.h"
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include "one_param_rewrite.h"
#include "webserver.h"
#include "card_reader.h"
#include "door_strike.h"
#include "adc_channels.h"
#include "card_database.h"
#include "access_log.h"
#include "adc_channels.h"
#include <ArduinoJson.h>
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

#define READER_W0 35  // Reader 0 Data 0
#define READER_W1 34  // Reader 0 Data 1

// Hardware component arrays
ADS7828 adc;
CardReader readers[] = {
    CardReader(adc, READER_W0, READER_W1, ADC_READER_FUSE_FB, ADC_READER_CURRENT, true)    // Reader 0 - Ignore parity errors
};
DoorStrike strikes[] = {
    DoorStrike(adc, 0, SOLENOID_A_PIN, ADC_STRIKE_FB0, ADC_STRIKE_0_CURRENT, 
               DoorStrike::Polarity::ACTIVE_LOW),  // Strike 0 - Active High
    DoorStrike(adc, 1, SOLENOID_B_PIN, ADC_STRIKE_FB1, ADC_STRIKE_1_CURRENT,
               DoorStrike::Polarity::ACTIVE_LOW)    // Strike 1 - Active Low
};

// Calculate array sizes
const size_t NUM_READERS = sizeof(readers)/sizeof(readers[0]);
const size_t NUM_STRIKES = sizeof(strikes)/sizeof(strikes[0]);

CardDatabase cardDb;
AccessLog accessLog;
CardReaderWebServer webServer(readers, NUM_READERS, strikes, NUM_STRIKES, cardDb, accessLog);

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;
const int daylightOffset_sec = 3600;


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

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Technocopia Card Reader Startup!");
  
  // Setup I2C for ADC
  Wire.begin(SENSOR_SDA, SENSOR_SCL);
  adc.begin(0);
  adc.setpd(IREF_ON_AD_ON);
  Serial.println("ADC initialized");

  
  // Initialize hardware components
  for (size_t i = 0; i < NUM_READERS; i++) {
    readers[i].begin();
    Serial.print("Reader ");
    Serial.print(i);
    Serial.println(" initialized");
  }
  for (size_t i = 0; i < NUM_STRIKES; i++) {
    strikes[i].begin();
    Serial.print("Strike ");
    Serial.print(i);
    Serial.println(" initialized");
    Serial.print("\tVoltage: ");
    Serial.println(strikes[i].getVoltage());
    Serial.print("\tCurrent: ");
    Serial.println(strikes[i].getCurrent());
  }

  // Mount internal Filesystem
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("LittleFS Mount Failed");
    return;
  } else {
    Serial.println("LittleFS Mounted!");
  }
  
  // Initialize card database and access log
  if (!cardDb.begin()) {
    Serial.println("Failed to initialize card database");
    return;
  } else {
    Serial.println("Card database initialized");
  }
  
  if (!accessLog.begin()) {
    Serial.println("Failed to initialize access log");
    return;
  } else {
    Serial.println("Access log initialized");
  }

  Network.onEvent(onEvent);
  ETH.begin();

  // Initialize and start the webserver
  webServer.begin();
  Serial.println("Webserver initialized");

}

void loop() {
    // Handle web server clients
    webServer.update();

  if (eth_connected) {
        webServer.update();
    }
    
    // Check all readers for cards
    for (size_t i = 0; i < NUM_READERS; i++) {
        // Update current readings buffer
        readers[i].update();
        
        //readers[i].printDebug();  // Print debug info for each reader
        
        if (readers[i].isCardPresent()) {
            Serial.print("Reader ");
            Serial.print(i);
            Serial.println(" has a card!");
            
            long card = readers[i].getCardId();
            if (card<0) continue;
            if (cardDb.hasCard(card)) {
                accessLog.addCardAccess(card, true);
      Serial.println("Entry Granted!");

                // Engage all strikes with automatic timeout
                for (size_t j = 0; j < NUM_STRIKES; j++) {
                    strikes[j].engageWithTimeout(5000); // 5 second timeout
                }
    } else {
                accessLog.addCardAccess(card, false);
      Serial.println("INVALID Card!");
    }
    Serial.println(card);
  }
    }
    
    delay(1000);  // Add a delay to prevent flooding the serial output
}
