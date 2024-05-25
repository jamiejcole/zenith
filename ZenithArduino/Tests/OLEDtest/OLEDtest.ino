#include <Encoder.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OSCBoards.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <OSCMatch.h>
#include <OSCMessage.h>
#include <OSCTiming.h>
#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif
#include <string.h>

// oled shit
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// etc shit 
#define NEXT_BTN            6
#define LAST_BTN            7
#define SHIFT_BTN           8

#define SUBSCRIBE           ((int32_t)1)
#define UNSUBSCRIBE         ((int32_t)0)

#define EDGE_DOWN           ((int32_t)1)
#define EDGE_UP             ((int32_t)0)

#define FORWARD             0
#define REVERSE             1

#define PAN_DIR             FORWARD
#define TILT_DIR            FORWARD

// new encoder test shit
Encoder knobPan(2, 3);
Encoder knobTilt(4, 5);
long positionPan  = -999;
long positionTilt = -999;

// Use these values to make the encoder more coarse or fine.
// This controls the number of wheel "ticks" the device sends to the console
// for each tick of the encoder. 1 is the default and the most fine setting.
// Must be an integer.
#define PAN_SCALE           1
#define TILT_SCALE          1

#define SIG_DIGITS          3   // Number of significant digits displayed

#define OSC_BUF_MAX_SIZE    512

const String HANDSHAKE_QUERY = "ETCOSC?";
const String HANDSHAKE_REPLY = "OK";

//See displayScreen() below - limited to 10 chars (after 6 prefix chars)
#define VERSION_STRING      "2.0.0.1"

#define BOX_NAME_STRING     "box1"

// Change these values to alter how long we wait before sending an OSC ping
// to see if Eos is still there, and then finally how long before we
// disconnect and show the splash screen
// Values are in milliseconds
#define PING_AFTER_IDLE_INTERVAL    2500
#define TIMEOUT_AFTER_IDLE_INTERVAL 5000


enum WHEEL_TYPE { TILT, PAN };
enum WHEEL_MODE { COARSE, FINE };


// struct Encoder
// {
//   uint8_t pinA;
//   uint8_t pinB;
//   int pinAPrevious;
//   int pinBPrevious;
//   float pos;
//   uint8_t direction;
// };

// struct Encoder panWheel;
// struct Encoder tiltWheel;

enum ConsoleType
{
  ConsoleNone,
  ConsoleEos,
  ConsoleCobalt,
  ConsoleColorSource
};

bool updateDisplay = false;
ConsoleType connectedToConsole = ConsoleNone;
unsigned long lastMessageRxTime = 0;
bool timeoutPingSent = false;


// OLED SCREEN Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


// LOCAL FUNCTIONS !!!
void updatePanTiltEncs() {
  long newPan, newTilt;
  newPan = knobPan.read();
  newTilt = knobTilt.read();
  if (newPan != positionPan || newTilt != positionTilt) {
    Serial.print("Left = ");
    Serial.print(newPan);
    Serial.print(", Right = ");
    Serial.print(newTilt);
    Serial.println();
    positionPan = newPan;
    positionTilt = newTilt;
  }
}

void issueEosSubscribes()
{
  // Add a filter so we don't get spammed with unwanted OSC messages from Eos
  OSCMessage filter("/eos/filter/add");
  filter.add("/eos/out/param/*");
  filter.add("/eos/out/ping");
  SLIPSerial.beginPacket();
  filter.send(SLIPSerial);
  SLIPSerial.endPacket();

  // subscribe to Eos pan & tilt updates
  OSCMessage subPan("/eos/subscribe/param/pan");
  subPan.add(SUBSCRIBE);
  SLIPSerial.beginPacket();
  subPan.send(SLIPSerial);
  SLIPSerial.endPacket();

  OSCMessage subTilt("/eos/subscribe/param/tilt");
  subTilt.add(SUBSCRIBE);
  SLIPSerial.beginPacket();
  subTilt.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void parseFloatPanUpdate(OSCMessage& msg, int addressOffset)
{
  positionPan = msg.getOSCData(0)->getFloat();
  updateDisplay = true;
}

void parseFloatTiltUpdate(OSCMessage& msg, int addressOffset)
{
  positionTilt = msg.getOSCData(0)->getFloat();
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

  if (!msg.route("/out/param/pan", parseFloatPanUpdate, addressOffset))
    msg.route("/out/param/tilt", parseFloatTiltUpdate, addressOffset);
}

/******************************************************************************/

void parseCobalt(OSCMessage& msg, int addressOffset)
{
  // Cobalt doesn't currently send anything other than ping
  connectedToConsole = ConsoleCobalt;
  updateDisplay = true;
}

void parseColorSource(OSCMessage& msg, int addressOffset)
{
  // ColorSource doesn't currently send anything other than ping
  connectedToConsole = ConsoleColorSource;
  updateDisplay = true;
}

/*******************************************************************************
   Given an unknown OSC message we check to see if it's a handshake message.
   If it's a handshake we issue a subscribe, otherwise we begin route the OSC
   message to the appropriate function.

   Parameters:
    msg - The OSC message of unknown importance

   Return Value: void

 ******************************************************************************/
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
    if (oscmsg.route("/cobalt", parseCobalt))
      return;
    if (oscmsg.route("/cs", parseColorSource))
      return;
  }
}

