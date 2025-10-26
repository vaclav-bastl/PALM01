/*
  PALM Configurator Captive Portal (Inline SVG with IDs, iOS-safe tap)
  Seeed XIAO ESP32-S3

  - AP + DNS captive portal + Web UI (dark)
  - Controller on the left; controls on the right
  - Pads & thumbs are wired by explicit element IDs in the SVG
  - Works even if the captive-sheet blocks fetch (uses local defaults)
  - Lower heat: 80 MHz CPU, WiFi sleep, reduced TX power
*/

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

#define CFG_NAMESPACE "palm_cfg"
#define CFG_KEY       "json"

static const IPAddress AP_IP(4,3,2,1);
static const IPAddress AP_MASK(255,255,255,0);

DNSServer dns;
WebServer server(80);
Preferences prefs;

static const char* kDefaultCfg = R"json(
{
  "bleName": "PALM_01",
  "midiDefaultChannel": 1,
  "hand": "LEFT",
  "presetMode": [0,0,1,0],
  "presetSelectorCC": [102,102,102,102],
  "presetGlobal": [[255,0,255,0],[255,0,255,0],[94,0,255,0],[92,0,93,0]],
  "fingers": ["LITTLE_A","LITTLE_B","RING_A","RING_B","MIDDLE_A","MIDDLE_B","INDEX_A","INDEX_B"],
  "preset": [
    [[8,255,255,0,255,0],[8,255,255,0,255,0],[8,255,255,0,255,0],[8,255,255,0,255,0]],
    [[7,255,255,0,255,0],[7,255,255,0,255,0],[7,255,255,0,255,0],[7,255,255,0,255,0]],
    [[6,255,38,255,71,0],[14,255,46,127,79,0],[22,255,54,127,87,0],[30,255,62,127,95,0]],
    [[5,255,37,255,70,0],[13,255,45,127,78,0],[21,255,53,127,86,0],[29,255,61,127,94,0]],
    [[4,255,36,64,69,0],[12,127,44,127,77,0],[20,255,52,127,85,0],[28,255,60,127,93,0]],
    [[3,255,35,64,68,0],[11,255,43,127,76,0],[19,255,51,127,84,0],[27,255,59,127,92,0]],
    [[2,255,34,127,67,0],[10,255,42,127,75,0],[18,255,50,127,83,0],[26,255,58,127,91,0]],
    [[1,255,33,127,66,0],[9,255,41,127,74,0],[17,255,49,127,82,0],[25,255,57,127,90,0]]
  ],
  "noteFirstVelocity": 64
}
)json";

const char HTML_INDEX[] PROGMEM = R"html(
<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover"/>
<title>PALM Configurator</title>
<style>
  :root{
    --bg:#0b0d10; --fg:#eaeef2; --muted:#9aa4af; --card:#14181d; --stroke:#2a2f36;
    --cyan:#47c1ff; --pink:#ee2a7b; --field:#0f1318; --shadow:#0008;
  }
  html,body{margin:0;padding:0;background:var(--bg);color:var(--fg);font-family:ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial;}
  .wrap{max-width:1280px;margin:0 auto;padding:12px}
  h1{font-size:18px;margin:0 0 10px}
  .pill{display:inline-block;padding:4px 8px;border-radius:999px;background:var(--field);border:1px solid var(--stroke);color:var(--muted);font-size:12px}

  /* Layout */
  .shell{display:grid;grid-template-columns: minmax(0,1fr);gap:12px}
  .headerRow{display:grid;grid-template-columns: 1fr 1fr 1fr 1fr;gap:8px}
  @media (max-width:640px){ .headerRow{grid-template-columns:1fr 1fr} }
  .mainRow{display:grid;grid-template-columns: clamp(90px,18vw,150px) 1fr;gap:12px;min-width:0}

  .card{background:var(--card);border-radius:16px;padding:12px;box-shadow:0 1px 0 var(--shadow)}
  .controllerCard{display:flex;justify-content:center}
  .controllerCard svg{width:clamp(90px,18vw,150px);max-width:150px;height:auto;display:block;touch-action:manipulation}

  label{font-size:12px;color:var(--muted)}
  select,input{width:100%;padding:10px;border-radius:12px;border:1px solid var(--stroke);background:var(--field);color:var(--fg)}
  .row2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
  @media (max-width:520px){ .row2{grid-template-columns:1fr} }
  .hr{height:1px;background:var(--stroke);margin:10px 0}
  .section{font-weight:800;letter-spacing:.03em;margin:6px 0 8px}
  .mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;white-space:pre;background:var(--field);border:1px solid var(--stroke);border-radius:12px;padding:8px;overflow:auto}

  /* Visual links */
  .linkCyan{ color:var(--cyan) }
  .linkPink{ color:var(--pink) }
