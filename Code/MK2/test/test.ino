#include <Arduino.h>
#include <Wire.h>
#include "Bounce2mcp.h"
#include "Adafruit_MCP23017.h"
#include "Rotary.h"
#include "RotaryEncOverMCP.h"
#include <string.h>

#define NUM_BUTTONS 5
#define MCP_PIN_MX1_SHIFT 13
#define MCP_PIN_MX2_GO 14
#define MCP_PIN_MX3_STOP 15
#define MCP_PIN_PB1_SW 12
#define MCP_PIN_PB1_LED 11
#define MCP_PIN_PB2_SW 10
#define MCP_PIN_PB2_LED 9

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
  {"pan", "tilt", "zoom", "intensity"}, // page 1
  {"red", "green", "blue", "white"} // page 2
};

int currentPage = 0;


const String HANDSHAKE_QUERY = "ETCOSC?";
const String HANDSHAKE_REPLY "OK";

BounceMcp * buttons = new BounceMcp[NUM_BUTTONS];

/* Our I2C MCP23017 GPIO expanders */
Adafruit_MCP23017 mcp;

//Array of pointers of all MCPs if there is more than one
Adafruit_MCP23017* allMCPs[] = { &mcp };
constexpr int numMCPs = (int)(sizeof(allMCPs) / sizeof(*allMCPs));

/* function prototypes */
void RotaryEncoderChanged(bool clockwise, int id);

/* Array of all rotary encoders and their pins */
RotaryEncOverMCP rotaryEncoders[] = {
    // outputA,B on GPA7,GPA6, register with callback and ID=1
    RotaryEncOverMCP(&mcp, 6, 7, &RotaryEncoderChanged, 1),
    RotaryEncOverMCP(&mcp, 4, 5, &RotaryEncoderChanged, 2),
    RotaryEncOverMCP(&mcp, 2, 3, &RotaryEncoderChanged, 3),
    RotaryEncOverMCP(&mcp, 0, 1, &RotaryEncoderChanged, 4),
};
constexpr int numEncoders = (int)(sizeof(rotaryEncoders) / sizeof(*rotaryEncoders));

void RotaryEncoderChanged(bool clockwise, int id) {
  Serial.println("Encoder " + String(id) + ": "
          + (clockwise ? String("clockwise") : String("counter-clock-wise")));
}

void startupBlink();
void waitingForConnectionBlink();

void startupBlink() {
  for (int i = 0; i < 5; i++) {
    mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
    mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
    delay(100);
    mcp.digitalWrite(MCP_PIN_PB1_LED, LOW);
    mcp.digitalWrite(MCP_PIN_PB2_LED, LOW);
    delay(100);
  }
}

void waitingForConnectionBlink() {
  mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
  delay(400);
  mcp.digitalWrite(MCP_PIN_PB1_LED, LOW);
  delay(200);
  mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
  delay(400);
  mcp.digitalWrite(MCP_PIN_PB2_LED, LOW);
  delay(200);
}

void setup(){
  delay(500); // Give time for bootup to finish
  
  Serial.begin(115200);
  Serial.flush();
  Serial.println("--- Zenith MK2 ---");

  Wire.begin();
  Wire.setClock(400000);

  mcp.begin();      // use default address 0

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

  startupBlink();
}

void pollAll() {
  for(int j = 0; j < numMCPs; j++) {
      uint16_t gpioAB = allMCPs[j]->readGPIOAB();
      for (int i=0; i < numEncoders; i++) {
          //only feed this in the encoder if this
          //is coming from the correct MCP
          if(rotaryEncoders[i].getMCP() == allMCPs[j])
              rotaryEncoders[i].feedInput(gpioAB);
      }
  }
}

void sendEncoderMovement(int page, int encoder, float ticks) {
  // we want to send a string e.g. /eos/wheel/coarse/pan
}

void loop() {
    pollAll();

    for (int i = 0; i < NUM_BUTTONS; i++) {
      buttons[i].update();
    }

    // not shift
    for (int i = 0; i < NUM_BUTTONS; i++) {
      if (i > 0) {
        if (buttons[i].fell()) {
          if (!buttons[0].read())
            Serial.print("SHIFT + ");
          Serial.print("Button ");
          Serial.print(i+1);
          Serial.println(" was pushed.");
          if (i == 3) 
            mcp.digitalWrite(MCP_PIN_PB1_LED, HIGH);
          if (i == 4)
            mcp.digitalWrite(MCP_PIN_PB2_LED, HIGH);
        }
      }
    }
}