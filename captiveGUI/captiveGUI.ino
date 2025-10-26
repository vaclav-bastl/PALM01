/*
  PALM Configurator Captive Portal Prototype
  Target: Seeed XIAO ESP32-S3 (Arduino)
  Features:
    - AP + Captive portal (DNSServer + WebServer)
    - Mobile-friendly HTML UI from PROGMEM
    - REST: GET /api/state, POST /api/save, POST /api/reboot
    - Saves entire JSON config to NVS (Preferences)

  WiFi: SSID: PALM_Config  PASS: palm1234
*/

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

#define CFG_NAMESPACE "palm_cfg"
#define CFG_KEY       "json"

static const IPAddress AP_IP(4,3,2,1);     // nice & short to type if needed
static const IPAddress AP_MASK(255,255,255,0);

DNSServer dns;
WebServer server(80);
Preferences prefs;

// ---------- Default JSON mirrors your current tables (trimmed but aligned) ----------
static const char* kDefaultCfg = R"json(
{
  "bleName": "PALM_01",
  "midiDefaultChannel": 1,
  "presetMode": [0, 0, 1, 0],
  "presetSelectorCC": [102,102,102,102],
  "presetGlobal": [
    [255,0,255,0],
    [255,0,255,0],
    [94,0,255,0],
    [92,0,93,0]
  ],
  "fingers": ["LITTLE_A","LITTLE_B","RING_A","RING_B","MIDDLE_A","MIDDLE_B","INDEX_A","INDEX_B"],
  "preset": [
    /* LITTLE_A */ [[8,255,255,0,255,0],[8,255,255,0,255,0],[8,255,255,0,255,0],[8,255,255,0,255,0]],
    /* LITTLE_B */ [[7,255,255,0,255,0],[7,255,255,0,255,0],[7,255,255,0,255,0],[7,255,255,0,255,0]],
    /* RING_A   */ [[6,255,38,255,71,0],[14,255,46,127,79,0],[22,255,54,127,87,0],[30,255,62,127,95,0]],
    /* RING_B   */ [[5,255,37,255,70,0],[13,255,45,127,78,0],[21,255,53,127,86,0],[29,255,61,127,94,0]],
    /* MIDDLE_A */ [[4,255,36,64,69,0],[12,127,44,127,77,0],[20,255,52,127,85,0],[28,255,60,127,93,0]],
    /* MIDDLE_B */ [[3,255,35,64,68,0],[11,255,43,127,76,0],[19,255,51,127,84,0],[27,255,59,127,92,0]],
    /* INDEX_A  */ [[2,255,34,127,67,0],[10,255,42,127,75,0],[18,255,50,127,83,0],[26,255,58,127,91,0]],
    /* INDEX_B  */ [[1,255,33,127,66,0],[9,255,41,127,74,0],[17,255,49,127,82,0],[25,255,57,127,90,0]]
  ],
  "leftHand": true,
  "noteFirstVelocity": 64
}
)json";

// ------------- Tiny helpers -------------
String loadJsonOrDefault() {
  prefs.begin(CFG_NAMESPACE, /*readOnly=*/true);
  String s = prefs.getString(CFG_KEY, "");
  prefs.end();
  if (s.length() == 0) return String(kDefaultCfg);
  return s;
}

bool saveJson(const String& s) {
  prefs.begin(CFG_NAMESPACE, /*readOnly=*/false);
  bool ok = prefs.putString(CFG_KEY, s) > 0;
  prefs.end();
  return ok;
}

// ------------- CORS -------------
void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ------------- API handlers -------------
void handleGetState() {
  addCORS();
  String cfg = loadJsonOrDefault();
  server.send(200, "application/json", cfg);
}

