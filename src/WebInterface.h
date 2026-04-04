#pragma once
// ═══════════════════════════════════════════════════════════════
//  WebInterface — serves an HTML control panel over WiFi AP
//
//  Toggle AP on/off via startAP() / stopAP().
//  Calls into OSCHandler::executeCommand() for each request.
// ═══════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// Forward declaration — the command executor is in OSCHandler
class OSCHandler;

#ifndef AP_SSID
  #define AP_SSID "Scoreboard"
#endif
#ifndef AP_PASS
  #define AP_PASS "12345678"     // min 8 chars for WPA2
#endif
#ifndef WEB_PORT
  #define WEB_PORT 80
#endif

class WebInterface {
public:
    WebInterface(OSCHandler& osc) : _osc(osc), _server(WEB_PORT), _running(false) {}

    bool isRunning() const { return _running; }

    /// Start the AP and web server.  Returns the AP IP address.
    IPAddress startAP() {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        delay(100);                       // let the AP stabilise
        IPAddress ip = WiFi.softAPIP();

        _server.on("/",       [this]() { _handleRoot(); });
        _server.on("/cmd",    [this]() { _handleCmd();  });
        _server.on("/status", [this]() { _handleStatus(); });
        _server.onNotFound(   [this]() { _server.send(404, "text/plain", "Not found"); });
        _server.begin();
        _running = true;

        Serial.printf("AP started — SSID: %s  IP: %s\n",
                      AP_SSID, ip.toString().c_str());
        return ip;
    }

    /// Stop the AP and web server.
    void stopAP() {
        _server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        _running = false;
        Serial.println("AP stopped");
    }

    /// Call every loop() when running.
    void update() {
        if (_running) _server.handleClient();
    }

    const char* ssid() const { return AP_SSID; }

private:
    OSCHandler& _osc;
    WebServer   _server;
    bool        _running;

    // ── HTTP handlers ────────────────────────────────────────

    void _handleRoot() {
        _server.send(200, "text/html", _buildPage());
    }

    void _handleCmd();    // defined after OSCHandler is fully visible

    void _handleStatus(); // returns JSON status snapshot

    // ── HTML page ────────────────────────────────────────────
    // Built as a single PROGMEM-friendly string.
    // The JS on the page calls  GET /cmd?q=/address+arg1+arg2  etc.

