// ESP32-S3 robust SoftAP bring-up using esp_wifi (Arduino core 3.x)
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_err.h>

static void chk(const char* what, esp_err_t e) {
  Serial.printf("%s -> %s (0x%X)\n", what, esp_err_to_name(e), (unsigned)e);
}

void print_state() {
  wifi_mode_t m;
  if (esp_wifi_get_mode(&m) == ESP_OK) Serial.printf("mode=%d (2=AP,3=AP+STA)\n", (int)m);
  wifi_country_t c;
  if (esp_wifi_get_country(&c) == ESP_OK) {
    Serial.printf("country: %.2s  schan=%d  nchan=%d  policy=%d\n", c.cc, c.schan, c.nchan, c.policy);
  }
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
  Serial.printf("Tx power enum: %d\n", (int)WiFi.getTxPower());
}

bool start_softap(const char* ssid, const char* pass, uint8_t channel, wifi_auth_mode_t auth) {
  // Clean slate
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.disconnect(true, true);
  delay(100);

  // Init Wi-Fi in AP mode
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Country: Czech Republic (channels 1..13), manual policy
  wifi_country_t country = {};
  memcpy(country.cc, "CZ", 2);
  country.schan  = 1;
  country.nchan  = 13; // count, not upper bound
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  chk("esp_wifi_set_country", esp_wifi_set_country(&country));

  // Protocols: enable legacy 11b + 11g + 11n for beacon compatibility
  chk("esp_wifi_set_protocol",
      esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));

  // Build AP config
  wifi_config_t cfg = {};
  strncpy((char*)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid)-1);
  cfg.ap.ssid_len       = strlen(ssid);
  cfg.ap.channel        = channel;         // try 1, 6, or 11
  cfg.ap.ssid_hidden    = 0;               // visible
  cfg.ap.max_connection = 8;
  cfg.ap.beacon_interval= 100;             // ms
  cfg.ap.pmf_cfg.required = false;         // PMF off
  cfg.ap.authmode       = auth;            // WIFI_AUTH_OPEN or WPA2/WPA3
  if (auth != WIFI_AUTH_OPEN) {
    strncpy((char*)cfg.ap.password, pass, sizeof(cfg.ap.password)-1);
  }

  chk("esp_wifi_set_mode(AP)", esp_wifi_set_mode(WIFI_MODE_AP));
  chk("esp_wifi_set_config(AP)", esp_wifi_set_config(WIFI_IF_AP, &cfg));
  chk("esp_wifi_start", esp_wifi_start());

  print_state();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nESP32-S3 SoftAP (IDF) bring-up");

  // 1) Try OPEN AP on ch6
  //if (start_softap("PALM01-Setup", "", 6, WIFI_AUTH_OPEN)) {
  //  Serial.println("Started OPEN AP on ch6; check for SSID.");
 // }

  // If still invisible, try WPA2 on ch1:
  // delay(4000);
   start_softap("PALM01-Setup", "palm01cfg", 11, WIFI_AUTH_WPA2_PSK);
}

void loop() {}
