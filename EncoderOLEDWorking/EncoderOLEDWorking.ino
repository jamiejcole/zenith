#include <OSCBoards.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <OSCMatch.h>
#include <OSCMessage.h>
#include <OSCTiming.h>

#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"

#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif
#include <string.h>

// consts
#define I2C_ADDRESS 0x3C
#define RST_PIN -1
SSD1306AsciiWire oled;


//buttons
#define enc1ModeButton 2
#define enc2ModeButton 3
#define enc3ModeButton 4
#define goButton 5
#define backButton 6
#define enc1PushButton A0
#define enc2PushButton A1
#define enc3PushButton A2

#define SUBSCRIBE           ((int32_t)1)
#define UNSUBSCRIBE         ((int32_t)0)

#define EDGE_DOWN           ((int32_t)1)
#define EDGE_UP             ((int32_t)0)

#define FORWARD             0
#define REVERSE             1

// Change these values to switch which direction increase/decrease pan/tilt
#define PAN_DIR             FORWARD
#define TILT_DIR            FORWARD
#define ZOOM_DIR            FORWARD

#define PAN_SCALE           2
#define TILT_SCALE          2
#define ZOOM_SCALE          2
 
#define SIG_DIGITS          3   // Number of significant digits displayed

#define OSC_BUF_MAX_SIZE    512

const String HANDSHAKE_QUERY = "ETCOSC?";
const String HANDSHAKE_REPLY = "OK";

#define VERSION_STRING      "1.0"

#define BOX_NAME_STRING     "Zenith MK1"

#define PING_AFTER_IDLE_INTERVAL    2500
#define TIMEOUT_AFTER_IDLE_INTERVAL 5000


// Local types
enum WHEEL_TYPE { TILT, PAN, ZOOM, EDGE, IRIS, GOBO, GOBO2 };
enum WHEEL_MODE { COARSE, FINE };
WHEEL_MODE currentPanMode = COARSE;
WHEEL_MODE currentTiltMode = COARSE;
WHEEL_MODE currentZoomMode = COARSE;

struct Encoder
{
  uint8_t pinA;
  uint8_t pinB;
  int pinAPrevious;
  int pinBPrevious;
  float pos;
  uint8_t direction;
};
struct Encoder panWheel;
struct Encoder tiltWheel;
struct Encoder zoomWheel;
struct Encoder irisWheel;
struct Encoder edgeWheel;

enum ConsoleType
{
  ConsoleNone,
  ConsoleEos,
};

// Global Vairables

bool updateDisplay = false;
bool screenCleared = true;

ConsoleType connectedToConsole = ConsoleNone;
unsigned long lastMessageRxTime = 0;
bool timeoutPingSent = false;

int prevEnc1ModeButtonState = HIGH;
int prevEnc2ModeButtonState = HIGH;
int prevEnc3ModeButtonState = HIGH;

int currentPanOptionIndex = 0;
int currentTiltOptionIndex = 0;
int currentZoomOptionIndex = 0;
const char* panOptions[] = {"Pan", "Iris", "Gobo 2"};
const char* tiltOptions[] = {"Tilt", "Edge", "Gobo"};
const char* zoomOptions[] = {"Zoom", "Clr Whl"};


float gobo_select = 1.000;

//Local Functions
void issueEosSubscribes()
{
  // Add a filter so we don't get spammed with unwanted OSC messages from Eos
  OSCMessage filter("/eos/filter/add");
  filter.add("/eos/out/param/*");
  filter.add("/eos/out/ping");
  SLIPSerial.beginPacket();
  filter.send(SLIPSerial);
  SLIPSerial.endPacket();

  // Parameters to subscribe to
  const char* params[] = {
    "/pan",
    "/Gobo_Select",
    "/tilt",
    "/zoom",
    "/iris",
    "/edge"
  };

  // Loop through the parameters and subscribe to each
  for (const char* param : params) {
    String address = "/eos/subscribe/param" + String(param);
    OSCMessage subscribeMessage(address.c_str()); // Convert the string to a const char*
    subscribeMessage.add(SUBSCRIBE);
    SLIPSerial.beginPacket();
    subscribeMessage.send(SLIPSerial);
    SLIPSerial.endPacket();
  }
}



void parseFloatGoboSelectUpdate(OSCMessage& msg, int addressOffset)
{
  gobo_select = msg.getOSCData(0)->getFloat();
  updateDisplay = true;
}