void handleSave() {
  addCORS();
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "Missing body"); return; }
  String body = server.arg("plain");
  // quick sanity: must contain some keys
  if (body.indexOf("\"preset\"") < 0 || body.indexOf("\"presetMode\"") < 0) {
    server.send(400, "text/plain", "Malformed JSON");
    return;
  }
  bool ok = saveJson(body);
  if (!ok) { server.send(500, "text/plain", "Save failed"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleReboot() {
  addCORS();
  server.send(200, "application/json", "{\"reboot\":\"now\"}");
  delay(200);
  ESP.restart();
}

const char HTML_INDEX[] PROGMEM = R"html(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover"/>
<title>PALM Configurator</title>
<style>
  :root { --bg:#0b0d10; --fg:#eaeef2; --muted:#9aa4af; --card:#14181d; --stroke:#2a2f36; --hi:#47c1ff; --ok:#46d37d; --warn:#ffbf3f; --err:#ff6b6b; }
  html,body{margin:0;padding:0;background:var(--bg);color:var(--fg);font-family:ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial;}
  .wrap{max-width:1100px;margin:0 auto;padding:16px}
  h1{font-size:20px;margin:6px 0 14px;display:flex;gap:8px;align-items:center}
  .pill{display:inline-block;padding:4px 8px;border-radius:999px;background:#0f1318;border:1px solid var(--stroke);color:var(--muted);font-size:12px}
  .row{display:grid;gap:12px;grid-template-columns:1fr}
  @media(min-width:860px){ .row{grid-template-columns:420px 1fr} }
  .card{background:var(--card);border-radius:18px;padding:14px;box-shadow:0 1px 0 #0008}
  label{font-size:12px;color:var(--muted)}
  select,input{width:100%;padding:10px;border-radius:12px;border:1px solid var(--stroke);background:#0f1318;color:var(--fg)}
  .btn-row{display:flex;gap:8px;flex-wrap:wrap}
  button{padding:10px 14px;border:0;border-radius:12px;background:var(--ok);color:#001;font-weight:700}
  .mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;white-space:pre;background:#0f1318;border:1px solid var(--stroke);border-radius:12px;padding:10px;overflow:auto;max-height:260px}
  .grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
  .grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
  .sectionTitle{font-weight:800;letter-spacing:.03em;margin:6px 0 10px}
  /* Controller SVG sizing */
  .controllerCard{display:flex;justify-content:center}
  svg{max-width:380px;height:auto}
  .pad{cursor:pointer;transition:filter .12s ease, opacity .12s ease}
  .pad:hover{filter:brightness(1.1)}
  .pad[data-active="1"]{stroke:var(--hi);stroke-width:2}
  .pad.fillA{fill:#222}
  .pad.fillB{fill:#444}
  .pad.fillThumb{fill:#1c1f24}
  .pad.selected{fill:var(--hi) !important}
  .bSmall{fill:#0f1318;stroke:#2a2f36;stroke-width:1}
  .badge{display:inline-block;background:#0f1318;border:1px solid var(--stroke);border-radius:10px;padding:2px 6px;font-size:11px;color:var(--muted)}
  .hr{height:1px;background:var(--stroke);margin:12px 0}
</style>
</head>
<body>
<div class="wrap">
  <h1>PALM Configurator <span class="pill" id="status">loading…</span></h1>

  <div class="row">
    <!-- LEFT: Controller -->
    <div class="card controllerCard">
      <!-- Simplified vector replica with named pads -->
      <svg viewBox="0 0 220 680" aria-label="PALM Controller">
        <!-- body -->
        <rect x="18" y="16" width="184" height="648" rx="18" fill="#0a0d11" stroke="#2a2f36"/>
        <!-- top bolts -->
        <circle cx="46" cy="44" r="10" class="bSmall"/>
        <circle cx="76" cy="44" r="10" class="bSmall"/>
        <circle cx="106" cy="44" r="10" class="bSmall"/>
        <!-- 4x2 finger pads (A column right, B column left visually similar to your mockup) -->
        <!-- Row 1 -->
        <rect x="48" y="88"  width="52" height="52" rx="10" class="pad fillB" data-f="INDEX_B"/>
        <rect x="120" y="88" width="52" height="52" rx="10" class="pad fillA" data-f="INDEX_A"/>
        <!-- Row 2 -->
        <rect x="48" y="162" width="52" height="52" rx="10" class="pad fillB" data-f="MIDDLE_B"/>
        <rect x="120" y="162" width="52" height="52" rx="10" class="pad fillA" data-f="MIDDLE_A"/>
        <!-- Row 3 -->
        <rect x="48" y="236" width="52" height="52" rx="10" class="pad fillB" data-f="RING_B"/>
        <rect x="120" y="236" width="52" height="52" rx="10" class="pad fillA" data-f="RING_A"/>
        <!-- Row 4 -->
        <rect x="48" y="310" width="52" height="52" rx="10" class="pad fillB" data-f="LITTLE_B"/>
        <rect x="120" y="310" width="52" height="52" rx="10" class="pad fillA" data-f="LITTLE_A"/>
        <!-- bottom buttons -->
        <circle cx="56" cy="460" r="12" class="bSmall" data-btn="L"/>
        <circle cx="110" cy="460" r="12" class="bSmall" data-btn="M"/>
        <circle cx="164" cy="460" r="12" class="bSmall" data-btn="R"/>
        <!-- tiny square buttons -->
        <rect x="36" y="504" width="24" height="24" rx="3" class="bSmall"/>
        <rect x="160" y="504" width="24" height="24" rx="3" class="bSmall"/>
        <!-- logo plate -->
        <rect x="82" y="540" width="56" height="56" rx="8" fill="#0f8ad8"/>
        <text x="110" y="606" text-anchor="middle" font-size="14" fill="#e5e5e5" font-weight="800">PALM</text>
      </svg>
    </div>

    <!-- RIGHT: Editor -->
    <div class="card">
      <div class="grid3">
        <div><label>Preset</label><select id="preset"></select></div>
        <div><label>Mode</label><select id="mode"><option value="0">CC MODE</option><option value="1">NOTE MODE</option></select></div>
        <div><label>CH</label><select id="channel"></select></div>
      </div>

      <div class="hr"></div>

      <div class="sectionTitle">PRESET ACCELERATOR</div>
      <div class="grid2">
        <div><label>WRIST CC</label><input id="gwcc" type="number" min="0" max="255"></div>
        <div><label>ON RELEASE</label><input id="gwreset" type="number" min="0" max="127"></div>
        <div><label>ELBOW CC</label><input id="gecc" type="number" min="0" max="255"></div>
        <div><label>ON RELEASE</label><input id="gereset" type="number" min="0" max="127"></div>
      </div>

      <div class="hr"></div>

      <div class="sectionTitle">TOUCH EDITOR <span id="touchBadge" class="badge">Tap a pad →</span></div>
      <div class="grid2">
        <div><label>TARGET (finger)</label><input id="fingerName" disabled></div>
        <div><label>SELECTOR CC (pinky-held)</label><input id="selcc" type="number" min="0" max="127"></div>

        <div><label>TOUCH CC</label><input id="tch" type="number" min="0" max="127"></div>
        <div>
          <label>RANGE</label>
          <select id="rng">
            <option value="255">GATE (0↔127)</option>
            <option value="127">CONTINUOUS</option>
            <option value="0">DISABLED</option>
          </select>
        </div>

        <div><label>WRIST CC</label><input id="wr" type="number" min="0" max="255"></div>
        <div><label>ON RELEASE</label><input id="wrst" type="number" min="0" max="127"></div>

        <div><label>ELBOW CC</label><input id="elb" type="number" min="0" max="255"></div>
        <div><label>ON RELEASE</label><input id="elrst" type="number" min="0" max="127"></div>
      </div>

      <div class="hr"></div>
      <div class="btn-row">
        <button id="save">Save</button>
        <button id="reboot" style="background:var(--warn)">Save & Reboot</button>
      </div>
      <div class="hr"></div>
      <div class="sectionTitle">MONITOR</div>
      <div id="monitor" class="mono">{…}</div>
      <div class="hr"></div>
      <div class="badge">Tip</div> <span class="badge" style="margin-left:6px">255 = DISABLED</span>
    </div>
  </div>
</div>

<script>
const S = { cfg:null, P:0, selected:null };
const FINGERS_ORDER = [ "INDEX_A","INDEX_B","MIDDLE_A","MIDDLE_B","RING_A","RING_B","LITTLE_A","LITTLE_B" ];

function qs(id){return document.getElementById(id)}
function $$(sel,root=document){return Array.from(root.querySelectorAll(sel))}
function setBadge(txt){const b=qs('touchBadge'); b.textContent=txt;}

function ensureArrays() {
  if(!S.cfg) return;
  S.cfg.fingers = S.cfg.fingers || ["LITTLE_A","LITTLE_B","RING_A","RING_B","MIDDLE_A","MIDDLE_B","INDEX_A","INDEX_B"];
  S.cfg.presetMode = S.cfg.presetMode || [0,0,0,0];
  S.cfg.presetSelectorCC = S.cfg.presetSelectorCC || [102,102,102,102];
  S.cfg.presetGlobal = S.cfg.presetGlobal || [[255,0,255,0],[255,0,255,0],[255,0,255,0],[255,0,255,0]];
}

async function loadState(){
  qs('status').textContent='loading…';
  const r = await fetch('/api/state');
  S.cfg = await r.json();
  ensureArrays();
  buildTopControls();
  wireController();
  loadSelected(null);
  qs('status').textContent='ready';
  refreshMonitor();
}

function buildTopControls(){
  const pSel=qs('preset'); pSel.innerHTML='';
  for(let i=0;i<4;i++){ const o=document.createElement('option'); o.value=i; o.textContent='PRESET '+i; pSel.appendChild(o); }
  pSel.value=S.P;

  const chSel=qs('channel'); chSel.innerHTML='';
  for(let i=1;i<=16;i++){ const o=document.createElement('option'); o.value=i; o.textContent=i; chSel.appendChild(o); }

  wirePresetFields();
  pSel.onchange = ()=>{ S.P=+pSel.value; wirePresetFields(); loadSelected(S.selected); refreshMonitor(); };
  qs('mode').onchange = savePresetMeta;
  chSel.onchange = savePresetMeta;

  ['gwcc','gwreset','gecc','gereset','selcc'].forEach(id=>qs(id).addEventListener('input', savePresetMeta));
  ['tch','rng','wr','wrst','elb','elrst'].forEach(id=>qs(id).addEventListener('input', saveTouchFields));

  qs('save').onclick = doSave;
  qs('reboot').onclick = ()=>doSave(true);
}

function wirePresetFields(){
  const pm = S.cfg.presetMode[S.P];
  const isNote = (pm>=1 && pm<=16);
  qs('mode').value = isNote ? 1 : 0;
  qs('channel').value = isNote ? pm : (S.cfg.midiDefaultChannel || 1);

  const g = S.cfg.presetGlobal[S.P];
  qs('gwcc').value   = g[0]; qs('gwreset').value= g[1];
  qs('gecc').value   = g[2]; qs('gereset').value= g[3];
  qs('selcc').value  = S.cfg.presetSelectorCC[S.P];
}

function savePresetMeta(){
  const isNote = +qs('mode').value === 1;
  const ch = +qs('channel').value;
  S.cfg.presetMode[S.P] = isNote ? ch : 0;
  S.cfg.presetGlobal[S.P] = [ +qs('gwcc').value, +qs('gwreset').value, +qs('gecc').value, +qs('gereset').value ];
  S.cfg.presetSelectorCC[S.P] = +qs('selcc').value;
  refreshMonitor();
}

function nameToIndex(name){
  // Match JSON order (LITTLE_A..INDEX_B), return index into S.cfg.preset
  return S.cfg.fingers.indexOf(name);
}

function wireController(){
  // click handlers for pads
  $$('.pad').forEach(p=>{
    p.addEventListener('click', ()=>{
      $$('.pad').forEach(q=>q.classList.remove('selected'));
      p.classList.add('selected');
      const name = p.getAttribute('data-f');
      loadSelected(name);
    });
  });
}

function loadSelected(name){
  S.selected = name;
  if(!name){ qs('fingerName').value = ''; setBadge('Tap a pad →'); return; }
  setBadge(name.replace('_',' '));
  qs('fingerName').value = name.replace('_',' ');

  const fIdx = nameToIndex(name);
  if (fIdx < 0) return;
  const row = S.cfg.preset[fIdx][S.P]; // [TCH_CC,RNG,WR_CC,WR_RESET,EL_CC,EL_RESET]
  qs('tch').value = row[0];
  qs('rng').value = (row[1]===255 || row[1]===127) ? String(row[1]) : '0';
  qs('wr').value  = row[2];
  qs('wrst').value= row[3];
  qs('elb').value = row[4];
  qs('elrst').value=row[5];
}

function saveTouchFields(){
  if(!S.selected) return;
  const fIdx = nameToIndex(S.selected);
  if (fIdx < 0) return;
  const rngV = +qs('rng').value;
  S.cfg.preset[fIdx][S.P] = [
    +qs('tch').value,
    rngV,
    +qs('wr').value,
    +qs('wrst').value,
    +qs('elb').value,
    +qs('elrst').value
  ];
  refreshMonitor();
}

function refreshMonitor(){
  qs('monitor').textContent = JSON.stringify(S.cfg, null, 2);
}

async function doSave(reboot=false){
  qs('status').textContent='saving…';
  const r = await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(S.cfg)});
  if(!r.ok){ qs('status').textContent='save failed'; return; }
  qs('status').textContent='saved';
  if(reboot){ await fetch('/api/reboot',{method:'POST'}); }
}

document.addEventListener('DOMContentLoaded', loadState);
</script>
</body></html>
)html";


// always redirect unknown hosts/paths to portal index
void handleCaptive() {
  server.sendHeader("Location", String("http://") + AP_IP.toString() + String("/"), true);
  server.send(302, "text/plain", "");
}

void serveIndex() {
  addCORS();
  server.send_P(200, "text/html", HTML_INDEX);
}

void setupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP("PALM_Config", "palm1234", 7, false, 4); // ch7, open to 4 clients

  // DNS: redirect all queries to AP_IP
  dns.start(53, "*", AP_IP);

  // HTTP routes
  server.on("/", HTTP_GET, serveIndex);
  server.on("/generate_204", HTTP_GET, handleCaptive);   // Android
  server.on("/fwlink", HTTP_GET, handleCaptive);         // Windows
  server.onNotFound([](){
    if (server.hostHeader() != AP_IP.toString()) { handleCaptive(); return; }
    serveIndex();
  });

  // API
  server.on("/api/state", HTTP_GET, handleGetState);
  server.on("/api/save",  HTTP_POST, handleSave);
  server.on("/api/reboot",HTTP_POST, handleReboot);
  server.on("/api/save",  HTTP_OPTIONS, [](){ addCORS(); server.send(204); });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nPALM Configurator Portal starting…");
  setupAP();
  Serial.printf("AP IP: %s\n", AP_IP.toString().c_str());
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
}
