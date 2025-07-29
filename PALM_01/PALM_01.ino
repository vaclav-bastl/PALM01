/*

Credits:
Developed by Václav Peloušek @toyotavangelis of Bastl Instruments at Pifcamp 2025
https://bastl-instruments.com
https://pif.camp

BLE MIDI aid from Rein Gundersen Bentdal who makes this cool instrument https://wavyindustries.com/monkey/

Contributions from pifcamp members:

Full doumentation is here:
https://docs.google.com/document/d/1CUI6_zo0RurEk8GgtcJpuo6qGxklACaIrTYTMEp8r8M/edit?pli=1&tab=t.0

GitHub repository here:
https://github.com/vaclav-bastl/PALM01/

Licece TBD

*/


#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"
#include <Adafruit_ADXL345_U.h>

const char* BLE_DEVICE_NAME = "PALM_BLE";  // Name shown to host devices


#define NEO_PIXEL_PIN D2
// === CONFIGURATION SECTION ===
#define USE_BLE_MIDI
#define USE_SERIAL_MIDI
//#define USE_USB_MIDI
#define USE_DEBUG
const uint8_t MIDI_CHANNEL = 1;  // Default channel (1-16)


#define ACCEL_HYSTERESIS 3
int accelValue[3];
int accelRunningValue[3];
int lastAccelRunningValue[3];
uint8_t outputCCValue[127];
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2

uint8_t rgb[3];
uint8_t currentPreset = 0;
uint8_t lastPreset = 0;

bool bleConnected = false;

#define NUMBER_OF_TOUCHPOINTS 11
bool touchState[NUMBER_OF_TOUCHPOINTS];
bool lastTouchState[NUMBER_OF_TOUCHPOINTS];
bool justTouched[NUMBER_OF_TOUCHPOINTS];
bool justUntouched[NUMBER_OF_TOUCHPOINTS];
uint8_t runningTouchValue[NUMBER_OF_TOUCHPOINTS];

#define NUMBER_OF_BUTTONS 2
bool buttonState[NUMBER_OF_BUTTONS];
bool lastButtonState[NUMBER_OF_BUTTONS];
bool justPressed[NUMBER_OF_BUTTONS];
bool justReleased[NUMBER_OF_BUTTONS];



uint8_t activeIndex = 0;
uint8_t lastActiveIndex;
uint8_t activeNote, lastActiveNote;
uint8_t touchThreshold = 120;


uint8_t note = 255;
uint8_t highestPointPressed;
uint8_t newNote = 255;
uint8_t octave;
long longPressTime = 0;

#define NUMBER_OF_BYTES_IN_PRESET 6
#define NUMBER_OF_PRESETS 4
#define NUMBER_OF_FINGERS 8

bool leftHand = true;  //set left/right priority for thumb points
/*
these are the numbers in preset that can be edited:
number 255 is typically used as disable

1. CC of touch point itself
2. 0-127 would set the variable pressure output maximum, 255 is only ON/OFF without continuous pressure
3. wrist rotation CC
4. number wrist rotation CC resets to on context point release 0-127 or 255 to not reset and let the CC hang
5. elbow rotation CC
6. number elbow rotation CC resets to on context point release 0-127 or 255 to not reset and let the CC hang

4*8=32

*/

#define INDEX_A 6
#define INDEX_B 7
#define MIDDLE_A 4
#define MIDDLE_B 5
#define RING_A 2
#define RING_B 3
#define LITTLE_A 0
#define LITTLE_B 1

#define THUMB_L 8
#define THUMB_M 9
#define THUMB_R 10


#define BUTTON_R 0
#define BUTTON_L 1

#define WAKE_BUTTON_PIN 9


#define TOUCH_CC 0
#define TOUCH_CC_RANGE 1
#define WRIST_CC 2
#define WRIST_RESET 3
#define ELBOW_CC 4
#define ELBOW_RESET 5

