#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <OSCBoards.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <OSCMatch.h>
#include <OSCMessage.h>
#include <OSCTiming.h>
#include "Bounce2mcp.h"
#include "Adafruit_MCP23017.h"
#include "Rotary.h"
#include "RotaryEncOverMCP.h"

#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif

#define NUM_BUTTONS 5
#define MCP_PIN_MX1_SHIFT 13
#define MCP_PIN_MX2_GO 14
#define MCP_PIN_MX3_STOP 15
#define MCP_PIN_PB1_SW 10
#define MCP_PIN_PB1_LED 9
#define MCP_PIN_PB2_SW 12
#define MCP_PIN_PB2_LED 11

#define OSC_BUF_MAX_SIZE 512
#define PING_AFTER_IDLE_INTERVAL 2500
#define TIMEOUT_AFTER_IDLE_INTERVAL 5000

const uint8_t BUTTON_PINS[NUM_BUTTONS] = {
  MCP_PIN_MX1_SHIFT,
  MCP_PIN_MX2_GO,
  MCP_PIN_MX3_STOP,
  MCP_PIN_PB1_SW,
  MCP_PIN_PB2_SW
};

const char* encoderFunctions[][4] = {
  {"pan", "tilt", "zoom", "intens"}, // page 1
  {"red", "green", "blue", "white"} // page 2
};

int currentPage = 0;
const int TICK_AMOUNT = 2;

enum ConsoleType
{
  ConsoleNone,
  ConsoleEos,
};
const String HANDSHAKE_QUERY = "ETCOSC?";
const String HANDSHAKE_REPLY = "OK";

ConsoleType connectedToConsole = ConsoleNone;
unsigned long lastMessageRxTime = 0;
bool timeoutPingSent = false;

BounceMcp * buttons = new BounceMcp[NUM_BUTTONS];
Adafruit_MCP23017 mcp;

/* function prototypes */
void RotaryEncoderChanged(bool clockwise, int id);
void sendOscMessage(const String &address, float value);
void startupBlink();
void waitingForConnectionBlink();
void readyBlink();
void parseOscMessage(String &msg);
void issueEosSubscribes();
void activatePage(int page);
void sendKeyPress(const String &key);

RotaryEncOverMCP rotaryEncoders[] = {
    RotaryEncOverMCP(&mcp, 6, 7, &RotaryEncoderChanged, 1),
    RotaryEncOverMCP(&mcp, 4, 5, &RotaryEncoderChanged, 2),
    RotaryEncOverMCP(&mcp, 2, 3, &RotaryEncoderChanged, 3),
    RotaryEncOverMCP(&mcp, 0, 1, &RotaryEncoderChanged, 4),
};
constexpr int numEncoders = (int)(sizeof(rotaryEncoders) / sizeof(*rotaryEncoders));

void RotaryEncoderChanged(bool clockwise, int id) {
  Serial.println(
    "Encoder " + String(id) + ": "
    + (clockwise ? String("clockwise") : String("counter-clock-wise"))
  );
  sendEncoderMovement(currentPage, id - 1, (clockwise ? -TICK_AMOUNT : TICK_AMOUNT));
}

void readyBlink() {
  mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
  mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
  delay(300);
  mcp.digitalWrite(MCP_PIN_PB1_LED, LOW);
  mcp.digitalWrite(MCP_PIN_PB2_LED, LOW);
  delay(100);
  mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
  mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
  delay(300);
  mcp.digitalWrite(MCP_PIN_PB1_LED, LOW);
  mcp.digitalWrite(MCP_PIN_PB2_LED, LOW);
}

void startupBlink() {
  mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
  mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
  delay(1000);
  mcp.digitalWrite(MCP_PIN_PB1_LED, LOW);
  mcp.digitalWrite(MCP_PIN_PB2_LED, LOW);
}

void waitingForConnectionBlink() {
  mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
  delay(400);
  mcp.digitalWrite(MCP_PIN_PB1_LED, LOW);
  delay(200);
  mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
  delay(400);
  mcp.digitalWrite(MCP_PIN_PB2_LED, LOW);
}

void pollAll() {
  uint16_t gpioAB = mcp.readGPIOAB();
  for (int i = 0; i < numEncoders; i++) {
    rotaryEncoders[i].feedInput(gpioAB);
  }
}

