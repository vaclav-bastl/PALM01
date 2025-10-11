#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <esp_wifi.h>

// ------------------- CONFIG -------------------
#define LOG_VERBOSE 0            // 0 = quiet, 1 = more logs

#define NUMBER_OF_BUTTONS 2
#define I2C_POWER_PIN     D3
const uint8_t buttonPin[NUMBER_OF_BUTTONS] = { D9, D10 };
#define WAKE_GPIO         GPIO_NUM_8

#define NEOPIXEL_PIN      21     // change if needed
#define NEOPIXEL_COUNT    1
#define LED_BRIGHTNESS    16

const uint8_t CHN[3] = {6,1,11}; // cycle order

// ------------------- GLOBALS -------------------
Adafruit_NeoPixel pix(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
bool pixelReady=false;

bool buttonState[NUMBER_OF_BUTTONS]={0};
bool lastButtonState[NUMBER_OF_BUTTONS]={0};

const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);
Preferences prefs;

volatile uint8_t connectedStations = 0;
uint8_t channelIndex = 0;
uint32_t bootMs = 0;

// minimal preset storage (so UI works)
static const uint8_t NUMBER_OF_FINGERS=8, NUMBER_OF_PRESETS=4, NUMBER_OF_BYTES_IN_PRESET=6;
enum PresetField: uint8_t { TOUCH_CC=0, TOUCH_CC_RANGE, WRIST_CC, WRIST_RESET, ELBOW_CC, ELBOW_RESET };
enum PresetGlobalField: uint8_t { G_WRIST_CC=0, G_WRIST_RESET, G_ELBOW_CC, G_ELBOW_RESET };
uint8_t preset[NUMBER_OF_FINGERS][NUMBER_OF_PRESETS][NUMBER_OF_BYTES_IN_PRESET];
uint8_t presetGlobal[NUMBER_OF_PRESETS][4];

struct ApSettings { String ssid; String pass; uint8_t channel; } apCfg;

// ------------------- LOG -------------------
#if LOG_VERBOSE
  #define LOG(...)  do{ Serial.printf(__VA_ARGS__); }while(0)
#else
  #define LOG(...)  do{}while(0)
#endif

// ------------------- LED / POWER -------------------
inline void ledShow(uint8_t r,uint8_t g,uint8_t b){ if(!pixelReady) return; pix.setPixelColor(0,pix.Color(r,g,b)); pix.show(); }
inline void ledRed(){    ledShow(16,0,0); }
inline void ledGreen(){  ledShow(0,16,0); }
inline void ledYellow(){ ledShow(16,16,0); }
inline void ledOff(){    ledShow(0,0,0); }

void powerPeripherals(bool on){
  pinMode(I2C_POWER_PIN, OUTPUT);
  if(on){
    digitalWrite(I2C_POWER_PIN, HIGH);
    delay(50);
    Wire.end(); Wire.begin();
    pix.begin(); pix.setBrightness(LED_BRIGHTNESS);
    pixelReady=true;
    ledRed();
  }else{
    ledOff();
    pixelReady=false;
    Wire.end(); delay(5);
    digitalWrite(I2C_POWER_PIN, LOW);
  }
}

// ------------------- BUTTONS / SLEEP -------------------
void readButtons(){
  for(uint8_t i=0;i<NUMBER_OF_BUTTONS;i++){
    pinMode(buttonPin[i], INPUT_PULLUP);
    lastButtonState[i]=buttonState[i];
    buttonState[i]=!digitalRead(buttonPin[i]);
  }
}

void wakeUp(){
  pinMode(WAKE_GPIO, INPUT_PULLUP);
  if (esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0){
    delay(80);
    uint32_t t0=millis();
    while (digitalRead(buttonPin[0])==LOW){
      if (millis()-t0>3000) break;
      delay(10);
    }
    if (millis()-t0<3000){
      esp_sleep_enable_ext0_wakeup(WAKE_GPIO,0);
      esp_deep_sleep_start();
    }
  }
}