void parseFloatPanUpdate(OSCMessage& msg, int addressOffset)
{
  panWheel.pos = msg.getOSCData(0)->getFloat();
  updateDisplay = true;
}

void parseFloatTiltUpdate(OSCMessage& msg, int addressOffset)
{
  tiltWheel.pos = msg.getOSCData(0)->getFloat();
  updateDisplay = true;
}

void parseFloatZoomUpdate(OSCMessage& msg, int addressOffset)
{
  zoomWheel.pos = msg.getOSCData(0)->getFloat();
  updateDisplay = true;
}

void parseFloatEdgeUpdate(OSCMessage& msg, int addressOffset)
{
  edgeWheel.pos = msg.getOSCData(0)->getFloat();
  updateDisplay = true;
}

void parseFloatIrisUpdate(OSCMessage& msg, int addressOffset)
{
  irisWheel.pos = msg.getOSCData(0)->getFloat();
  updateDisplay = true;
}

void parseEos(OSCMessage& msg, int addressOffset)
{
  // If we don't think we're connected, reconnect and subscribe
  if (connectedToConsole != ConsoleEos)
  {
    issueEosSubscribes();
    connectedToConsole = ConsoleEos;
    updateDisplay = true;
  }

  msg.route("/out/param/pan", parseFloatPanUpdate, addressOffset);
  msg.route("/out/param/tilt", parseFloatTiltUpdate, addressOffset);
  msg.route("/out/param/Gobo_Select", parseFloatGoboSelectUpdate, addressOffset);
  msg.route("/out/param/zoom", parseFloatZoomUpdate, addressOffset);
  msg.route("/out/param/iris", parseFloatIrisUpdate, addressOffset);
  msg.route("/out/param/edge", parseFloatEdgeUpdate, addressOffset);
}

/******************************************************************************/


void parseOSCMessage(String& msg)
{
  // check to see if this is the handshake string
  if (msg.indexOf(HANDSHAKE_QUERY) != -1)
  {
    // handshake string found!
    SLIPSerial.beginPacket();
    SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
    SLIPSerial.endPacket();

    // An Eos would do nothing until subscribed
    // Let Eos know we want updates on some things
    issueEosSubscribes();

    updateDisplay = true;
  }
  else
  {
    // prepare the message for routing by filling an OSCMessage object with our message string
    OSCMessage oscmsg;
    oscmsg.fill((uint8_t*)msg.c_str(), (int)msg.length());
    // route pan/tilt messages to the relevant update function

    // Try the various OSC routes
    if (oscmsg.route("/eos", parseEos))
      return;
  }
}


// todo - check it works with oled prints
//      - duplicate for other encs
//      - check and scale enc increments by 1x or 0.1x based on state at loop.
// void checkFineCoarseToggles() {
//   static bool defaultState = LOW;
//   bool butt1Pressed = digitalRead(enc1PushButton);
//   if (butt1Pressed != defaultState) {
//     if (butt1Pressed == HIGH) {
//       currentPanMode = (currentPanMode == FINE) ? COARSE : FINE;
//     }
//   }
//   bool butt2Pressed = digitalRead(enc2PushButton);
//   if (butt2Pressed != defaultState) {
//     if (butt2Pressed == HIGH) {
//       currentTiltMode = (currentTiltMode == FINE) ? COARSE : FINE;
//     }
//   }
//   bool butt3Pressed = digitalRead(enc3PushButton);
//   if (butt3Pressed != defaultState) {
//     if (butt3Pressed == HIGH) {
//       currentZoomMode = (currentZoomMode == FINE) ? COARSE : FINE;
//     }
//   }
// }

void checkModeButton(int buttonState, int& prevButtonState, int& currentIndex, const char* options[], int optionCount) {
  if (buttonState == LOW && prevButtonState == HIGH) {
    updateDisplay = true;
    currentIndex = (currentIndex + 1) % optionCount;
    delay(75);
  }
  prevButtonState = buttonState;
}

void checkModeButtons() {
  checkModeButton(digitalRead(enc1ModeButton), prevEnc1ModeButtonState, currentPanOptionIndex, panOptions, sizeof(panOptions) / sizeof(panOptions[0]));
  checkModeButton(digitalRead(enc2ModeButton), prevEnc2ModeButtonState, currentTiltOptionIndex, tiltOptions, sizeof(tiltOptions) / sizeof(tiltOptions[0]));
  checkModeButton(digitalRead(enc3ModeButton), prevEnc3ModeButtonState, currentZoomOptionIndex, zoomOptions, sizeof(zoomOptions) / sizeof(zoomOptions[0]));
}