void sendOscMessage(const String &address, float value) {
  OSCMessage msg(address.c_str());
  msg.add(value);
  SLIPSerial.beginPacket();
  msg.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void sendEncoderMovement(int page, int encoder, float ticks) {
  // we want to send a string e.g. /eos/wheel/coarse/pan
  float sentTicks = ticks;
  String wheelMsg("/eos/wheel");
  wheelMsg.concat("/coarse/");
  wheelMsg.concat(encoderFunctions[page][encoder]);
  if (page == 0 && encoder == 3) // double ticks for intensity
    sentTicks = 5;
  sendOscMessage(wheelMsg, sentTicks);
}

void issueEosSubscribes() {
  // Adding a filter to only listen for pings
  OSCMessage filter("/eos/filter/add");
  // filter.add("/eos/out/param/*");
  filter.add("/eos/out/ping");

  SLIPSerial.beginPacket();
  filter.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void parseOscMessage(String &msg) {
  // If it's a handshake string, reply
  if (msg.indexOf(HANDSHAKE_QUERY) != -1) {
    SLIPSerial.beginPacket();
    SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
    SLIPSerial.endPacket();

    // Subscribe to ping message
    issueEosSubscribes();
  }
}

void activatePage(int page) {
  if (page == 0) {
    mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
    mcp.digitalWrite(MCP_PIN_PB2_LED, LOW);
    currentPage = 0;
  }
  else if (page == 1) {
    mcp.digitalWrite(MCP_PIN_PB1_LED, LOW);
    mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
    currentPage = 1;
  }
  else {
    return;
  }
}

void sendKeyPress(const String &key) {
  String msg = "/eos/key/" + key;
  OSCMessage keyMsg(msg.c_str());
  SLIPSerial.beginPacket();
  keyMsg.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void setup(){
  delay(500); // Give time for bootup to finish

  Wire.begin();
  Wire.setClock(400000);

  mcp.begin();

  //Initialize input encoders (pin mode, interrupt)
  for(int i=0; i < numEncoders; i++) {
      rotaryEncoders[i].init();
  }

  // Initialize buttons
  for (int i = 0; i < NUM_BUTTONS; i++) {
    mcp.pinMode(BUTTON_PINS[i], INPUT);
    mcp.pullUp(BUTTON_PINS[i], HIGH);
    buttons[i].attach(mcp, BUTTON_PINS[i], 2);
  }
  mcp.pinMode(MCP_PIN_PB1_LED, OUTPUT);
  mcp.pinMode(MCP_PIN_PB2_LED, OUTPUT);

  // Ready! 
  startupBlink();

  // OSC Library hack to avoid Arduino bug
  SLIPSerial.begin(115200);
  #ifdef BOARD_HAS_USB_SERIAL
    while (!SerialUSB);
  #else
    while (!Serial);
  #endif

  // Needed for device reconnection
  SLIPSerial.beginPacket();
  SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
  SLIPSerial.endPacket();

  issueEosSubscribes();

  delay(500);
  readyBlink();

  delay(100);
  activatePage(0);
}

void loop() {
  pollAll();

  // Checking if OSC commands have come from EOS
  static String curMsg;
  int size;
  size = SLIPSerial.available();
  if (size > 0)
  {
    while (size--)
      curMsg += (char)(SLIPSerial.read());
  }
  if (SLIPSerial.endofPacket())
  {
    parseOscMessage(curMsg);
    lastMessageRxTime = millis();
    // We only care about the ping if we haven't heard recently
    // Clear flag when we get any traffic
    timeoutPingSent = false;
    curMsg = String();
  }

  // Checking for timeouts
  if (lastMessageRxTime > 0)
  {
    unsigned long diff = millis() - lastMessageRxTime;
    if (diff > TIMEOUT_AFTER_IDLE_INTERVAL)
    {
      connectedToConsole = ConsoleNone;
      lastMessageRxTime = 0;
      timeoutPingSent = false;
    }

    // Send a ping after 2.5s
    if (!timeoutPingSent && diff > PING_AFTER_IDLE_INTERVAL)
    {
      OSCMessage ping("/eos/ping");
      ping.add("ZenithMK2_hello");
      SLIPSerial.beginPacket();
      ping.send(SLIPSerial);
      SLIPSerial.endPacket();
      timeoutPingSent = true;
    }
  }

  // -----------------------------
  // Buttons
  // -----------------------------
  for (int i = 0; i < NUM_BUTTONS; i++) {
    buttons[i].update();
  }

  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (i > 0) {
      if (buttons[i].fell()) {
        if (!buttons[0].read())
          Serial.print("SHIFT + ");
        Serial.print("Button ");
        Serial.print(i+1);
        Serial.println(" was pushed.");
        if (i == 1)
          sendKeyPress("go_0");
        if (i == 2)
          sendKeyPress("stop");
        if (i == 3) // PB1
          activatePage(0);
        if (i == 4) // PB2
          activatePage(1);
      }
    }
  }
}