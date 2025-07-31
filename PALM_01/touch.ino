// You can have up to 4 on one i2c bus but one is enough for testing!
Adafruit_MPR121 cap = Adafruit_MPR121();
uint16_t currtouched = 0;

#define MPR_TOUCH_THR 6
#define MPR_RELEASE_THR 3
#define TOUCH_THR 50
#define THUMBR_THR 50000
uint8_t touchPin[16] = { 1, 0, 3, 2, 4, 5, 7, 6, 11, 10, 9 };
long touchMinimum[NUMBER_OF_TOUCHPOINTS] = { 736, 733, 739, 725, 736, 716, 716, 723, 727, 721, 718 };
long touchMaximum[NUMBER_OF_TOUCHPOINTS] = { 227, 224, 191, 200, 193, 181, 200, 190, 164, 237, 189 };
long touchMultiMaximum[NUMBER_OF_TOUCHPOINTS] = { 40, 25, 43, 33, 39, 40, 115, 78, 39, 34, 37 };

static constexpr size_t Map_Pressure_Curve_Length = 6;
const long Map_Pressure_Curve[Map_Pressure_Curve_Length * 2] = { 0, 50, 70, 85, 95, 100,
                                                                 0, 20, 40, 60, 80, 100 };
long rawTouchValue[NUMBER_OF_TOUCHPOINTS];
long touchValue[NUMBER_OF_TOUCHPOINTS];
long calibratedTouchValue[NUMBER_OF_TOUCHPOINTS];
bool touched[NUMBER_OF_TOUCHPOINTS];
uint8_t numberOfTouchedPoints = 0;

#define RUNNING_TOUCH_SIZE 8
uint8_t runningTouchBuffer[NUMBER_OF_TOUCHPOINTS][RUNNING_TOUCH_SIZE];
uint8_t lastRunningTouchValue[NUMBER_OF_TOUCHPOINTS];
uint8_t runningTouchCounter = 0;
void initTouchSensors() {
  Serial.println("Adafruit MPR121 Capacitive Touch sensor test");

  // Default address is 0x5A, if tied to 3.3V its 0x5B
  // If tied to SDA its 0x5C and if SCL then 0x5D
  if (!cap.begin(0x5A)) {
    Serial.println("MPR121 not found, check wiring?");
    while (1)
      ;
  }
  Serial.println("MPR121 found!");
  cap.setAutoconfig(true);
  cap.setThreshholds(MPR_TOUCH_THR, MPR_RELEASE_THR);
}



void readTouchSensors() {

  currtouched = cap.touched();

  numberOfTouchedPoints = 0;
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {  //NUMBER_OF_TOUCHPOINTS
    //read touch values
    rawTouchValue[i] = cap.filteredData(touchPin[i]);
    touchValue[i] = constrain(map(rawTouchValue[i], touchMinimum[i], touchMaximum[i], 0, 100), 0, 255);
    //check if points are touched
    touched[i] = (touchValue[i] > TOUCH_THR || currtouched & _BV(touchPin[i]));
    //check how many are touched
    if (touched[i]) numberOfTouchedPoints++;
  }
  //do thumb
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    //if more than 1 are touched the curve change dramatically so recalculate it
    if (numberOfTouchedPoints > 1) touchValue[i] = constrain(map(rawTouchValue[i], touchMinimum[i], touchMultiMaximum[i], 0, 100), 0, 255);
    //apply pressure curve for natural response
    calibratedTouchValue[i] = curve_map(touchValue[i], Map_Pressure_Curve, Map_Pressure_Curve_Length);
  }


  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    lastTouchState[i] = touchState[i];
    touchState[i] = touched[i];
    justTouched[i] = touchState[i] && !lastTouchState[i];
    justUntouched[i] = !touchState[i] && lastTouchState[i];
  }

  runningTouchCounter++;
  if (runningTouchCounter > RUNNING_TOUCH_SIZE) runningTouchCounter = 0;

  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    runningTouchBuffer[i][runningTouchCounter] = calibratedTouchValue[i];
    long _sum = 0;
    for (uint8_t j = 0; j < RUNNING_TOUCH_SIZE; j++) {
      _sum += runningTouchBuffer[i][j];
    }
    lastRunningTouchValue[i] = runningTouchValue[i];
    runningTouchValue[i] = _sum / RUNNING_TOUCH_SIZE;
  }
}
void printTouchedSensors() {
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++)
    if (justTouched[i]) Serial.println(i);
}

void printTouchSensors() {
  //Serial.println(numberOfTouchedPoints);
  Serial.print("raw:    ");
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    Serial.print(rawTouchValue[i]);
    Serial.print("\t");
  }
  Serial.println();


  Serial.print("touched: ");
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    Serial.print(touched[i]);
    Serial.print("\t");
  }
  Serial.println();

  Serial.print("scaled: ");
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {

    Serial.print(touchValue[i]);
    Serial.print("\t");
  }
  Serial.println();

  Serial.print("curved: ");

  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    Serial.print(calibratedTouchValue[i]);
    Serial.print("\t");
  }
  Serial.println();

  Serial.print("running: ");

  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    Serial.print(runningTouchValue[i]);
    Serial.print("\t");
  }
  Serial.println();

  Serial.println();
}