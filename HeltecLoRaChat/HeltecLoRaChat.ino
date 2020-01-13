/* 
 *  USB UART Drivers:
 *  https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers
 *  Needed to get the COM port to show up in the Arduino IDE
 *  
 *  Device SDK & Docs:
 *  https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series
 *  
 *  Board Manager URL:
 *  https://docs.heltec.cn/download/package_heltec_esp32_index.json
 *  
 *  The onboard OLED display is SSD1306 driver and I2C interface. In order to make the
 *  OLED correctly operation, you should output a high-low-high(1-0-1) signal by soft-
 *  ware to OLED's reset pin, the low-level signal at least 5ms.
 *  
 *  ESP32 Pin Reference:
 *  GPIO25 - LED
 *  GPIO13 - For battery voltage detection
 *  
 *  OLED pins:
 *  OLED_SDA -- GPIO4
 *  OLED_SCL -- GPIO15
 *  OLED_RST -- GPIO16
 *  
 *  SX1278 LoRa Pins:
 *  GPIO5 — SX1278’s SCK
 *  GPIO19 — SX1278’s MISO
 *  GPIO27 — SX1278’s MOSI
 *  GPIO18 — SX1278’s CS
 *  GPIO14 — SX1278’s RESET
 *  GPIO26 — SX1278’s IRQ(Interrupt Request)
 */

// Includes
#include <SSD1306Wire.h>
/*
 * you need to install the ESP8266 oled driver for SSD1306
 * by Daniel Eichorn and Fabrice Weinberg to get this file!
 * It's in the arduino library manager :-D
 * https://github.com/ThingPulse/esp8266-oled-ssd1306
 */
#include <SPI.h>
#include <LoRa.h>
/*
 * This is the one by Sandeep Mistry (also in the Arduino Library manager)
 * https://github.com/sandeepmistry/arduino-LoRa
 */

// Bluetooth libraries
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
float txValue = 0;
// display descriptor
SSD1306Wire display(0x3c, 4, 15);

#define FullBatteryVoltage 3700 // A normal 1S LiPo battery is 3700mv when fully charged.
float XS = 0.00225; //The returned reading is multiplied by this XS to get the battery voltage.
uint16_t MUL = 1000;
uint16_t MMUL = 100;

const long batteryCheckInterval = 10000;
unsigned long previousBatteryCheckMillis = 0;

const int ledPin = 25;
int ledState = LOW;
const long ledBlinkInterval = 100;
unsigned long previousLedBlinkMillis = 0;
int remainingLedBlinks = 0;

//SPI defs for LoRa radio
#define SS 18
#define RST 14
#define DI0 26

// LoRa Settings
#define BAND 868E6 // You can set band here directly, e.g. 433E6, 470E6, 868E6, 915E6
#define spreadingFactor 10 // Supported values are between 6 and 12.
#define SignalBandwidth 31.25E3 // Supported values are 7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, and 250E3
#define codingRateDenominator 8 // Supported values are between 5 and 8, these correspond to coding rates of 4/5 and 4/8. The coding rate numerator is fixed at 4.
#define preambleLength 8

// misc vars
String msg;
String displayName;
String sendMsg;
char chr;
int i = 0;

// Helpers func.s for LoRa
String mac2str(int mac)
{
  String s;
  byte *arr = (byte*) &mac;
  for (byte i = 0; i < 6; ++i) {
    char buff[3];
    // yea, this is a sprintf... fite me...
    sprintf(buff, "%2X", arr[i]);
    s += buff;
    if (i < 5) s += ':';
  }
  return s;
}

void blinkLED(int count) {
  remainingLedBlinks = count;
}

void blinkCheck() {
  if (remainingLedBlinks > 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousLedBlinkMillis >= ledBlinkInterval) {
      // save the last time you blinked the LED
      previousLedBlinkMillis = currentMillis;
  
      // if the LED is off turn it on and vice-versa:
      if (ledState == LOW) {
        ledState = HIGH;
        remainingLedBlinks--;
      } else {
        ledState = LOW;
      }
  
      // set the LED with the ledState of the variable:
      digitalWrite(ledPin, ledState);
    }
  } else {
    // Always set the LED off again at the end
    ledState = LOW;
    digitalWrite(ledPin, ledState);
  }
}