void displayStatus(const char* mode1, const char* mode2, const char* mode3)
{
  oled.clear();

  switch (connectedToConsole)
  {
    case ConsoleNone:
    {
      oled.println("");
      oled.println("");
      oled.println("    " BOX_NAME_STRING " v" VERSION_STRING);
      oled.println("    Waiting for EOS");
    } break;

    case ConsoleEos:
    {
      Encoder* wheel1 = &panWheel; // Default to pan
      Encoder* wheel2 = &tiltWheel; // Default to tilt
      Encoder* wheel3 = &zoomWheel; // Default to zoom
      int isGobo1 = false;
      int isGobo2 = false;

      if (strcmp(mode1, "Pan") == 0)
        wheel1 = &panWheel;
      else if (strcmp(mode1, "Tilt") == 0)
        wheel1 = &tiltWheel;
      else if (strcmp(mode1, "Zoom") == 0)
        wheel1 = &zoomWheel;
      else if (strcmp(mode1, "Edge") == 0)
        wheel1 = &edgeWheel;
      else if (strcmp(mode1, "Iris") == 0)
        wheel1 = &irisWheel;
      else if (strcmp(mode1, "Gobo") == 0)
        isGobo1 = true;

      if (strcmp(mode2, "Pan") == 0)
        wheel2 = &panWheel;
      else if (strcmp(mode2, "Tilt") == 0)
        wheel2 = &tiltWheel;
      else if (strcmp(mode2, "Zoom") == 0)
        wheel2 = &zoomWheel;
      else if (strcmp(mode2, "Edge") == 0)
        wheel2 = &edgeWheel;
      else if (strcmp(mode2, "Iris") == 0)
        wheel2 = &irisWheel;
      else if (strcmp(mode2, "Gobo") == 0)
        isGobo2 = true;

      if (strcmp(mode3, "Pan") == 0)
        wheel3 = &panWheel;
      else if (strcmp(mode3, "Tilt") == 0)
        wheel3 = &tiltWheel;
      else if (strcmp(mode3, "Zoom") == 0)
        wheel3 = &zoomWheel;
      else if (strcmp(mode3, "Edge") == 0)
        wheel3 = &edgeWheel;
      else if (strcmp(mode3, "Iris") == 0)
        wheel3 = &irisWheel;

      oled.println("");
      oled.println("");
      if (isGobo1)
      {
        oled.println("    1. " + (String)mode1 + ": " + ((isGobo2) ? (int)gobo_select : wheel1->pos));
        oled.println("");
        oled.println("    2. " + (String)mode2 + ": " + ((isGobo1) ? (int)gobo_select : wheel2->pos));
        oled.println("");
        oled.println("    3. " + (String)mode3 + ": " + wheel3->pos);
      }
      else if (isGobo2)
      {
        oled.println("    1. " + (String)mode1 + ": " + wheel1->pos);
        oled.println("");
        oled.println("    2. " + (String)mode2 + ": " + ((isGobo2) ? (int)gobo_select : wheel2->pos));
        oled.println("");
        oled.println("    3. " + (String)mode3 + ": " + wheel3->pos);
      }
      else
      {
        oled.println("    1. " + (String)mode1 + ": " + wheel1->pos);
        oled.println("");
        oled.println("    2. " + (String)mode2 + ": " + wheel2->pos);
        oled.println("");
        oled.println("    3. " + (String)mode3 + ": " + wheel3->pos);
      }
      oled.println("");
    } break;
  }

  updateDisplay = false;
}



void initEncoder(struct Encoder* encoder, uint8_t pinA, uint8_t pinB, uint8_t direction)
{
  encoder->pinA = pinA;
  encoder->pinB = pinB;
  encoder->pos = 0;
  encoder->direction = direction;

  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);

  encoder->pinAPrevious = digitalRead(pinA);
  encoder->pinBPrevious = digitalRead(pinB);
}

int8_t updateEncoder(struct Encoder* encoder)
{
  int8_t encoderMotion = 0;
  int pinACurrent = digitalRead(encoder->pinA);
  int pinBCurrent = digitalRead(encoder->pinB);

  // has the encoder moved at all?
  if (encoder->pinAPrevious != pinACurrent)
  {
    // Since it has moved, we must determine if the encoder has moved forwards or backwards
    encoderMotion = (encoder->pinAPrevious == encoder->pinBPrevious) ? -1 : 1;

    // Adjust motion based on the current mode
    encoderMotion *= 1.5; // Coarse mode adjustment

    // If we are in reverse mode, flip the direction of the encoder motion
    if (encoder->direction == REVERSE)
      encoderMotion = -encoderMotion;
  }
  encoder->pinAPrevious = pinACurrent;
  encoder->pinBPrevious = pinBCurrent;

  return encoderMotion;
}



