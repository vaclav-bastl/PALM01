

#define I2C_POWER_PIN D3

const uint8_t buttonPin[NUMBER_OF_BUTTONS] = { D9, D10};  //4, 36, 39, 34 };
void readButtons() {
  for (uint8_t i = 0; i < NUMBER_OF_BUTTONS; i++) {
    lastButtonState[i] = buttonState[i];
    buttonState[i] = !digitalRead(buttonPin[i]);
    justPressed[i] = buttonState[i] && !lastButtonState[i];
    justReleased[i] = !buttonState[i] && lastButtonState[i];

    if (justPressed[i]) Serial.println(i);
  }
}

void wakeUp() {

  pinMode(GPIO_NUM_8, INPUT_PULLUP);  // make sure pull-up is active
 

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    // Give time for voltage to stabilize
    delay(50);  // debounce, helps with false wake

    unsigned long t0 = millis();

    // Wait to see if button is held long enough
    while (digitalRead(D9) == LOW) {
      if (millis() - t0 > 3000) {
        // Long press confirmed
        break;
      }
      delay(10);
    }

    // If button released too early → go back to sleep
    if (millis() - t0 < 3000) {
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_8, 0);  // re-enable wake
      esp_deep_sleep_start();
    }

    // Otherwise continue booting
  }
}
void sleep() {
  Wire.end();
  digitalWrite(I2C_POWER_PIN, LOW);
  pinMode(GPIO_NUM_8, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_8, 0);  // Wake on button press
  esp_deep_sleep_start();                        //longpress to sleep
}

void initHw() {

  for (uint8_t i = 0; i < NUMBER_OF_BUTTONS; i++) pinMode(buttonPin[i], INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(I2C_POWER_PIN, OUTPUT);
  digitalWrite(I2C_POWER_PIN, HIGH);
  delay(10);
  neopixelWrite(NEO_PIXEL_PIN, 10, 0, 0);
  delay(10);
  
  /*
  if (!lis.begin(0x18)) {  // change this to 0x19 for alternative i2c address
    //Serial.println("Couldnt start");
    while (1) yield();
  } else
    ;  // Serial.println("LIS3DH found!");

  lis.setPerformanceMode(LIS3DH_MODE_LOW_POWER);
  lis.setRange(LIS3DH_RANGE_2_G);
  lis.setDataRate(LIS3DH_DATARATE_50_HZ);
*/

  
  delay(100);
  digitalWrite(I2C_POWER_PIN, LOW);
  delay(100);
  digitalWrite(I2C_POWER_PIN, HIGH);
  delay(100);
  Wire.end();
  Wire.begin();  // Reinitialize I2C
  delay(500);    // Short delay for stabilization

  
  
  initTouchSensors();
  initAccelerometer();
}