    String _buildPage() {
        String p;
        p.reserve(12000);
        p += F(R"rawhtml(<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Scoreboard Control</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:10px;max-width:480px;margin:auto}
h1{text-align:center;color:#0ff;font-size:1.3em;margin:8px 0}
.card{background:#16213e;border-radius:8px;padding:10px;margin:8px 0}
.card h2{font-size:.9em;color:#8af;margin-bottom:6px;border-bottom:1px solid #334}
label{display:block;font-size:.8em;margin:4px 0 2px;color:#aaa}
input[type=range]{width:100%}
input[type=text],input[type=number]{width:100%;padding:4px;background:#0d1b2a;color:#eee;border:1px solid #334;border-radius:4px;font-size:.85em}
input[type=color]{width:40px;height:28px;border:none;background:none;cursor:pointer;vertical-align:middle}
button{background:#0f3460;color:#e0e0e0;border:1px solid #1a4a7a;border-radius:5px;padding:6px 12px;cursor:pointer;font-size:.8em;margin:2px}
button:active{background:#1a5a9a}
.row{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
.row>*{flex:1;min-width:0}
.val{color:#0ff;font-size:.8em;min-width:30px;text-align:right;flex:0 0 auto}
.toggle{display:inline-flex;align-items:center;gap:4px;font-size:.8em}
.toggle input{width:auto}
select{background:#0d1b2a;color:#eee;border:1px solid #334;border-radius:4px;padding:4px;font-size:.85em}
#log{background:#0d1b2a;color:#6f6;font-size:.7em;height:60px;overflow-y:auto;padding:4px;border-radius:4px;margin-top:6px;font-family:monospace;white-space:pre-wrap}
</style></head><body>
<h1>&#127918; Scoreboard Control</h1>

<!-- ── Layers & Mode ────────────────────── -->
<div class="card">
  <h2>Layers &amp; Mode</h2>
  <div class="row">
    <label class="toggle"><input type="checkbox" id="textEn" checked onchange="cmd('/text/enable '+(this.checked?1:0))">Text</label>
    <label class="toggle"><input type="checkbox" id="partEn" onchange="cmd('/particles/enable '+(this.checked?1:0))">Particles</label>
    <select id="mode" onchange="cmd('/mode '+this.value)">
      <option value="0">Text (static)</option>
      <option value="1">Scroll Up</option>
      <option value="2">Scroll Down</option>
    </select>
  </div>
</div>

<!-- ── Brightness ───────────────────────── -->
<div class="card">
  <h2>Brightness</h2>
  <div class="row">
    <label>Global</label><input type="range" id="bright" min="0" max="40" value="10" oninput="V('brightV',this.value);cmd('/brightness '+this.value)"><span class="val" id="brightV">10</span>
  </div>
  <div class="row">
    <label>Text</label><input type="range" id="tBright" min="0" max="255" value="255" oninput="V('tBrV',this.value);cmd('/text/brightness '+this.value)"><span class="val" id="tBrV">255</span>
  </div>
  <div class="row">
    <label>Particles</label><input type="range" id="pBright" min="0" max="255" value="255" oninput="V('pBrV',this.value);cmd('/particles/brightness '+this.value)"><span class="val" id="pBrV">255</span>
  </div>
</div>

<!-- ── Colour ───────────────────────────── -->
<div class="card">
  <h2>Colour</h2>
  <div class="row">
    <label>Text</label><input type="color" id="tCol" value="#ffffff" onchange="sendCol('/display/1/color',this.value)">
    <label>Particles</label><input type="color" id="pCol" value="#6464ff" onchange="sendCol('/particles/color',this.value)">
  </div>
</div>

<!-- ── Text ──────────────────────────────── -->
<div class="card">
  <h2>Text</h2>
  <div class="row">
    <input type="text" id="txt" placeholder="Enter text…">
    <button onclick="pushTxt()">Push</button>
    <button onclick="cmd('/text/pop')">Pop</button>
    <button onclick="cmd('/text/clear')">Clear</button>
  </div>
</div>

<!-- ── Scroll ───────────────────────────── -->
<div class="card">
  <h2>Scroll</h2>
  <div class="row">
    <label>Speed (ms/px)</label><input type="range" id="sSpd" min="5" max="200" value="50" oninput="V('sSpdV',this.value);cmd('/scrollspeed '+this.value)"><span class="val" id="sSpdV">50</span>
  </div>
  <div class="row">
    <label class="toggle"><input type="checkbox" id="sCont" onchange="cmd('/scrollcontinuous '+(this.checked?1:0))">Continuous</label>
  </div>
</div>

<!-- ── Particles ────────────────────────── -->
<div class="card">
  <h2>Particle Physics</h2>
  <label class="toggle"><input type="checkbox" id="spCol" onchange="sendP()">Speed Colour</label>
  <div class="row"><label>Count</label><input type="range" id="pCnt" min="1" max="64" value="6" oninput="V('pCntV',this.value);sendP()"><span class="val" id="pCntV">6</span></div>
  <div class="row"><label>Render (ms)</label><input type="range" id="pRms" min="5" max="100" value="20" oninput="V('pRmsV',this.value);sendP()"><span class="val" id="pRmsV">20</span></div>
  <div class="row"><label>Substep (ms)</label><input type="range" id="pSub" min="1" max="50" value="20" oninput="V('pSubV',this.value);sendP()"><span class="val" id="pSubV">20</span></div>
  <div class="row"><label>Gravity</label><input type="range" id="pGrv" min="0" max="50" step="0.5" value="18" oninput="V('pGrvV',this.value);sendP()"><span class="val" id="pGrvV">18</span></div>
  <label class="toggle"><input type="checkbox" id="pGrvEn" checked onchange="sendP()">Gravity On</label>
  <div class="row"><label>Elasticity</label><input type="range" id="pEl" min="0" max="1" step="0.01" value="0.92" oninput="V('pElV',this.value);sendP()"><span class="val" id="pElV">0.92</span></div>
  <div class="row"><label>Wall Elast.</label><input type="range" id="pWe" min="0" max="1" step="0.01" value="0.78" oninput="V('pWeV',this.value);sendP()"><span class="val" id="pWeV">0.78</span></div>
  <div class="row"><label>Damping</label><input type="range" id="pDmp" min="0.99" max="1" step="0.0001" value="0.9998" oninput="V('pDmpV',this.value);sendP()"><span class="val" id="pDmpV">0.9998</span></div>
  <div class="row"><label>Radius</label><input type="range" id="pRad" min="0.1" max="3" step="0.05" value="0.45" oninput="V('pRadV',this.value);sendP()"><span class="val" id="pRadV">0.45</span></div>
  <div class="row">
    <label>Render Style</label>
    <select id="pSty" onchange="sendP()">
      <option value="0">Point</option><option value="1">Square</option>
      <option value="2">Circle</option><option value="3">Text</option>
      <option value="4" selected>Glow</option>
    </select>
  </div>
  <div class="row"><label>Glow &sigma;</label><input type="range" id="pSig" min="0.2" max="4" step="0.1" value="1.2" oninput="V('pSigV',this.value);sendP()"><span class="val" id="pSigV">1.2</span></div>
  <div class="row"><label>Wavelength</label><input type="range" id="pWav" min="0" max="8" step="0.1" value="0" oninput="V('pWavV',this.value);sendP()"><span class="val" id="pWavV">0</span></div>
  <div class="row"><label>Temperature</label><input type="range" id="pTmp" min="0" max="50" step="0.5" value="0" oninput="V('pTmpV',this.value);sendP()"><span class="val" id="pTmpV">0</span></div>
  <div class="row"><label>Attract</label><input type="range" id="pAtt" min="-20" max="20" step="0.5" value="0" oninput="V('pAttV',this.value);sendP()"><span class="val" id="pAttV">0</span></div>
  <div class="row"><label>Attract Range</label><input type="range" id="pAtR" min="0.5" max="10" step="0.1" value="3" oninput="V('pAtRV',this.value);sendP()"><span class="val" id="pAtRV">3</span></div>
</div>

<!-- ── Actions ──────────────────────────── -->
<div class="card">
  <h2>Actions</h2>
  <div class="row">
    <button onclick="cmd('/clearall')">Clear All</button>
    <button onclick="cmd('/defaults')">Reset Defaults</button>
    <button onclick="cmd('/save')">Save NVS</button>
  </div>
</div>

<div id="log"></div>

<script>
function V(id,v){document.getElementById(id).textContent=v}
function log(s){let l=document.getElementById('log');l.textContent+=s+'\n';l.scrollTop=l.scrollHeight}
let _t=null;
function cmd(c){
  clearTimeout(_t);
  _t=setTimeout(()=>{
    fetch('/cmd?q='+encodeURIComponent(c))
      .then(r=>r.text()).then(t=>log('> '+c))
      .catch(e=>log('ERR: '+e));
  },40);
}
function sendCol(addr,hex){
  let r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);
  cmd(addr+' '+r+' '+g+' '+b);
}
function pushTxt(){
  let t=document.getElementById('txt').value.trim();
  if(!t)return;
  cmd('/text/push "'+t+'"');
  document.getElementById('txt').value='';
}
function sendP(){
  let g=e=>''+document.getElementById(e).value;
  let c=e=>document.getElementById(e).checked?'1':'0';
  cmd('/display/1/particles '+g('pCnt')+' '+g('pRms')+' '+g('pGrv')+' '+g('pEl')+' '+g('pWe')
    +' '+g('pRad')+' '+g('pSty')+' '+g('pSig')+' '+g('pTmp')
    +' '+g('pAtt')+' '+g('pAtR')+' '+c('pGrvEn')+' '+g('pSub')+' '+g('pDmp')+' '+g('pWav')
    +' '+c('spCol'));
}
</script></body></html>)rawhtml");
        return p;
    }
};
