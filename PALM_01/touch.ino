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

void dump_regs() {
  Serial.println("========================================");
  Serial.println("CHAN 00 01 02 03 04 05 06 07 08 09 10 11");
  Serial.println("     -- -- -- -- -- -- -- -- -- -- -- --");
  // CDC
  Serial.print("CDC: ");
  for (int chan = 0; chan < 12; chan++) {
    uint8_t reg = cap.readRegister8(0x5F + chan);
    if (reg < 10) Serial.print(" ");
    Serial.print(reg);
    Serial.print(" ");
  }
  Serial.println();
  // CDT
  Serial.print("CDT: ");
  for (int chan = 0; chan < 6; chan++) {
    uint8_t reg = cap.readRegister8(0x6C + chan);
    uint8_t cdtx = reg & 0b111;
    uint8_t cdty = (reg >> 4) & 0b111;
    if (cdtx < 10) Serial.print(" ");
    Serial.print(cdtx);
    Serial.print(" ");
    if (cdty < 10) Serial.print(" ");
    Serial.print(cdty);
    Serial.print(" ");
  }
  Serial.println();
  Serial.println("========================================");
}



void initTouchSensors() {
  Serial.println("Adafruit MPR121 Capacitive Touch sensor test");

  // Default address is 0x5A, if tied to 3.3V its 0x5B
  // If tied to SDA its 0x5C and if SCL then 0x5D
  if (!cap.begin(0x5A)) {
    Serial.println("MPR121 not found, check wiring?");
    while (1)
      ;
  }
  delay(100);
  Serial.println("MPR121 found!");
  Serial.println("Initial CDC/CDT values:");
  dump_regs();

  cap.setAutoconfig(true);
  cap.setThreshholds(MPR_TOUCH_THR, MPR_RELEASE_THR);

  dump_regs();
}



void readTouchSensors() {
  currtouched = cap.touched();

  // 1) Raw reads + initial touch detection
  uint8_t preCount = 0;
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    rawTouchValue[i] = cap.filteredData(touchPin[i]);
    touchValue[i] = constrain(map(rawTouchValue[i], touchMinimum[i], touchMaximum[i], 0, 100), 0, 255);

    // initial raw touch flag (before pair arbitration)
    bool rawHit = (touchValue[i] > TOUCH_THR) || (currtouched & _BV(touchPin[i]));
    touched[i] = rawHit;
    if (rawHit) preCount++;
  }

  // 2) Pressure curve + multi-touch remap when >1
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    long tv = touchValue[i];
    if (preCount > 1) {
      tv = constrain(map(rawTouchValue[i], touchMinimum[i], touchMultiMaximum[i], 0, 100), 0, 255);
    }
    calibratedTouchValue[i] = curve_map(tv, Map_Pressure_Curve, Map_Pressure_Curve_Length);
  }

  // 3) Pair arbitration (mutual exclusivity per finger pair)
  for (uint8_t k = 0; k < PAIR_COUNT; ++k) {
    uint8_t a = pairA[k];
    uint8_t b = pairB[k];

    bool aTouched = touched[a];
    bool bTouched = touched[b];

    if (aTouched && bTouched) {
      int aVal = (int)calibratedTouchValue[a];
      int bVal = (int)calibratedTouchValue[b];
      int diff = aVal - bVal;

      // default winner by handedness if tie: leftHand -> A, rightHand -> B
      int8_t winner = leftHand ? 0 : 1;

      // keep latch unless the other exceeds by PAIR_HYST
      if (pairLatch[k] == 0 && (bVal <= aVal + PAIR_HYST)) {
        winner = 0; // keep A
      } else if (pairLatch[k] == 1 && (aVal <= bVal + PAIR_HYST)) {
        winner = 1; // keep B
      } else {
        // no valid latch or clearly outmatched -> choose stronger if beyond hysteresis
        if (diff > PAIR_HYST)      winner = 0; // A clearly stronger
        else if (diff < -PAIR_HYST) winner = 1; // B clearly stronger
        // else keep handedness default winner
      }

      // Apply mutual exclusivity
      touched[a] = (winner == 0);
      touched[b] = (winner == 1);
      pairLatch[k] = winner;
    } else if (aTouched) {
      // only A touched
      touched[a] = true;
      touched[b] = false;
      pairLatch[k] = 0;
    } else if (bTouched) {
      // only B touched
      touched[a] = false;
      touched[b] = true;
      pairLatch[k] = 1;
    } else {
      // none touched; clear latch
      touched[a] = false;
      touched[b] = false;
      pairLatch[k] = -1;
    }
  }

  // 4) Recompute count AFTER arbitration
  numberOfTouchedPoints = 0;
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    if (touched[i]) numberOfTouchedPoints++;
  }

  // 5) Edge detection (justTouched / justUntouched)
  for (uint8_t i = 0; i < NUMBER_OF_TOUCHPOINTS; i++) {
    lastTouchState[i] = touchState[i];
    touchState[i] = touched[i];
    justTouched[i] =  touchState[i] && !lastTouchState[i];
    justUntouched[i] = !touchState[i] &&  lastTouchState[i];
  }

  // 6) Running average (fix off-by-one on the counter)
  runningTouchCounter++;
  if (runningTouchCounter >= RUNNING_TOUCH_SIZE) runningTouchCounter = 0;

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