uint8_t preset[NUMBER_OF_FINGERS][NUMBER_OF_PRESETS][NUMBER_OF_BYTES_IN_PRESET] = {
  //LITTLE A
  { { 8, 255, 40, 127, 73, 0 },
    { 16, 255, 48, 127, 81, 0 },
    { 24, 255, 56, 127, 89, 0 },
    { 32, 255, 65, 127, 97, 0 } },
  //LITTLE B
  { { 7, 255, 39, 127, 72, 0 },
    { 15, 255, 47, 127, 80, 0 },
    { 23, 255, 55, 127, 88, 0 },
    { 31, 255, 63, 127, 96, 0 } },

  //RING A
  { { 6, 255, 38, 127, 71, 0 },
    { 14, 255, 46, 127, 79, 0 },
    { 22, 255, 54, 127, 87, 0 },
    { 30, 255, 62, 127, 95, 0 } },
  //RING B
  { { 5, 255, 37, 127, 70, 0 },
    { 13, 255, 45, 127, 78, 0 },
    { 21, 255, 53, 127, 86, 0 },
    { 29, 255, 61, 127, 94, 0 } },

  //MIDDLE A
  { { 4, 127, 36, 127, 255, 0 },
    { 12, 127, 44, 127, 77, 0 },
    { 20, 255, 52, 127, 85, 0 },
    { 28, 255, 60, 127, 93, 0 } },
  //MIDDLE B
  { { 3, 255, 35, 127, 255, 0 },
    { 11, 255, 43, 127, 76, 0 },
    { 19, 255, 51, 127, 84, 0 },
    { 27, 255, 59, 127, 92, 0 } },

  //INDEX A
  { { 2, 255, 34, 127, 255, 0 },
    { 10, 255, 42, 127, 75, 0 },
    { 18, 255, 50, 127, 83, 0 },
    { 26, 255, 58, 127, 91, 0 } },
  //INDEX B
  { { 1, 255, 33, 127, 255, 0 },
    { 9, 255, 41, 127, 74, 0 },
    { 17, 255, 49, 127, 82, 0 },
    { 25, 255, 57, 127, 90, 0 } }
};



void onControlChange(uint8_t channel, uint8_t controller, uint8_t value, uint16_t timestamp) {
  rgb[controller % 3] = value;
  // Serial.printf("Received control change : channel %d, controller %d, value %d (timestamp %dms)\n", channel, controller, value, timestamp);
}



void readSensors() {
  readTouchSensors();
  readButtons();
  readAccelerometer();
}


uint8_t mappingMode;
void handleButtons() {
  if (bleConnected) {
    if (justPressed[BUTTON_L]) {
      mappingMode++;
      if (mappingMode > 3) mappingMode = 0;
    }
  }
  if (justPressed[BUTTON_R]) {
    adverstiseBle();
    longPressTime = millis();
    //esp_sleep_enable_timer_wakeup(10000000);  // 1 sec
  }
  if (buttonState[BUTTON_R] && ((millis() - longPressTime)) > 3000) {
    sleep();
  }
}



void setup() {
  wakeUp();
  Serial.begin(115200);
  delay(500);
  Serial.println("start");

  initHw();
  initMidi();
}
#define NOT_MAPPING 0
#define MAP_WRIST 1
#define MAP_ELBOW 2
#define MAP_TOUCH 3

void switchPreset() {                                //this is a bruteforce method meaning more midi trafic on preset change but also more robust
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; i++) {  //do it for all finger points 8 of them

    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      if (preset[i][lastPreset][TOUCH_CC_RANGE] == 255) {  //ON OFF for finger touch
        if (outputCCValue[preset[i][lastPreset][TOUCH_CC]] != 0) sendControlChange(preset[i][lastPreset][TOUCH_CC], 0, MIDI_CHANNEL);
      } else {  //continuous pressure per point
        if (outputCCValue[preset[i][lastPreset][TOUCH_CC]] != 0) sendControlChange(preset[i][lastPreset][TOUCH_CC], 0, MIDI_CHANNEL);
      }
    }

    if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
      if (preset[i][lastPreset][WRIST_CC] != 255) {
        if (outputCCValue[preset[i][lastPreset][WRIST_CC]] != preset[i][lastPreset][WRIST_RESET]) sendControlChange(preset[i][lastPreset][WRIST_CC], preset[i][lastPreset][WRIST_RESET], MIDI_CHANNEL);
      }
    }

    if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
      if (preset[i][lastPreset][ELBOW_CC] != 255) {
        if (outputCCValue[preset[i][lastPreset][ELBOW_CC]] != preset[i][lastPreset][ELBOW_RESET]) sendControlChange(preset[i][lastPreset][ELBOW_CC], preset[i][lastPreset][ELBOW_RESET], MIDI_CHANNEL);
      }
    }
    
    //the buffer might overflow with too much info so dispatch midi after each finger
    midiDispatch();
    delay(1);
  }
}