</style>
</head>
<body>
<div class="wrap">
  <h1>PALM Configurator <span id="status" class="pill">ready</span></h1>

  <div class="shell">

    <!-- Header controls (always on top) -->
    <div class="card headerRow">
      <div><label>I AM USING THIS EXO PALM IN:</label>
        <select id="hand">
          <option value="LEFT">LEFT HAND</option>
          <option value="RIGHT">RIGHT HAND</option>
        </select>
      </div>

      <div><label><span id="presetLabel">PRESET 0</span></label>
        <select id="preset" onchange="onPresetDropdown(this.value)">
          <option value="0">PRESET 0</option>
          <option value="1">PRESET 1</option>
          <option value="2">PRESET 2</option>
          <option value="3">PRESET 3</option>
        </select>
      </div>

      <div><label>CH</label>
        <!-- Prefilled so captive-sheet never shows empty -->
        <select id="channel">
          <option>1</option><option>2</option><option>3</option><option>4</option>
          <option>5</option><option>6</option><option>7</option><option>8</option>
          <option>9</option><option>10</option><option>11</option><option>12</option>
          <option>13</option><option>14</option><option>15</option><option>16</option>
        </select>
      </div>

      <div><label>MODE</label>
        <select id="mode" onchange="onModeChange(this.value)">
          <option value="0">CC MODE</option>
          <option value="1">NOTE MODE</option>
        </select>
      </div>
    </div>

    <!-- Controller + Editor -->
    <div class="mainRow">
      <!-- LEFT: controller -->
      <div class="card controllerCard">
        <svg xmlns="http://www.w3.org/2000/svg" width="140" viewBox="0 0 71.73 361.22" preserveAspectRatio="xMidYMin meet" aria-label="PALM Controller">
          <defs>
            <style>
              .z{fill:#bcbec0;} .aa{fill:#231f20;} .ab{fill:#414042;}
              .ac{fill:#00aeef;} .ae{fill:#ee2a7b;} .ag{fill:#e6e7e8;}
            </style>
          </defs>

          <!-- main chassis -->
          <path fill="#1b1b1d" d="M71.49,349.3V11.69L60.03,0H11.92L0,11.69v337.61l11.92,11.92h47.66l11.92-11.92Z"/>
          <!-- BLACK OVERLAY (fixes the too-bright gradient look) -->
          <rect x="6" y="26" width="60" height="290" rx="4" fill="#1e1d1f"/>

          <!-- top grey bar (for screw look) -->
          <rect class="ab" x="4.27" y="8.63" width="61.27" height="12.21"/>

          <!-- THUMBS (tap again to deselect -> preset 0) -->
          <path id="THUMB_R" fill="#bcbec0" onclick="tapThumb(3)" d="M65.1,4.75h-10.6l-5.3,9.18,5.3,9.18h10.6l5.3-9.18-5.3-9.18Z"/>
          <path id="THUMB_M" fill="#bcbec0" onclick="tapThumb(2)" d="M41.21,4.75h-10.6l-5.3,9.18,5.3,9.18h10.6l5.3-9.18-5.3-9.18Z"/>
          <path id="THUMB_L" fill="#bcbec0" onclick="tapThumb(1)" d="M17.32,4.75H6.71L1.41,13.93l5.3,9.18h10.6l5.3-9.18-5.3-9.18Z"/>

          <!-- PADS (left=B, right=A, top→bottom) -->
          <g id="pads" stroke="#000" stroke-opacity=".2">
            <polygon id="INDEX_A"  fill="#bcbec0" onclick="tapPad('INDEX_A')"  points="25.73 56.27 14.08 56.27 5.85 64.51 5.85 76.15 14.08 84.38 25.73 84.38 33.96 76.15 33.96 64.51 25.73 56.27"/>
            <polygon id="INDEX_B"  fill="#bcbec0" onclick="tapPad('INDEX_B')"  points="57.35 56.27 45.7 56.27 37.47 64.51 37.47 76.15 45.7 84.38 57.35 84.38 65.58 76.15 65.58 64.51 57.35 56.27"/>
            <polygon id="MIDDLE_A" fill="#bcbec0" onclick="tapPad('MIDDLE_A')" points="25.73 105.93 14.08 105.93 5.85 114.16 5.85 125.8 14.08 134.04 25.73 134.04 33.96 125.8 33.96 114.16 25.73 105.93"/>
            <polygon id="MIDDLE_B" fill="#bcbec0" onclick="tapPad('MIDDLE_B')" points="57.35 105.93 45.7 105.93 37.47 114.16 37.47 125.8 45.7 134.04 57.35 134.04 65.58 125.8 65.58 114.16 57.35 105.93"/>
            <polygon id="RING_A"   fill="#bcbec0" onclick="tapPad('RING_A')"   points="25.73 154.22 14.08 154.22 5.85 162.46 5.85 174.1 14.08 182.33 25.73 182.33 33.96 174.1 33.96 162.46 25.73 154.22"/>
            <polygon id="RING_B"   fill="#bcbec0" onclick="tapPad('RING_B')"   points="57.35 154.22 45.7 154.22 37.47 162.46 37.47 174.1 45.7 182.33 57.35 182.33 65.58 174.1 65.58 162.46 57.35 154.22"/>
            <polygon id="LITTLE_A" fill="#bcbec0" onclick="tapPad('LITTLE_A')" points="25.73 197.79 14.08 197.79 5.85 206.02 5.85 217.66 14.08 225.9 25.73 225.9 33.96 217.66 33.96 206.02 25.73 197.79"/>
            <polygon id="LITTLE_B" fill="#bcbec0" onclick="tapPad('LITTLE_B')" points="57.35 197.79 45.7 197.79 37.47 206.02 37.47 217.66 45.7 225.9 57.35 225.9 65.58 217.66 65.58 206.02 57.35 197.79"/>
          </g>
        </svg>
      </div>

      <!-- RIGHT: touch editor first, then preset accelerator -->
      <div class="card">
        <!-- MODE PANELS -->
        <div id="ccPanel">
          <div class="section">TOUCH ACTIVATED: <span id="touchName" class="linkCyan">—</span></div>
          <div class="row2">
            <div><label>TOUCH CC</label><input id="tch" type="number" inputmode="numeric" min="0" max="127" value="19"></div>
            <div><label>RANGE</label>
              <select id="rng">
                <option value="255">ON/OFF</option>
                <option value="127">CONTINUOUS</option>
                <option value="0">DISABLED</option>
              </select>
            </div>
            <div><label>WRIST CC</label><input id="wr" type="number" inputmode="numeric" min="0" max="255" value="20"></div>
            <div><label>ON RELEASE</label>
              <select id="wrst">
                <option value="0">RESET TO 0</option>
                <option value="64">RESET TO 64</option>
                <option value="127">RESET TO 127</option>
              </select>
            </div>
            <div><label>ELBOW CC</label><input id="elb" type="number" inputmode="numeric" min="0" max="255" value="21"></div>
            <div><label>ON RELEASE</label>
              <select id="elrst">
                <option value="0">RESET TO 0</option>
                <option value="64" selected>RESET TO 64</option>
                <option value="127">RESET TO 127</option>
              </select>
            </div>
          </div>
        </div>

        <div id="notePanel" style="display:none">
          <div class="section">CHORD: <span id="chordName">C</span></div>
          <div class="row2">
            <div><label>SELECTOR CC (pinky-held)</label><input id="noteSelCc" type="number" inputmode="numeric" min="0" max="127" value="102"></div>
            <div></div>
          </div>
        </div>

        <div class="hr"></div>
        <div class="section">PRESET ACCELERATOR</div>
        <div class="row2">
          <div><label>WRIST CC</label><input id="gwcc" type="number" inputmode="numeric" min="0" max="255" value="20"></div>
          <div><label>ON RELEASE</label>
            <select id="gwreset">
              <option value="0">RESET TO 0</option>
              <option value="64">RESET TO 64</option>
              <option value="127" selected>RESET TO 127</option>
            </select>
          </div>
          <div><label>ELBOW CC</label><input id="gecc" type="number" inputmode="numeric" min="0" max="255" value="255" placeholder="DISABLED"></div>
          <div><label>ON RELEASE</label>
            <select id="gereset">
              <option value="255" selected>DO NOTHING</option>
              <option value="0">RESET TO 0</option>
              <option value="64">RESET TO 64</option>
              <option value="127">RESET TO 127</option>
            </select>
          </div>
        </div>

        <div class="hr"></div>
        <div class="section">MONITOR</div>
        <div id="monitor" class="mono">Ready.</div>
      </div>
    </div>
  </div>
</div>

<script>
  // State
  let currentPreset = 0;     // 0..3
  let selectedPad   = null;  // 'INDEX_A', etc.

  const GREY  = '#bcbec0';
  const CYAN  = '#47c1ff';
  const PINK  = '#ee2a7b';

  const $ = (id)=>document.getElementById(id);
  const setTxt = (id, t)=>{ $(id).textContent = t }

  function monitor(msg){ $('monitor').textContent = msg }

  function updatePresetLabel(){
    const lbl = $('presetLabel');
    lbl.textContent = 'PRESET ' + currentPreset;
    lbl.classList.toggle('linkPink', currentPreset>0);
  }
  function setThumbColors(){
    const ids = ['THUMB_L','THUMB_M','THUMB_R'];
    ids.forEach((id, idx)=>{
      const el = $(id);
      const on = (currentPreset===idx+1);
      el.setAttribute('fill', on ? PINK : GREY);
    });
  }
  function onPresetDropdown(v){
    currentPreset = Number(v)||0;
    setThumbColors();
    updatePresetLabel();
    monitor('Preset: '+currentPreset);
  }
  function tapThumb(p){
    const next = (currentPreset===p)?0:p;
    $('preset').value = String(next);
    onPresetDropdown(next);
  }

  function clearPadColors(){
    document.querySelectorAll('#pads > *').forEach(el=>{
      el.setAttribute('fill', GREY);
      el.removeAttribute('stroke');
    });
  }
  function tapPad(name){
    const el = $(name);
    if(!el) return;
    if(selectedPad===name){
      // toggle off
      selectedPad=null;
      el.setAttribute('fill', GREY);
      el.removeAttribute('stroke');
      setTxt('touchName','—');
      monitor('Pad: none');
      return;
    }
    selectedPad=name;
    clearPadColors();
    el.setAttribute('fill', CYAN);
    el.setAttribute('stroke', CYAN);
    el.setAttribute('stroke-width','1.5');
    setTxt('touchName', name.replace('_',' '));
    monitor('Pad: '+name);
  }

  function onModeChange(v){
    const note = (Number(v)===1);
    $('ccPanel').style.display   = note ? 'none'  : 'block';
    $('notePanel').style.display = note ? 'block' : 'none';
    monitor('Mode: '+(note?'NOTE':'CC'));
  }

  // Boot visuals
  (function init(){
    $('channel').value = '1';
    updatePresetLabel();
    setThumbColors();
    onModeChange($('mode').value);
    // Normalize any baked-in colors from the original art
    clearPadColors();
    monitor('Ready. Taps change fill via setAttribute() for captive reliability.');
  })();
</script>
</body>
</html>









)html";

// --- helpers ---
String loadJsonOrDefault(){
  prefs.begin(CFG_NAMESPACE, true);
  String s = prefs.getString(CFG_KEY, "");
  prefs.end();
  if(s.length()==0) return String(kDefaultCfg);
  return s;
}
bool saveJson(const String& s){
  prefs.begin(CFG_NAMESPACE, false);
  bool ok = prefs.putString(CFG_KEY, s) > 0;
  prefs.end(); return ok;
}
void addCORS(){
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.sendHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers","Content-Type");
}

// --- API & captive portal ---
void handleGetState(){ addCORS(); server.send(200,"application/json",loadJsonOrDefault()); }
void handleSave(){
  addCORS();
  if(!server.hasArg("plain")){ server.send(400,"text/plain","Missing body"); return; }
  String body=server.arg("plain");
  if(body.indexOf("\"preset\"")<0 || body.indexOf("\"presetMode\"")<0){ server.send(400,"text/plain","Malformed JSON"); return; }
  if(!saveJson(body)){ server.send(500,"text/plain","Save failed"); return; }
  server.send(200,"application/json","{\"ok\":true}");
}
void handleReboot(){ addCORS(); server.send(200,"application/json","{\"reboot\":\"now\"}"); delay(200); ESP.restart(); }

void handleCaptive(){ server.sendHeader("Location", String("http://")+AP_IP.toString()+"/", true); server.send(302,"text/plain",""); }
void serveIndex(){
  addCORS();
  String html(HTML_INDEX);
  html.replace("%DEFAULT_JSON%", loadJsonOrDefault());
  server.send(200,"text/html",html);
}

void setupAP(){
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // lower TX power
  WiFi.softAP("PALM_Config","palm1234",7,false,1);

  dns.start(53,"*",AP_IP);

  server.on("/", HTTP_GET, serveIndex);
  server.on("/generate_204", HTTP_GET, handleCaptive);
  server.on("/fwlink",      HTTP_GET, handleCaptive);
  server.onNotFound([](){ if(server.hostHeader()!=AP_IP.toString()){ handleCaptive(); return; } serveIndex(); });

  server.on("/api/state",  HTTP_GET, handleGetState);
  server.on("/api/save",   HTTP_POST, handleSave);
  server.on("/api/save",   HTTP_OPTIONS, [](){ addCORS(); server.send(204); });
  server.on("/api/reboot", HTTP_POST, handleReboot);

  server.begin();
}

void setup(){
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  delay(150);
  setupAP();
}
void loop(){
  dns.processNextRequest();
  server.handleClient();
}