// TO BE EDITED TO WORK WITH OLED SCREEN

void displayStatus()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 5);

  switch (connectedToConsole)
  {
    case ConsoleNone:
      {
        // display a splash message before the Eos connection is open
        display.println(BOX_NAME_STRING " v" VERSION_STRING);
        display.println("Waiting...");
      } break;

    case ConsoleEos:
      {
        // // put the cursor at the begining of the first line
        // display.setCursor(0, 0);
        // display.print("Pan:  ");
        // display.print(panWheel.pos, SIG_DIGITS);

        // // put the cursor at the begining of the second line
        // display.setCursor(0, 1);
        // display.print("Tilt: ");
        // display.print(tiltWheel.pos, SIG_DIGITS);

        // mine
        display.println((String)"1. Pan :" + positionPan);
        display.println("");
        display.println((String)"2. Tilt:" + positionTilt);
        display.println("");

      } break;

    // who tf cares about these
    case ConsoleCobalt:
      {
      } break;

    case ConsoleColorSource:
      {
      } break;

  }

  updateDisplay = false;
}


// void initEncoder(struct Encoder* encoder, uint8_t pinA, uint8_t pinB, uint8_t direction)
// {
//   encoder->pinA = pinA;
//   encoder->pinB = pinB;
//   encoder->pos = 0;
//   encoder->direction = direction;

//   pinMode(pinA, INPUT_PULLUP);
//   pinMode(pinB, INPUT_PULLUP);

//   encoder->pinAPrevious = digitalRead(pinA);
//   encoder->pinBPrevious = digitalRead(pinB);
// }

// int8_t updateEncoder(struct Encoder* encoder)
// {
//   int8_t encoderMotion = 0;
//   int pinACurrent = digitalRead(encoder->pinA);
//   int pinBCurrent = digitalRead(encoder->pinB);

//   // has the encoder moved at all?
//   if (encoder->pinAPrevious != pinACurrent)
//   {
//     // Since it has moved, we must determine if the encoder has moved forwards or backwards
//     encoderMotion = (encoder->pinAPrevious == encoder->pinBPrevious) ? -1 : 1;

//     // If we are in reverse mode, flip the direction of the encoder motion
//     if (encoder->direction == REVERSE)
//       encoderMotion = -encoderMotion;
//   }
//   encoder->pinAPrevious = pinACurrent;
//   encoder->pinBPrevious = pinBCurrent;

//   return encoderMotion;
// }

// custom update display implementation
// void updateDisplay() {
//   display.clearDisplay();

//   display.setTextSize(1);
//   display.setTextColor(WHITE);
//   display.setCursor(0, 5);
//   // Display static text
//   // display.println("1. Pan :   -000.00");
//   display.println((String)"1. Pan:" + panWheel.pos);
//   display.println("");
//   display.println("2. Tilt:   -000.00");
//   display.println("");
//   display.println("3. Zoom:   -000.00");
//   display.println("");
//   display.println("4. Iris:   -000.00");
//   display.display(); 
// }

/*******************************************************************************
   Sends a message to Eos informing them of a wheel movement.

   Parameters:
    type - the type of wheel that's moving (i.e. pan or tilt)
    ticks - the direction and intensity of the movement

   Return Value: void

 ******************************************************************************/

