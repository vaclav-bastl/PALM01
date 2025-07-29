Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

#define AVG_BUFFER 16
void initAccelerometer() {
  if (!accel.begin()) {
    /* There was a problem detecting the ADXL345 ... check your connections */
    Serial.println("Ooops, no ADXL345 detected ... Check your wiring!");
    while (1)
      ;
  }
  accel.setRange(ADXL345_RANGE_16_G);
}
int accelRunningBuffer[3][AVG_BUFFER];
int runningCounter;


void readAccelerometer() {
  accelValue[X_AXIS] = accel.getX();
  accelValue[Y_AXIS] = accel.getY();
  accelValue[Z_AXIS] = accel.getZ();
  if (runningCounter < AVG_BUFFER) runningCounter++;
  else runningCounter = 0;

  for (uint i = 0; i < 3; i++) {
    accelRunningBuffer[i][runningCounter] = accelValue[i];
    int _sum = 0;
    for (uint8_t j = 0; j < 16; j++) {
      _sum += accelRunningBuffer[i][j];
    }
   // lastAccelRunningValue[i] = accelRunningValue[i];
    accelRunningValue[i] = _sum / AVG_BUFFER;
  }
}
void printAccelValues() {

  Serial.print("X: ");
  Serial.print(accelValue[X_AXIS]);
  Serial.print("  ");
  Serial.print("Y: ");
  Serial.print(accelValue[Y_AXIS]);
  Serial.print("  ");
  Serial.print("Z: ");
  Serial.print(accelValue[Z_AXIS]);
  Serial.print("  ");
  Serial.println("m/s^2 ");
}

void printAvgAccelValues() {

  Serial.print("X: ");
  Serial.print(accelRunningValue[X_AXIS]);
  Serial.print("  ");
  Serial.print("Y: ");
  Serial.print(accelRunningValue[Y_AXIS]);
  Serial.print("  ");
  Serial.print("Z: ");
  Serial.print(accelRunningValue[Z_AXIS]);
  Serial.print("  ");
  Serial.println("m/s^2 ");
}