void sleep_now(){
  ledYellow(); delay(150);
  powerPeripherals(false);
  pinMode(WAKE_GPIO, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(WAKE_GPIO,0);
  esp_deep_sleep_start();
}

// ------------------- PRESET DEFAULTS / NVS -------------------
void setDefaults(){
  for(uint8_t p=0;p<NUMBER_OF_PRESETS;p++){
    presetGlobal[p][G_WRIST_CC]=255; presetGlobal[p][G_WRIST_RESET]=0;
    presetGlobal[p][G_ELBOW_CC]=255; presetGlobal[p][G_ELBOW_RESET]=0;
  }
  for(uint8_t f=0;f<NUMBER_OF_FINGERS;f++)
    for(uint8_t p=0;p<NUMBER_OF_PRESETS;p++){
      preset[f][p][TOUCH_CC]=255; preset[f][p][TOUCH_CC_RANGE]=255;
      preset[f][p][WRIST_CC]=255; preset[f][p][WRIST_RESET]=0;
      preset[f][p][ELBOW_CC]=255; preset[f][p][ELBOW_RESET]=0;
    }
  preset[0][0][TOUCH_CC]=8; preset[1][0][TOUCH_CC]=7;

  apCfg.ssid="PALM01-Setup";
  apCfg.pass="";               // OPEN default
  apCfg.channel=6;
  channelIndex=0;
}

bool loadConfigFromNVS(){
  prefs.begin("palm01", true);
  String json=prefs.getString("config","");
  apCfg.ssid   =prefs.getString("ap_ssid","PALM01-Setup");
  apCfg.pass   =prefs.getString("ap_pass","");
  apCfg.channel=prefs.getUChar ("ap_chan",6);
  prefs.end();
  if (apCfg.channel<1||apCfg.channel>13) apCfg.channel=6;
  if (!json.length()) return false;

  DynamicJsonDocument doc(64*1024);
  if (deserializeJson(doc,json)) return false;
  if (!doc.containsKey("presetGlobal")||!doc.containsKey("preset")) return false;

  JsonArray pg=doc["presetGlobal"].as<JsonArray>(); if(pg.size()!=NUMBER_OF_PRESETS) return false;
  for(uint8_t p=0;p<NUMBER_OF_PRESETS;p++){
    JsonArray r=pg[p].as<JsonArray>(); if(r.size()!=4) return false;
    for(uint8_t j=0;j<4;j++){ int v=r[j].as<int>(); if(!((v>=0&&v<=127)||v==255)) return false; presetGlobal[p][j]=(uint8_t)v; }
  }
  JsonArray pr=doc["preset"].as<JsonArray>(); if(pr.size()!=NUMBER_OF_FINGERS) return false;
  for(uint8_t f=0;f<NUMBER_OF_FINGERS;f++){
    JsonArray rf=pr[f].as<JsonArray>(); if(rf.size()!=NUMBER_OF_PRESETS) return false;
    for(uint8_t p=0;p<NUMBER_OF_PRESETS;p++){
      JsonArray rp=rf[p].as<JsonArray>(); if(rp.size()!=NUMBER_OF_BYTES_IN_PRESET) return false;
      for(uint8_t k=0;k<NUMBER_OF_BYTES_IN_PRESET;k++){
        int v=rp[k].as<int>(); if(!((v>=0&&v<=127)||v==255)) return false; preset[f][p][k]=(uint8_t)v;
      }
    }
  }
  if (apCfg.channel==6) channelIndex=0; else if(apCfg.channel==1) channelIndex=1; else if(apCfg.channel==11) channelIndex=2; else channelIndex=0;
  return true;
}

bool saveConfigToNVS(const String& json,const String* s=nullptr,const String* p=nullptr,const uint8_t* c=nullptr){
  DynamicJsonDocument doc(64*1024);
  if (deserializeJson(doc,json)) return false;
  if (!doc.containsKey("presetGlobal")||!doc.containsKey("preset")) return false;

  uint8_t pgTmp[NUMBER_OF_PRESETS][4];
  uint8_t prTmp[NUMBER_OF_FINGERS][NUMBER_OF_PRESETS][NUMBER_OF_BYTES_IN_PRESET];
  JsonArray pg=doc["presetGlobal"].as<JsonArray>(); if(pg.size()!=NUMBER_OF_PRESETS) return false;
  for(uint8_t i=0;i<NUMBER_OF_PRESETS;i++){
    JsonArray r=pg[i].as<JsonArray>(); if(r.size()!=4) return false;
    for(uint8_t j=0;j<4;j++){ int v=r[j].as<int>(); if(!((v>=0&&v<=127)||v==255)) return false; pgTmp[i][j]=(uint8_t)v; }
  }
  JsonArray pr=doc["preset"].as<JsonArray>(); if(pr.size()!=NUMBER_OF_FINGERS) return false;
  for(uint8_t f=0;f<NUMBER_OF_FINGERS;f++){
    JsonArray rf=pr[f].as<JsonArray>(); if(rf.size()!=NUMBER_OF_PRESETS) return false;
    for(uint8_t p2=0;p2<NUMBER_OF_PRESETS;p2++){
      JsonArray rp=rf[p2].as<JsonArray>(); if(rp.size()!=NUMBER_OF_BYTES_IN_PRESET) return false;
      for(uint8_t k=0;k<NUMBER_OF_BYTES_IN_PRESET;k++){
        int v=rp[k].as<int>(); if(!((v>=0&&v<=127)||v==255)) return false; prTmp[f][p2][k]=(uint8_t)v;
      }
    }
  }

  prefs.begin("palm01", false);
  bool ok1=prefs.putString("config",json)>0, ok2=true;
  if (s){ ok2&=prefs.putString("ap_ssid",*s)>0; apCfg.ssid=*s; }
  if (p){ ok2&=prefs.putString("ap_pass",*p)>0; apCfg.pass=*p; }
  if (c){ ok2&=prefs.putUChar ("ap_chan",*c)>0; apCfg.channel=*c; }
  prefs.end();
  if (!(ok1&&ok2)) return false;
  memcpy(presetGlobal,pgTmp,sizeof(presetGlobal));
  memcpy(preset,prTmp,sizeof(preset));
  if (apCfg.channel==6) channelIndex=0; else if(apCfg.channel==1) channelIndex=1; else if(apCfg.channel==11) channelIndex=2; else channelIndex=0;
  return true;
}

String currentConfigAsJson(){
  DynamicJsonDocument doc(64*1024);
  JsonArray pg=doc.createNestedArray("presetGlobal");
  for(uint8_t p=0;p<NUMBER_OF_PRESETS;p++){ JsonArray r=pg.createNestedArray(); for(uint8_t j=0;j<4;j++) r.add(presetGlobal[p][j]); }
  JsonArray pr=doc.createNestedArray("preset");
  for(uint8_t f=0;f<NUMBER_OF_FINGERS;f++){ JsonArray rf=pr.createNestedArray();
    for(uint8_t p=0;p<NUMBER_OF_PRESETS;p++){ JsonArray rp=rf.createNestedArray(); for(uint8_t k=0;k<NUMBER_OF_BYTES_IN_PRESET;k++) rp.add(preset[f][p][k]); }
  }
  JsonObject wifi=doc.createNestedObject("ap");
  wifi["ssid"]=apCfg.ssid; wifi["pass"]=apCfg.pass; wifi["channel"]=apCfg.channel;
  String out; serializeJson(doc,out); return out;
}

// ------------------- AP BRING-UP (Arduino) -------------------
void wifiRadioCleanReset(){
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF); delay(60);
  WiFi.disconnect(true,true); delay(120);
  esp_wifi_stop(); esp_wifi_deinit(); // extra clean
}

