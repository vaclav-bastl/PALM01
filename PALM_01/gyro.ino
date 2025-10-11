#include <Adafruit_ADXL345_U.h>

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

#define AVG_BUFFER 16


// Internal filter state
static int16_t ringBuf[3][AVG_BUFFER] = {{0}};
static int32_t runningSum[3] = {0, 0, 0};
static uint8_t head = 0;
static uint8_t filled = 0;

void initAccelerometer() {
  if (!accel.begin()) {
    Serial.println("Ooops, no ADXL345 detected ... Check your wiring!");
    while (1) { delay(10); }
  }
  accel.setRange(ADXL345_RANGE_16_G);
  // Optional: pick a data rate that matches your loop frequency
  // accel.setDataRate(ADXL345_DATARATE_100_HZ);
}

// Read raw register counts just like your original code did.
// This preserves your numeric range exactly.
static inline void readAdxlRaw(int out[3]) {
  out[X_AXIS] = (int)accel.getX();
  out[Y_AXIS] = (int)accel.getY();
  out[Z_AXIS] = (int)accel.getZ();
}

void readAccelerometer() {
  // 1) Latest raw readings (same scale as before)
  readAdxlRaw(accelValue);

  // 2) O(1) moving-average update per axis
  for (uint8_t axis = 0; axis < 3; ++axis) {
    int16_t old = ringBuf[axis][head];
    int16_t now = (int16_t)accelValue[axis];

    ringBuf[axis][head] = now;
    runningSum[axis] += (int32_t)now - (int32_t)old;

    uint8_t denom = (filled < AVG_BUFFER) ? (filled + 1) : AVG_BUFFER;
    accelRunningValue[axis] = (int)(runningSum[axis] / (int32_t)denom);
  }

  // 3) Advance ring head safely
  head = (uint8_t)((head + 1) % AVG_BUFFER);
  if (filled < AVG_BUFFER) filled++;
}

void printAccelValues() {
  Serial.print("X: "); Serial.print(accelValue[X_AXIS]); Serial.print("  ");
  Serial.print("Y: "); Serial.print(accelValue[Y_AXIS]); Serial.print("  ");
  Serial.print("Z: "); Serial.print(accelValue[Z_AXIS]); Serial.print("  ");
  Serial.println("(raw)");
}

void printAvgAccelValues() {
  Serial.print("X: "); Serial.print(accelRunningValue[X_AXIS]); Serial.print("  ");
  Serial.print("Y: "); Serial.print(accelRunningValue[Y_AXIS]); Serial.print("  ");
  Serial.print("Z: "); Serial.print(accelRunningValue[Z_AXIS]); Serial.print("  ");
  Serial.println("(raw avg)");
}