void handleContext() {

  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; i++) {  //do it for all finger points 8 of them

    // send touch information
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      if (preset[i][currentPreset][TOUCH_CC_RANGE] == 255) {  //ON OFF for finger touch
        if (justTouched[i]) {
          sendControlChange(preset[i][currentPreset][TOUCH_CC], 127, MIDI_CHANNEL);
        }
        if (justUntouched[i]) {
          sendControlChange(preset[i][currentPreset][TOUCH_CC], 0, MIDI_CHANNEL);
        }
      } else {                //continuous pressure per point
        if (touchState[i]) {  //
          uint8_t newCCvalue = runningTouchValue[i];
          if (newCCvalue != outputCCValue[preset[i][currentPreset][TOUCH_CC]]) {  // send only if it changed
            sendControlChange(preset[i][currentPreset][TOUCH_CC], newCCvalue, MIDI_CHANNEL);
          }
        }
        if (justUntouched[i]) {  //reset when released
          sendControlChange(preset[i][currentPreset][TOUCH_CC], 0, MIDI_CHANNEL);
        }
      }
    }


    if (touchState[i]) {                                             //send gyro
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {  //send only when being mapped or outside of mapping
        if (preset[i][currentPreset][WRIST_CC] != 255) {
          uint8_t newCCvalue = constrain(stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0, lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS), 0, 127);
          if (newCCvalue != outputCCValue[preset[i][currentPreset][WRIST_CC]]) {  // send only if it changed
            sendControlChange(preset[i][currentPreset][WRIST_CC], newCCvalue, MIDI_CHANNEL);
          }
        }
      }

      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t newCCvalue = constrain(stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0, lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS), 0, 127);
        if (newCCvalue != outputCCValue[preset[i][currentPreset][ELBOW_CC]]) {  // send only if it changed
          sendControlChange(preset[i][currentPreset][ELBOW_CC], newCCvalue, MIDI_CHANNEL);
        }
      }
    }

    if (justUntouched[i]) { //reset gyro values on release of the touch point
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        if (preset[i][currentPreset][WRIST_CC] != 255) {
          sendControlChange(preset[i][currentPreset][WRIST_CC], preset[i][currentPreset][WRIST_RESET], MIDI_CHANNEL);
        }
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        if (preset[i][currentPreset][ELBOW_CC] != 255) {
          sendControlChange(preset[i][currentPreset][ELBOW_CC], preset[i][currentPreset][ELBOW_RESET], MIDI_CHANNEL);
        }
      }
    }
    //the buffer might overflow with too much info so dispatch midi after each finger
    midiDispatch();
    delay(1);
  }
}


void handleThumb() {

  lastPreset = currentPreset;
  currentPreset = 0;
  if (leftHand) {
    if (touchState[THUMB_L]) currentPreset = 1;
    if (touchState[THUMB_M]) currentPreset = 2;
    if (touchState[THUMB_R]) currentPreset = 3;
  } else {
    if (touchState[THUMB_R]) currentPreset = 1;
    if (touchState[THUMB_M]) currentPreset = 2;
    if (touchState[THUMB_L]) currentPreset = 3;
  }

  if (lastPreset != currentPreset) switchPreset();
}


void loop() {

  readSensors();
  //

  //printTouchedSensors();
  //printTouchSensors();
  //printAvgAccelValues();
  handleButtons();

  handleThumb();
  handleContext();

  //handleIncomingMessages();

  midiRead();
  //testMidi();
  //midiDispatch();

  // Serial.println(2);
  //  delay(2);

  if (bleConnected) {
    if (mappingMode == MAP_WRIST) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 0);
    else if (mappingMode == MAP_ELBOW) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 10);
    else if (mappingMode == MAP_TOUCH) neopixelWrite(NEO_PIXEL_PIN, 20, 10, 10);
    else if (mappingMode == NOT_MAPPING) neopixelWrite(NEO_PIXEL_PIN, 0, 0, 5);
  } else {
    //adverstiseBle();
    neopixelWrite(NEO_PIXEL_PIN, 10, 0, 0);
  }
}