void startAP_simple(){
  wifiRadioCleanReset();

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  wifi_country_t country={}; memcpy(country.cc,"CZ",2);
  country.schan=1; country.nchan=13; country.policy=WIFI_COUNTRY_POLICY_MANUAL;
  esp_wifi_set_country(&country);
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

  bool useWPA2 = apCfg.pass.length()>=8;
  if (!useWPA2) apCfg.pass="";
  bool ok = WiFi.softAP(apCfg.ssid.c_str(), useWPA2? apCfg.pass.c_str():"", apCfg.channel, false, 8);

  connectedStations = WiFi.softAPgetStationNum();
  ledRed();
  if (!LOG_VERBOSE) { Serial.printf("AP: '%s' ch=%u %s -> %s\n", apCfg.ssid.c_str(), apCfg.channel, useWPA2?"WPA2":"OPEN", ok?"OK":"FAIL"); }
}

// ------------------- DNS / HTTP -------------------
void startDNS(){
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError); // always “exists” -> redirects to us
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}

void sendNoCache(){ server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0"); server.sendHeader("Pragma","no-cache"); server.sendHeader("Expires","0"); }
void sendCORS(){ server.sendHeader("Access-Control-Allow-Origin","*"); server.sendHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS"); server.sendHeader("Access-Control-Allow-Headers","Content-Type"); }
void sendConnClose(){ server.sendHeader("Connection","close"); }
void sendNoCacheCORS(){ sendNoCache(); sendCORS(); sendConnClose(); }

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>PALM_01 Config</title>
<style>
body{font-family:system-ui,Roboto,Arial,sans-serif;margin:0;background:#0b0e14;color:#e6e6e6}
header{padding:12px 16px;background:#161b22;border-bottom:1px solid #232a34}
main{padding:16px;max-width:900px;margin:0 auto}
textarea{width:100%;height:48vh;background:#0f131a;color:#e6e6e6;border:1px solid #2a3441;border-radius:8px;padding:10px;font-family:ui-monospace,monospace}
input{background:#0f131a;color:#e6e6e6;border:1px solid #2a3441;border-radius:8px;padding:8px}
label{display:block;margin-top:10px;margin-bottom:4px}
button{padding:10px 14px;margin-right:10px;border:0;border-radius:8px;background:#2a7bff;color:white;cursor:pointer}
button.sec{background:#3a444f}
.notice{font-size:13px;color:#aab3bf;margin-top:8px}
.card{background:#10151d;border:1px solid #222b37;border-radius:12px;padding:14px;margin-top:16px}
.row{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
</style>
<header><b>PALM_01</b> — Preset Configurator</header>
<main>
<div class="card">
  <div class="row">
    <div><label>AP SSID</label><input id="ssid" placeholder="PALM01-Setup"></div>
    <div><label>AP Password (≥8 = WPA2)</label><input id="pass" placeholder=""></div>
    <div><label>Channel (1,6,11)</label><input id="chan" type="number" min="1" max="13" value="6"></div>
  </div>
</div>
<textarea id="editor" spellcheck="false"></textarea>
<div style="margin-top:12px">
  <button id="btnLoad" class="sec">Load</button>
  <button id="btnSave">Save</button>
  <button id="btnSleep" class="sec">Sleep & Apply</button>
  <button id="btnRestartAP" class="sec">Restart AP</button>
  <button id="btnReboot" class="sec">Reboot</button>
  <button id="btnFlash" class="sec">Flash LED</button>
</div>
<div id="msg" class="notice"></div>
<script>
const ed=document.getElementById('editor'), msg=document.getElementById('msg');
const ssid=document.getElementById('ssid'), pass=document.getElementById('pass'), chan=document.getElementById('chan');
async function loadCfg(){ msg.textContent='Loading...';
  try{ const r=await fetch('/api/get-config'); const t=await r.text(); ed.value=t; const j=JSON.parse(t);
    ssid.value=j.ap?.ssid??''; pass.value=j.ap?.pass??''; chan.value=j.ap?.channel??6; msg.textContent='Loaded.'; }
  catch(e){ msg.textContent='Load error: '+e; } }
async function saveCfg(){ msg.textContent='Saving...';
  try{ JSON.parse(ed.value); }catch(e){ msg.textContent='Invalid JSON: '+e; return; }
  try{ const body=JSON.stringify(Object.assign(JSON.parse(ed.value),{ap:{ssid:ssid.value,pass:pass.value,channel:Number(chan.value)||6}}));
    const r=await fetch('/api/set-config',{method:'POST',headers:{'Content-Type':'application/json'},body});
    if(!r.ok) throw new Error('HTTP '+r.status); msg.textContent='Saved. If Wi-Fi changed, use Sleep & Apply.'; }
  catch(e){ msg.textContent='Save error: '+e; } }
async function sleepApply(){ msg.textContent='Sleeping...'; try{ await fetch('/api/sleep'); }catch(e){} }
async function restartAP(){ msg.textContent='Restarting AP...'; try{ await fetch('/api/restart-ap'); }catch(e){} }
async function reboot(){ msg.textContent='Rebooting...'; try{ await fetch('/api/reboot'); }catch(e){} }
async function flash(){ msg.textContent='Flashing LED...'; try{ await fetch('/api/flash-led'); }catch(e){} }
document.getElementById('btnLoad').onclick=loadCfg;
document.getElementById('btnSave').onclick=saveCfg;
document.getElementById('btnSleep').onclick=sleepApply;
document.getElementById('btnRestartAP').onclick=restartAP;
document.getElementById('btnReboot').onclick=reboot;
document.getElementById('btnFlash').onclick=flash;
loadCfg();
</script>
)HTML";

void handleRoot(){ sendNoCacheCORS(); server.send(200,"text/html", INDEX_HTML); }
void handleProbe(){ // captive helpers -> force open portal
  sendNoCacheCORS();
  server.send(200,"text/html","<meta http-equiv='refresh' content='0;url=/'/>");
}
void handleGetConfig(){ sendNoCacheCORS(); server.send(200,"application/json", currentConfigAsJson()); }
void handleSetConfig(){
  sendNoCacheCORS();
  if (!server.hasArg("plain")) { server.send(400,"text/plain","Missing body"); return; }
  String body=server.arg("plain");
  DynamicJsonDocument doc(64*1024);
  if (deserializeJson(doc, body)) { server.send(400,"text/plain","Bad JSON"); return; }
  String s=apCfg.ssid, p=apCfg.pass; int c=apCfg.channel;
  if (doc.containsKey("ap")){
    JsonObject a=doc["ap"].as<JsonObject>();
    s=String(a["ssid"]|apCfg.ssid);
    p=String(a["pass"]|apCfg.pass);
    c=int(a["channel"]|apCfg.channel);
    if (c<1||c>13) c=6;
  }
  uint8_t uc=(uint8_t)c;
  if (!saveConfigToNVS(body,&s,&p,&uc)) { server.send(400,"text/plain","Invalid config"); return; }
  server.send(200,"text/plain","OK");
}
void handleSleep(){ sendNoCacheCORS(); server.send(200,"text/plain","Sleeping"); delay(120); sleep_now(); }
void handleRestartAP(){
  sendNoCacheCORS(); server.send(200,"text/plain","Restarting AP");
  delay(80);
  channelIndex=(channelIndex+1)%3;
  apCfg.channel=CHN[channelIndex];
  startAP_simple();
}
void handleReboot(){ sendNoCacheCORS(); server.send(200,"text/plain","Rebooting"); delay(60); ESP.restart(); }
void handleFlashLed(){ sendNoCacheCORS(); server.send(200,"text/plain","OK"); if(!pixelReady) powerPeripherals(true); ledShow(0,0,16); delay(120); ledRed(); }

void startHTTP(){
  server.on("/", HTTP_GET, handleRoot);
  // Captive probes (always 200 + redirect to /)
  server.on("/generate_204", HTTP_GET, handleProbe);     // Android
  server.on("/gen_204", HTTP_GET, handleProbe);          // Android alt
  server.on("/hotspot-detect.html", HTTP_GET, handleProbe); // iOS/macOS
  server.on("/library/test/success.html", HTTP_GET, handleProbe); // Apple
  server.on("/ncsi.txt", HTTP_GET, handleProbe);         // Windows
  server.on("/fwlink", HTTP_GET, handleProbe);           // Windows captive
  server.on("/favicon.ico", HTTP_GET, handleProbe);

  server.on("/api/get-config", HTTP_GET, handleGetConfig);
  server.on("/api/set-config", HTTP_POST, handleSetConfig);
  server.on("/api/sleep", HTTP_GET, handleSleep);
  server.on("/api/restart-ap", HTTP_GET, handleRestartAP);
  server.on("/api/reboot", HTTP_GET, handleReboot);
  server.on("/api/flash-led", HTTP_GET, handleFlashLed);

  server.onNotFound(handleProbe);
  server.begin();
}

// ------------------- WATCHDOG -------------------
void apWatchdog(){
  static uint32_t lastTry=0;
  uint32_t now=millis();

  // Fast cycle for first 20 s (every 5 s), then slow every 60 s
  bool earlyPhase = (now - bootMs) < 20000;
  uint32_t interval = earlyPhase ? 5000UL : 60000UL;

  if (connectedStations==0 && now - lastTry > interval){
    lastTry=now;
    LOG("AP watchdog cycling channel\n");
    handleRestartAP(); // cycles channel & restarts
  }
}

// ------------------- EVENTS -------------------
void onWiFiEvent(WiFiEvent_t event){
  switch(event){
    case ARDUINO_EVENT_WIFI_AP_START:
      connectedStations = WiFi.softAPgetStationNum();
      ledRed();
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      connectedStations = WiFi.softAPgetStationNum();
      ledGreen();
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      connectedStations = WiFi.softAPgetStationNum();
      if (connectedStations==0) ledRed();
      break;
    default: break;
  }
}

// ------------------- SETUP / LOOP -------------------
void setup(){
  Serial.begin(115200);
  delay(150);

  bootMs = millis();
  wakeUp();
  powerPeripherals(true);
  delay(150);

  setDefaults();
  if (loadConfigFromNVS()) { if(!LOG_VERBOSE) Serial.println("cfg:NVS"); }
  else                     { if(!LOG_VERBOSE) Serial.println("cfg:DEF"); }

  WiFi.onEvent(onWiFiEvent);
  startAP_simple();

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT,"*",WiFi.softAPIP());
  startHTTP();

  if(!LOG_VERBOSE) { Serial.print("AP IP: "); Serial.println(WiFi.softAPIP()); Serial.println("Portal ready."); }
}

void loop(){
  dnsServer.processNextRequest();
  server.handleClient();
  apWatchdog();

  readButtons();

  // both buttons -> deep sleep (Sleep & Apply)
  if (buttonState[0] && buttonState[1]){
    if(!LOG_VERBOSE) Serial.println("Sleep…");
    delay(120); sleep_now();
  }

  // button 0 long press (~2 s) -> reboot
  static uint32_t b0At=0;
  if (buttonState[0] && !lastButtonState[0]) b0At=millis();
  if (!buttonState[0] && lastButtonState[0]) b0At=0;
  if (buttonState[0] && b0At && millis()-b0At>2000){
    if(!LOG_VERBOSE) Serial.println("Reboot…");
    b0At=0; ESP.restart();
  }
}