void sendOscMessage(const String &address, float value)
{
  OSCMessage msg(address.c_str());
  msg.add(value);
  SLIPSerial.beginPacket();
  msg.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void sendEosWheelMove(WHEEL_TYPE type, float ticks)
{
  String wheelMsg("/eos/wheel");

  if (digitalRead(SHIFT_BTN) == LOW)
    wheelMsg.concat("/fine");
  else
    wheelMsg.concat("/coarse");

  if (type == PAN)
    wheelMsg.concat("/pan");
  else if (type == TILT)
    wheelMsg.concat("/tilt");
  else
    // something has gone very wrong
    return;

  sendOscMessage(wheelMsg, ticks);
}

void sendCobaltWheelMove(WHEEL_TYPE type, float ticks)
{
  String wheelMsg("/cobalt/param");

  if (type == PAN)
    wheelMsg.concat("/pan/wheel");
  else if (type == TILT)
    wheelMsg.concat("/tilt/wheel");
  else
    // something has gone very wrong
    return;

  if (digitalRead(SHIFT_BTN) != LOW)
    ticks = ticks * 16;

  sendOscMessage(wheelMsg, ticks);
}

void sendColorSourceWheelMove(WHEEL_TYPE type, float ticks)
{
  String wheelMsg("/cs/param");

  if (type == PAN)
    wheelMsg.concat("/pan/wheel");
  else if (type == TILT)
    wheelMsg.concat("/tilt/wheel");
  else
    // something has gone very wrong
    return;

  if (digitalRead(SHIFT_BTN) != LOW)
    ticks = ticks * 2;

  sendOscMessage(wheelMsg, ticks);
}

/******************************************************************************/

void sendWheelMove(WHEEL_TYPE type, float ticks)
{
  switch (connectedToConsole)
  {
    default:
    case ConsoleEos:
      sendEosWheelMove(type, ticks);
      break;
    case ConsoleCobalt:
      sendCobaltWheelMove(type, ticks);
      break;
    case ConsoleColorSource:
      sendColorSourceWheelMove(type, ticks);
      break;
  }
}

/*******************************************************************************
   Sends a message to the console informing them of a key press.

   Parameters:
    down - whether a key has been pushed down (true) or released (false)
    key - the OSC key name that has moved

   Return Value: void

 ******************************************************************************/
void sendKeyPress(bool down, const String &key)
{
  String keyAddress;
  switch (connectedToConsole)
  {
    default:
    case ConsoleEos:
      keyAddress = "/eos/key/" + key;
      break;
    case ConsoleCobalt:
      keyAddress = "/cobalt/key/" + key;
      break;
    case ConsoleColorSource:
      keyAddress = "/cs/key/" + key;
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

/*******************************************************************************
   Checks the status of all the relevant buttons (i.e. Next & Last)

   NOTE: This does not check the shift key. The shift key is used in tandem with
   the encoder to determine coarse/fine mode and thus does not report directly.

   Parameters: none

   Return Value: void

 ******************************************************************************/

void checkButtons()
{
  // OSC configuration
  const int keyCount = 2;
  const int keyPins[2] = {NEXT_BTN, LAST_BTN};
  const String keyNames[4] = {
    "NEXT", "LAST",
    "soft6", "soft4"
  };

  static int keyStates[2] = {HIGH, HIGH};

  // Eos and Cobalt buttons are the same
  // ColorSource is different
  int firstKey = (connectedToConsole == ConsoleColorSource) ? 2 : 0;

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




// main

void setup() {
  display.clearDisplay();
  Serial.begin(115200);
  delay(1000);
  Serial.println("setup begun");
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

  // initEncoder(&panWheel, A0, A1, PAN_DIR);
  // missing tilt wheel

  // if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
  //   Serial.println(F("SSD1306 allocation failed"));
  //   for(;;);
  // }
  // delay(2000);
  // display.clearDisplay();

  // display.setTextSize(2);
  // display.setTextColor(WHITE);
  // display.setCursor(0, 2);
  // display.println("Launching");
  
  pinMode(NEXT_BTN, INPUT_PULLUP);
  pinMode(LAST_BTN, INPUT_PULLUP);
  pinMode(SHIFT_BTN, INPUT_PULLUP);

  // displayStatus();


// //  old
  
//   Serial.begin(115200);
//   initEncoder(&panWheel, A0, A1, PAN_DIR);

  
//   // Display static text
//   display.println("1. Pan :   -000.00");
//   display.println("");
//   display.println("2. Tilt:   -000.00");
//   display.println("");
//   display.println("3. Zoom:   -000.00");
//   display.println("");
//   display.println("4. Iris:   -000.00");
//   display.display(); 
}

void loop() {
  static String curMsg;
  int size;
  // get the updated state of each encoder
  // int32_t panMotion = updateEncoder(&panWheel);
  // int32_t tiltMotion = updateEncoder(&tiltWheel);
  updatePanTiltEncs();

  // Scale the result by a scaling factor
  positionPan *= PAN_SCALE;
  positionTilt *= TILT_SCALE;

  // check for next/last updates
  checkButtons();

  // now update our wheels
  // if (tiltMotion != 0)
  sendWheelMove(TILT, positionTilt);

  // if (panMotion != 0)
  sendWheelMove(PAN, positionPan);

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

  if (updateDisplay)
    displayStatus();
}





