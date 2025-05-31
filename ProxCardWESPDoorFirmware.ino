/*
    This sketch shows the Ethernet event usage

*/

#include <Wire.h>
#include <ADS7828.h>

// Important to be defined BEFORE including ETH.h for ETH.begin() to work.

#define ETH_PHY_TYPE ETH_PHY_RTL8201
#define ETH_PHY_ADDR  -1
#define ETH_PHY_MDC   16
#define ETH_PHY_MDIO  17
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN
#include <ETH.h>
static bool eth_connected = false;

#define SOLENOID_A_PIN 13
#define SOLENOID_B_PIN 18

#define SENSOR_SDA      15
#define SENSOR_SCL      4

#define RS485_DI        33
#define RS485_RO        39
#define RS485_DE        12
#define RS485_RE        36

#define READER_W0       35
#define READER_W1       34

// Global variable to store wigand bits
unsigned long long bitw = 0;
// Bit RX'd Timestamp
unsigned int timeout = 1000;
// Count of RX'd bits in wiegand burst
int bitcnt = 0;

#define LED_D6          14
#define LED_D7          5

ADS7828 adc;
#define ADC_READER_FUSE_FB    0
#define ADC_STRIKE_FB0        1   // Lowside of the coil goes through a VDIV,
                                  // should read ~12v when coil is connected
#define ADC_STRIKE_FB1        2   // Lowside of the coil goes through a VDIV,
                                  // should read ~12v when coil is connected
#define ADC_STRIKE_0_CURRENT  3   // Current through Strike 0
#define ADC_DC_CONNECTOR_FB   4   // Voltage at Aux DC in conn.
#define ADC_12V_FB            5   // Voltage of 12V BUS
#define ADC_STRIKE_1_CURRENT  6   // Current through Strike 1
#define ADC_READER_CURRENT    7   // Current to Card Reader

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






// Wiegand 0 bit ISR. Triggered by wiegand 0 wire.
void IRAM_ATTR W0ISR() {
	if (digitalRead(READER_W0))
		return;
	//portEXIT_CRITICAL(&mux);
	bitw = (bitw << 1) | 0x0; // shift in a 0 bit.
	bitcnt++;               // Increment bit count
	timeout = millis();         // Reset timeout
	//portEXIT_CRITICAL(&mux);

}

// Wiegand 1 bit ISR. Triggered by wiegand 1 wire.

void IRAM_ATTR W1ISR() {
	if (digitalRead(READER_W1))
		return;
	//portEXIT_CRITICAL(&mux);
	bitw = (bitw << 1) | 0x1; // shift in a 1 bit
	bitcnt++;               // Increment bit count
	timeout = millis();         // Reset Timeout
	//portEXIT_CRITICAL(&mux);
}

// Check for end of wiegand bitstream
// by waiting for last bit rx to be 500 ms ago and the rx of enough bits.
bool haveCard() {
	return (((millis() - timeout) > 500) && bitcnt > 30);
}

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
		Serial.print((unsigned int) (bitwtmp >> (i - 1) & 0x00000001)); // Print the card number in binary
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

void i2c_scan() {
  byte error, address;
  int nDevices;
  Serial.println();
  Serial.println("Scanning...");
  nDevices = 0;
  for (address = 1; address < 127; address++ )  {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
  delay(5000); // wait 5 seconds for next scan
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
  Wire.begin(SENSOR_SDA,SENSOR_SCL);
  //i2c_scan();
  adc.begin(0);

  Serial.print("Reader Fuse: ");
  Serial.print(adc.read(ADC_READER_FUSE_FB));
  Serial.println("v");

  Serial.print("12v BUS: ");
  Serial.print(adc.read(ADC_12V_FB));
  Serial.println("v");

  Serial.print("Strike 0: ");
  Serial.print(adc.read(ADC_STRIKE_FB0));
  Serial.println("v");

  Serial.print("Strike 1: ");
  Serial.print(adc.read(ADC_STRIKE_FB0));
  Serial.println("v");

  Network.onEvent(onEvent);
  ETH.begin();
}

void loop() {
  if (eth_connected) {
    
  }
  if (haveCard()){
    Serial.println("I have a card!");
    unsigned long card = getIDOfCurrentCard();
    Serial.println(card);
  }
  delay(100);
}