void checkBattery() {
  unsigned long currentMillis = millis();
  if (previousBatteryCheckMillis == 0 || (currentMillis - previousBatteryCheckMillis >= batteryCheckInterval)) {
    // save the last time you checked
    previousBatteryCheckMillis = currentMillis;

    // Read the battery
    uint16_t batteryMilliVolts  =  analogRead(13)*XS*MUL;
    uint16_t batteryPercentage  =  (analogRead(13)*XS*MUL*MMUL)/FullBatteryVoltage;

    display.setColor(BLACK);
    display.fillRect(0, 0, 128, 12);
    display.setColor(WHITE);
    
    display.drawString(0, 0, "VBAT:");
    display.drawString(35, 0, (String)batteryMilliVolts);
    display.drawString(60, 0, "(mV)");
    display.drawString(90, 0, (String)batteryPercentage);
    display.drawString(98, 0, ".");
    display.drawString(107, 0, "%");
    display.display();
  }
}

void setup() {
  // Setup battery voltage readings
  adcAttachPin(13);
  analogSetClockDiv(255); // 1338mS

  // Set up the LED
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);
  
  // Reset the display
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW); // set GPIO16 low to reset OLED
  delay(50);
  digitalWrite(16, HIGH);
  Serial.begin(115200);
  while (!Serial); // If just the the basic function, must connect to a computer

  // Initialising the UI will init the display too.
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(5, 5, "LoRa Chat Node");
  display.display();

  SPI.begin(5, 19, 27, 18);
  LoRa.setPins(SS, RST, DI0);
  Serial.println("LoRa Chat Node");
  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  Serial.print("LoRa Spreading Factor: ");
  Serial.println(spreadingFactor);
  LoRa.setSpreadingFactor(spreadingFactor);

  Serial.print("LoRa Signal Bandwidth: ");
  Serial.println(SignalBandwidth);
  LoRa.setSignalBandwidth(SignalBandwidth);
  LoRa.setCodingRate4(codingRateDenominator);
  LoRa.setPreambleLength(preambleLength);

  /* Radios with different Sync Words will not receive each other's transmissions.
   * This is one way you can filter out radios you want to ignore, without making an addressing scheme.
   */
  LoRa.setSyncWord(0x64); // byte value to use as the sync word, defaults to 0x12

  Serial.println("LoRa Initial OK!");
  display.drawString(5, 20, "LoRaChat is running!");
  display.display();
  delay(2000);
  
  Serial.println("Welcome to LoRaCHAT!");
  // get MAC as initial Nick
  int MAC = ESP.getEfuseMac();
  displayName = mac2str(MAC);
  
  Serial.print("Initial nick is "); Serial.println(displayName);
  Serial.println("Use /? for command help...");
  Serial.println(": ");
  display.clear();
  
  display.drawString(5, 20, "Nickname set:");
  display.drawString(20, 30, displayName);
  display.display();
  delay(1000);
}

void loop() {
  
  // Receive a message first...
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    display.clear();
    display.drawString(3, 0, "Received Message!");
    display.display();
    while (LoRa.available()) {
      String data = LoRa.readString();
      display.drawString(20, 22, data);
      display.display();
      Serial.println(data);
    }
  } // once we're done there, we read bytes from Serial
  if (Serial.available()) {
    chr = Serial.read();
    Serial.print(chr); // so the user can see what they're doing :P
    if (chr == '\n' || chr == '\r') {
      msg += chr; //msg+='\0'; // should maybe terminate my strings properly....
      if (msg.startsWith("/")) {
        // clean up msg string...
        msg.trim(); msg.remove(0, 1);
        // process command...
        char cmd[1]; msg.substring(0, 1).toCharArray(cmd, 2);
        switch (cmd[0]){
          case '?':
            Serial.println("Supported Commands:");
            Serial.println("/? - This message");
            Serial.println("/n Name - Change Tx nickname");
            Serial.println("/d - Display Tx nickname");
            Serial.println("/b - Blink LED");
            break;
          case 'n':
            displayName = msg.substring(2);
            Serial.print("Display name set to: "); Serial.println(displayName);
            break;
          case 'd':
            Serial.print("Your display name is: "); Serial.println(displayName);
            break;
          case 'b':
            Serial.print("Blinking LED"); blinkLED(10);
            break;
          default:
            Serial.println("Command not known. Use '/?' for help.");
        }
        msg = "";
        
      } else {
        Serial.print("Me: "); Serial.println(msg);
        // Assemble message
        sendMsg += displayName;
        sendMsg += "> ";
        sendMsg += msg;
        // send message
        LoRa.beginPacket();
        LoRa.print(sendMsg);
        LoRa.endPacket();
        display.clear();
        display.drawString(1, 0, sendMsg);
        display.display();
        
        // Clear the strings and start again
        msg = "";
        sendMsg = "";
        Serial.print(": ");
      }
    } else {
      msg += chr;
    }
  }
  checkBattery();
  blinkCheck();
}