void sendOscMessage(const String &address, float value)
{
  OSCMessage msg(address.c_str());
  msg.add(value);
  SLIPSerial.beginPacket();
  msg.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void sendEosGoboSelect(float value) {
  value = value/4;
  gobo_select += value;
  if (gobo_select <= 0.5)
    gobo_select = 1;
  // todo: clamp gobo_select to retrieved max value (ensure it's a float!)

  String oscAddress = "/eos/param/Gobo_Select";
  OSCMessage msg(oscAddress.c_str());
  msg.add(gobo_select);
  msg.add(0.500 + gobo_select);
  msg.add(1.500 + gobo_select);
  SLIPSerial.beginPacket();
  msg.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void sendEosWheelMove(WHEEL_TYPE type, float ticks)
{
  String wheelMsg("/eos/wheel");
  wheelMsg.concat("/coarse");

  if (type == PAN)
    wheelMsg.concat("/pan");
  else if (type == TILT)
    wheelMsg.concat("/tilt");
  else if (type == ZOOM)
    wheelMsg.concat("/zoom");
  else if (type == IRIS)
    wheelMsg.concat("/iris");
  else if (type == EDGE)
    wheelMsg.concat("/edge");
  else
    // something has gone very wrong
    return;

  sendOscMessage(wheelMsg, ticks);
}


/******************************************************************************/

void sendWheelMove(const char* name, float ticks) {
  WHEEL_TYPE type;
  if (strcmp(name, "Tilt") == 0)
    type = TILT;
  else if (strcmp(name, "Pan") == 0)
    type = PAN;
  else if (strcmp(name, "Zoom") == 0)
    type = ZOOM;
  else if (strcmp(name, "Iris") == 0)
    type = IRIS;
  else if (strcmp(name, "Edge") == 0)
    type = EDGE;
  else if (strcmp(name, "Gobo") == 0)
    type = GOBO; // Added Gobo type  
  
  switch (connectedToConsole) {
    default:
    case ConsoleEos:
      if (type == GOBO) {
        // Handle Gobo selection differently
        sendEosGoboSelect(ticks);
      } else {
        // Handle other parameters like Pan, Tilt, etc.
        sendEosWheelMove(type, ticks);
      }
      break;
  }
}



void sendKeyPress(bool down, const String &key)
{
  String keyAddress;
  switch (connectedToConsole)
  {
    default:
    case ConsoleEos:
      keyAddress = "/eos/key/" + key;
      break;
  }
  OSCMessage keyMsg(keyAddress.c_str());

  if (down)
    keyMsg.add(EDGE_DOWN);
  else
    keyMsg.add(EDGE_UP);

  SLIPSerial.beginPacket();
  keyMsg.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void checkButtons()
{
  // OSC configuration
  const int keyCount = 2;
  const int keyPins[2] = {goButton, backButton};
  const String keyNames[2] = {
    "go_0", "stop"
  };

  static int keyStates[2] = {HIGH, HIGH};

  // Eos and Cobalt buttons are the same
  // ColorSource is different
  int firstKey = 0;

  // Loop over the buttons
  for (int keyNum = 0; keyNum < keyCount; ++keyNum)
  {
    // Has the button state changed
    if (digitalRead(keyPins[keyNum]) != keyStates[keyNum])
    {
      // Notify console of this key press
      if (keyStates[keyNum] == LOW)
      {
        sendKeyPress(false, keyNames[firstKey + keyNum]);
        keyStates[keyNum] = HIGH;
      }
      else
      {
        sendKeyPress(true, keyNames[firstKey + keyNum]);
        keyStates[keyNum] = LOW;
      }
    }
  }
}

void setup()
{
  pinMode(enc1ModeButton, INPUT_PULLUP);
  pinMode(enc2ModeButton, INPUT_PULLUP);
  pinMode(enc3ModeButton, INPUT_PULLUP);
  pinMode(enc1PushButton, INPUT_PULLUP);
  pinMode(enc2PushButton, INPUT_PULLUP);
  pinMode(enc3PushButton, INPUT_PULLUP);

  pinMode(goButton, INPUT_PULLUP);
  pinMode(backButton, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(400000L);

  #if RST_PIN >= 0
    oled.begin(&Adafruit128x64, I2C_ADDRESS, RST_PIN);
  #else // RST_PIN >= 0
    oled.begin(&Adafruit128x64, I2C_ADDRESS);
  #endif // RST_PIN >= 0

  oled.setFont(System5x7);
  oled.clear();
  oled.println("");
  oled.println("");
  oled.println("    Starting Up...");
  delay(1000);

  SLIPSerial.begin(115200);
  // This is a hack around an Arduino bug. It was taken from the OSC library
  //examples
#ifdef BOARD_HAS_USB_SERIAL
  while (!SerialUSB);
#else
  while (!Serial);
#endif

  // This is necessary for reconnecting a device because it needs some time
  // for the serial port to open. The handshake message may have been sent
  // from the console before #lighthack was ready

  SLIPSerial.beginPacket();
  SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
  SLIPSerial.endPacket();

  // If it's an Eos, request updates on some things
  issueEosSubscribes();

  initEncoder(&panWheel, 12, 13, PAN_DIR);
  initEncoder(&tiltWheel, 11, 10, TILT_DIR);
  initEncoder(&zoomWheel, 9, 8, ZOOM_DIR);

  displayStatus(panOptions[currentPanOptionIndex], tiltOptions[currentTiltOptionIndex], zoomOptions[currentZoomOptionIndex]);
}


void loop()
{
  // checkFineCoarseToggles();
  checkModeButtons();
  static String curMsg;
  int size;
  // get the updated state of each encoder
  int32_t panMotion = updateEncoder(&panWheel);
  int32_t tiltMotion = updateEncoder(&tiltWheel);
  int32_t zoomMotion = updateEncoder(&zoomWheel);

  // check for next/last updates
  checkButtons();

  // now update our wheels
  // float tiltScaleFactor = (currentTiltMode == FINE) ? 0.1 : 1.0;
  // float panScaleFactor = (currentPanMode == FINE) ? 0.1 : 1.0;
  // float zoomScaleFactor = (currentZoomMode == FINE) ? 0.1 : 1.0;

  if (tiltMotion != 0)
    sendWheelMove(tiltOptions[currentTiltOptionIndex], tiltMotion);

  if (panMotion != 0)
    sendWheelMove(panOptions[currentPanOptionIndex], panMotion);

  if (zoomMotion != 0)
    sendWheelMove(zoomOptions[currentZoomOptionIndex], zoomMotion);

  // Then we check to see if any OSC commands have come from Eos
  // and update the display accordingly.
  size = SLIPSerial.available();
  if (size > 0)
  {
    // Fill the msg with all of the available bytes
    while (size--)
      curMsg += (char)(SLIPSerial.read());
  }
  if (SLIPSerial.endofPacket())
  {
    parseOSCMessage(curMsg);
    lastMessageRxTime = millis();
    // We only care about the ping if we haven't heard recently
    // Clear flag when we get any traffic
    timeoutPingSent = false;
    curMsg = String();
  }

  if (lastMessageRxTime > 0)
  {
    unsigned long diff = millis() - lastMessageRxTime;
    //We first check if it's been too long and we need to time out
    if (diff > TIMEOUT_AFTER_IDLE_INTERVAL)
    {
      connectedToConsole = ConsoleNone;
      lastMessageRxTime = 0;
      updateDisplay = true;
      timeoutPingSent = false;
    }

    //It could be the console is sitting idle. Send a ping once to
    // double check that it's still there, but only once after 2.5s have passed
    if (!timeoutPingSent && diff > PING_AFTER_IDLE_INTERVAL)
    {
      OSCMessage ping("/eos/ping");
      ping.add(BOX_NAME_STRING "_hello"); // This way we know who is sending the ping
      SLIPSerial.beginPacket();
      ping.send(SLIPSerial);
      SLIPSerial.endPacket();
      timeoutPingSent = true;
    }
  }

  if (updateDisplay) {
    if (screenCleared) {
      // Clear the screen only if it's not already cleared
      oled.clear();
      screenCleared = true;
    }
    displayStatus(panOptions[currentPanOptionIndex], tiltOptions[currentTiltOptionIndex], zoomOptions[currentZoomOptionIndex]);  // Display the updated status
    screenCleared = false;
    updateDisplay = false;
  }
}