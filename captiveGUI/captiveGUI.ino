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
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover"/>
<title>PALM Configurator</title>
<style>
:root { --bg:#0b0d10; --fg:#eaeef2; --muted:#9aa4af; --card:#14181d; --stroke:#2a2f36; --hi:#47c1ff; --ok:#46d37d; --warn:#ffbf3f; }
html,body{margin:0;padding:0;background:var(--bg);color:var(--fg);font-family:ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial;}
.wrap{max-width:1200px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:6px 0 14px;display:flex;gap:8px;align-items:center}
.pill{display:inline-block;padding:4px 8px;border-radius:999px;background:#0f1318;border:1px solid var(--stroke);color:var(--muted);font-size:12px}
.layout{display:grid;grid-template-columns:220px 1fr;gap:16px}
@media(max-width:900px){ .layout{grid-template-columns:1fr}}
.card{background:var(--card);border-radius:18px;padding:14px;box-shadow:0 1px 0 #0008}
.controllerCard{display:flex;justify-content:center;align-items:flex-start}
.controllerCard svg{width:180px;max-width:180px;height:auto;display:block}
label{font-size:12px;color:var(--muted)}
select,input{width:100%;padding:10px;border-radius:12px;border:1px solid var(--stroke);background:#0f1318;color:var(--fg)}
.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.hr{height:1px;background:var(--stroke);margin:12px 0}
.sectionTitle{font-weight:800;letter-spacing:.03em;margin:6px 0 10px}
.btns{display:flex;gap:8px;flex-wrap:wrap}
button{padding:10px 14px;border:0;border-radius:12px;background:var(--ok);color:#001;font-weight:700}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;white-space:pre;background:#0f1318;border:1px solid var(--stroke);border-radius:12px;padding:10px;overflow:auto;max-height:220px}
.pad{cursor:pointer;transition:filter .12s ease; pointer-events:auto}
.pad:hover{filter:brightness(1.08)}
.pad.selected{ fill:#47c1ff !important; stroke:#47c1ff; stroke-width:1.5 }
.thumb{cursor:pointer; pointer-events:auto}
.thumb.on{ fill:#ee2a7b !important }
.thumb.off{ opacity:.65 }
</style>
</head>
<body>
<div class="wrap">
  <h1>PALM Configurator <span class="pill" id="status">loading…</span></h1>

  <div class="layout">
    <!-- LEFT: Inline SVG with explicit IDs on interactive shapes -->
    <div class="card controllerCard">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 71.73 361.22" aria-label="PALM Controller">
        <!-- (non-interactive bits trimmed for brevity; visuals preserved) -->
        <g fill="#1f1f22"><path d="M71.49,349.3V11.69L60.03,0H11.92L0,11.69v337.61l11.92,11.92h47.66l11.92-11.92Z"/></g>
        <!-- THUMBS -->
        <g id="thumbs" fill="#bcbec0">
          <path id="THUMB_R" class="thumb" d="M65.1,4.75h-10.6l-5.3,9.18,5.3,9.18h10.6l5.3-9.18-5.3-9.18Z"/>
          <path id="THUMB_M" class="thumb" d="M41.21,4.75h-10.6l-5.3,9.18,5.3,9.18h10.6l5.3-9.18-5.3-9.18Z"/>
          <path id="THUMB_L" class="thumb on"  d="M17.32,4.75H6.71L1.41,13.93l5.3,9.18h10.6l5.3-9.18-5.3-9.18Z"/>
        </g>
        <!-- PADS (IDs = finger names) left=B, right=A; rows top→bottom -->
        <g id="pads" fill="#bcbec0">
          <polygon id="INDEX_B"  class="pad" points="25.73 56.27 14.08 56.27 5.85 64.51 5.85 76.15 14.08 84.38 25.73 84.38 33.96 76.15 33.96 64.51 25.73 56.27"/>
          <polygon id="INDEX_A"  class="pad" points="57.35 56.27 45.7 56.27 37.47 64.51 37.47 76.15 45.7 84.38 57.35 84.38 65.58 76.15 65.58 64.51 57.35 56.27"/>
          <polygon id="MIDDLE_B" class="pad" points="25.73 105.93 14.08 105.93 5.85 114.16 5.85 125.8 14.08 134.04 25.73 134.04 33.96 125.8 33.96 114.16 25.73 105.93"/>
          <polygon id="MIDDLE_A" class="pad" points="57.35 105.93 45.7 105.93 37.47 114.16 37.47 125.8 45.7 134.04 57.35 134.04 65.58 125.8 65.58 114.16 57.35 105.93"/>
          <polygon id="RING_B"   class="pad" points="25.73 154.22 14.08 154.22 5.85 162.46 5.85 174.1 14.08 182.33 25.73 182.33 33.96 174.1 33.96 162.46 25.73 154.22"/>
          <polygon id="RING_A"   class="pad" points="57.35 154.22 45.7 154.22 37.47 162.46 37.47 174.1 45.7 182.33 57.35 182.33 65.58 174.1 65.58 162.46 57.35 154.22"/>
          <polygon id="LITTLE_B" class="pad" points="25.73 197.79 14.08 197.79 5.85 206.02 5.85 217.66 14.08 225.9 25.73 225.9 33.96 217.66 33.96 206.02 25.73 197.79"/>
          <polygon id="LITTLE_A" class="pad" points="57.35 197.79 45.7 197.79 37.47 206.02 37.47 217.66 45.7 225.9 57.35 225.9 65.58 217.66 65.58 206.02 57.35 197.79"/>
        </g>
      </svg>
    </div>

    <!-- RIGHT: Controls -->
    <div class="card">
      <div class="row3">
        <div><label>I AM USING THIS EXO PALM IN:</label>
          <select id="hand"><option value="LEFT">LEFT HAND</option><option value="RIGHT">RIGHT HAND</option></select>
        </div>
        <div><label>PRESET</label><select id="preset"></select></div>
        <div><label>CH</label><select id="channel"></select></div>
      </div>
      <div class="row3" style="margin-top:8px">
        <div><label>MODE</label><select id="mode"><option value="0">CC MODE</option><option value="1">NOTE MODE</option></select></div>
        <div><label>SELECTOR CC (pinky-held)</label><input id="selcc" type="number" inputmode="numeric" min="0" max="127"></div>
        <div></div>
      </div>

      <div class="hr"></div>
      <div class="sectionTitle">PRESET ACCELERATOR</div>
      <div class="row2">
        <div><label>WRIST CC</label><input id="gwcc" type="number" inputmode="numeric" min="0" max="255"></div>
        <div><label>ON RELEASE</label>
          <select id="gwreset"><option value="0">RESET TO 0</option><option value="64">RESET TO 64</option><option value="127">RESET TO 127</option></select>
        </div>
        <div><label>ELBOW CC</label><input id="gecc" type="number" inputmode="numeric" min="0" max="255"></div>
        <div><label>ON RELEASE</label>
          <select id="gereset"><option value="0">RESET TO 0</option><option value="64">RESET TO 64</option><option value="127">RESET TO 127</option><option value="255">DO NOTHING</option></select>
        </div>
      </div>

      <div class="hr"></div>
      <div class="sectionTitle">TOUCH EDITOR <span id="touchBadge" class="pill">Tap a pad →</span></div>
      <div class="row2">
        <div><label>TARGET (finger)</label><input id="fingerName" disabled></div>
        <div><label>RANGE</label>
          <select id="rng"><option value="255">ON/OFF</option><option value="127">CONTINUOUS</option><option value="0">DISABLED</option></select>
        </div>

        <div><label>TOUCH CC</label><input id="tch" type="number" inputmode="numeric" min="0" max="127"></div>
        <div><label>WRIST CC</label><input id="wr" type="number" inputmode="numeric" min="0" max="255"></div>
        <div><label>ON RELEASE</label><select id="wrst"><option value="0">RESET TO 0</option><option value="64">RESET TO 64</option><option value="127">RESET TO 127</option></select></div>
        <div><label>ELBOW CC</label><input id="elb" type="number" inputmode="numeric" min="0" max="255"></div>
        <div><label>ON RELEASE</label><select id="elrst"><option value="0">RESET TO 0</option><option value="64">RESET TO 64</option><option value="127">RESET TO 127</option></select></div>
      </div>

      <div class="hr"></div>
      <div class="btns">
        <button id="save">Save</button>
        <button id="reboot" style="background:var(--warn)">Save & Reboot</button>
      </div>

      <div class="hr"></div>
      <div class="sectionTitle">MONITOR</div>
      <div id="monitor" class="mono">{…}</div>
    </div>
  </div>
</div>

<script id="defaults" type="application/json">%DEFAULT_JSON%</script>
<script>
const S = { cfg:null, P:0, selected:null };

function qs(id){ return document.getElementById(id) }
function setBadge(t){ qs('touchBadge').textContent = t }
function bindTap(el, fn){
  ['pointerdown','touchstart','click'].forEach(ev=>el.addEventListener(ev, e=>{ e.preventDefault(); fn(e); }, {passive:false}));
}

/* === Wire SVG by explicit IDs (no geometry guessing) === */
function wireSvg(){
  const svg = document.querySelector('.controllerCard svg'); if(!svg) return;
  // pads
  const padIds = ['INDEX_A','INDEX_B','MIDDLE_A','MIDDLE_B','RING_A','RING_B','LITTLE_A','LITTLE_B'];
  padIds.forEach(id=>{
    const el = svg.getElementById(id);
    if(!el) return;
    el.classList.add('pad');
    bindTap(el, ()=>{
      svg.querySelectorAll('.pad').forEach(n=>n.classList.remove('selected'));
      el.classList.add('selected');
      loadSelected(id);
    });
  });
  // thumbs → presets 1..3 (left→right: L=1, M=2, R=3)
  const thumbs = [ ['THUMB_L',1], ['THUMB_M',2], ['THUMB_R',3] ];
  thumbs.forEach(([id,p]){
    const el = svg.getElementById(id);
    if(!el) return;
    el.classList.add('thumb','off');
    bindTap(el, ()=>{
      S.P = p;
      qs('preset').value = S.P;
      wirePresetFields();
      loadSelected(null);
      refreshThumbLights();
      refreshMonitor();
    });
  });
  refreshThumbLights();
}
function refreshThumbLights(){
  const svg = document.querySelector('.controllerCard svg'); if(!svg) return;
  svg.querySelectorAll('.thumb').forEach(t=>{ t.classList.remove('on'); t.classList.add('off'); });
  if (S.P>=1 && S.P<=3){
    const id = ['THUMB_L','THUMB_M','THUMB_R'][S.P-1];
    const el = svg.getElementById(id);
    if(el){ el.classList.add('on'); el.classList.remove('off'); }
  }
}

/* === Data/UI === */
function ensureArrays(){
  const c=S.cfg; if(!c) return;
  c.fingers ||= ["LITTLE_A","LITTLE_B","RING_A","RING_B","MIDDLE_A","MIDDLE_B","INDEX_A","INDEX_B"];
  c.presetMode ||= [0,0,0,0];
  c.presetSelectorCC ||= [102,102,102,102];
  c.presetGlobal ||= [[255,0,255,0],[255,0,255,0],[255,0,255,0],[255,0,255,0]];
  c.hand ||= "LEFT";
}

function buildControls(){
  // presets 0..3
  const pSel=qs('preset'); pSel.innerHTML='';
  for(let i=0;i<4;i++){ const o=document.createElement('option'); o.value=i; o.textContent='PRESET '+i; pSel.appendChild(o); }
  pSel.value=S.P;
  // channel 1..16
  const chSel=qs('channel'); chSel.innerHTML='';
  for(let i=1;i<=16;i++){ const o=document.createElement('option'); o.value=i; o.textContent=i; chSel.appendChild(o); }
  qs('hand').value = S.cfg.hand || 'LEFT';

  wirePresetFields();

  qs('hand').onchange = ()=>{ S.cfg.hand = qs('hand').value; refreshMonitor(); };
  pSel.onchange = ()=>{ S.P=+pSel.value; wirePresetFields(); loadSelected(S.selected); refreshThumbLights(); refreshMonitor(); };
  qs('mode').onchange = savePresetMeta;
  chSel.onchange = savePresetMeta;

  ['selcc','gwcc','gecc'].forEach(id=>qs(id).addEventListener('input', savePresetMeta));
  ['gwreset','gereset'].forEach(id=>qs(id).addEventListener('change', savePresetMeta));

  ['tch','wr','elb'].forEach(id=>qs(id).addEventListener('input', saveTouchFields));
  ['rng','wrst','elrst'].forEach(id=>qs(id).addEventListener('change', saveTouchFields));

  qs('save').onclick = doSave;
  qs('reboot').onclick = ()=>doSave(true);
}

function wirePresetFields(){
  const pm = S.cfg.presetMode[S.P];
  const isNote=(pm>=1 && pm<=16);
  qs('mode').value = isNote?1:0;
  qs('channel').value = isNote?pm:(S.cfg.midiDefaultChannel||1);
  const g = S.cfg.presetGlobal[S.P];
  qs('gwcc').value=g[0]; qs('gwreset').value=g[1]; qs('gecc').value=g[2]; qs('gereset').value=g[3];
  qs('selcc').value=S.cfg.presetSelectorCC[S.P];
}

function nameToIndex(name){ return S.cfg.fingers.indexOf(name); }

function loadSelected(name){
  S.selected = name;
  if(!name){ qs('fingerName').value=''; setBadge('Tap a pad →'); return; }
  setBadge(name.replace('_',' '));
  qs('fingerName').value = name.replace('_',' ');
  const f = nameToIndex(name); if(f<0) return;
  const row = S.cfg.preset[f][S.P]; // [TCH_CC,RNG,WR_CC,WR_RESET,EL_CC,EL_RESET]
  qs('tch').value = row[0];
  qs('rng').value = (row[1]===255||row[1]===127)?String(row[1]):'0';
  qs('wr').value  = row[2];
  qs('wrst').value= row[3];
  qs('elb').value = row[4];
  qs('elrst').value=row[5];
}

function saveTouchFields(){
  if(!S.selected) return;
  const f = nameToIndex(S.selected); if(f<0) return;
  S.cfg.preset[f][S.P] = [ +qs('tch').value, +qs('rng').value, +qs('wr').value, +qs('wrst').value, +qs('elb').value, +qs('elrst').value ];
  refreshMonitor();
}
function savePresetMeta(){
  const isNote=+qs('mode').value===1;
  const ch=+qs('channel').value;
  S.cfg.presetMode[S.P]=isNote?ch:0;
  S.cfg.presetGlobal[S.P]=[+qs('gwcc').value,+qs('gwreset').value,+qs('gecc').value,+qs('gereset').value];
  S.cfg.presetSelectorCC[S.P]=+qs('selcc').value;
  refreshMonitor();
}
function refreshMonitor(){ qs('monitor').textContent = JSON.stringify(S.cfg,null,2); }

/* === Boot: wire SVG first, then UI with defaults, then try fetch === */
async function boot(){
  wireSvg(); // interaction works immediately
  S.cfg = JSON.parse(document.getElementById('defaults').textContent);
  ensureArrays(); buildControls(); refreshMonitor(); qs('status').textContent='ready';
  try{
    const r = await fetch('/api/state',{cache:'no-store'}); 
    if(r.ok){ S.cfg = await r.json(); ensureArrays(); buildControls(); refreshMonitor(); }
  }catch(e){ /* captive sheet blocked; keep defaults */ }
}
async function doSave(reboot=false){
  qs('status').textContent='saving…';
  try{
    const r = await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(S.cfg)});
    if(!r.ok) throw 0;
    qs('status').textContent='saved';
    if(reboot){ await fetch('/api/reboot',{method:'POST'}); }
  }catch(e){ qs('status').textContent='save failed'; }
}
document.addEventListener('DOMContentLoaded', boot);
</script>
</body></html>
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
