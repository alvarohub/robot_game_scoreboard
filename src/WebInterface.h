#pragma once
// ═══════════════════════════════════════════════════════════════
//  WebInterface — serves an HTML control panel over the active network
//
//  WiFi/Ethernet bring-up is owned by OSCHandler.
//  This class only manages HTTP routes and request handling.
// ═══════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// Forward declaration — the command executor is in OSCHandler
class OSCHandler;

class WebInterface {
public:
  WebInterface(OSCHandler& osc)
    : _osc(osc), _server(WEB_PORT), _running(false), _routesConfigured(false) {}

    bool isRunning() const { return _running; }

  /// Start only the HTTP server, assuming WiFi/AP is already up.
  bool startServer() {
    _ensureRoutes();
    if (_running) return true;
    _server.begin();
    _running = true;
    Serial.println("Web UI started");
    return true;
  }

  /// Stop only the HTTP server and keep WiFi as-is.
  void stopServer() {
    if (!_running) return;
    _server.stop();
    _running = false;
    Serial.println("Web UI stopped");
  }

    /// Call every loop() when running.
    void update() {
        if (_running) _server.handleClient();
    }

    const char* ssid() const { return WIFI_AP_SSID; }

private:
    OSCHandler& _osc;
    WebServer   _server;
    bool        _running;
    bool        _routesConfigured;

    void _ensureRoutes() {
      if (_routesConfigured) return;
      _server.on("/",       [this]() { _handleRoot(); });
      _server.on("/cmd",    [this]() { _handleCmd();  });
      _server.on("/status", [this]() { _handleStatus(); });
      _server.onNotFound(   [this]() { _server.send(404, "text/plain", "Not found"); });
      _routesConfigured = true;
    }

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
      p.reserve(22000);
        p += F(R"rawhtml(<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Scoreboard Remote</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#101820;color:#f2f0ea;padding:12px;max-width:980px;margin:auto}
h1{text-align:center;color:#ffd166;font-size:1.5em;margin:10px 0}
p.lead{text-align:center;color:#9ad1d4;font-size:.9em;margin:0 0 14px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:10px}
.card{background:#1a2a33;border:1px solid #29434e;border-radius:10px;padding:12px;margin:0}
.wide{grid-column:1 / -1}
.card h2{font-size:.92em;color:#9ad1d4;margin-bottom:8px;border-bottom:1px solid #29434e;padding-bottom:4px}
label{display:block;font-size:.8em;margin:4px 0 2px;color:#aaa}
input[type=range]{width:100%}
input[type=text]{width:100%;padding:6px;background:#0f1b22;color:#f2f0ea;border:1px solid #35515f;border-radius:4px;font-size:.85em}
input[type=color]{width:40px;height:28px;border:none;background:none;cursor:pointer;vertical-align:middle}
button{background:#1d4e5f;color:#f2f0ea;border:1px solid #4b7c87;border-radius:5px;padding:7px 12px;cursor:pointer;font-size:.8em;margin:2px}
button:active{background:#2c6c82}
.row{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
.row>*{flex:1;min-width:0}
.stackInput{flex:3 1 320px}
.val{color:#ffd166;font-size:.8em;min-width:34px;text-align:right;flex:0 0 auto}
.toggle{display:inline-flex;align-items:center;gap:4px;font-size:.8em}
.toggle input{width:auto}
.toggleLabel{display:flex;align-items:center;gap:8px;min-width:120px;flex:0 0 120px}
.toggleLabel input{width:auto;flex:0 0 auto}
.toggleLabel span{flex:0 1 auto}
.subhead{font-size:.76em;letter-spacing:.08em;text-transform:uppercase;color:#87b7bc;margin:12px 0 6px}
.controlRow{display:grid;grid-template-columns:minmax(120px,150px) 1fr auto 72px;gap:6px;align-items:center;margin:6px 0}
.controlRow .stretch{width:100%}
.controlTail{font-size:.76em;color:#87b7bc;text-align:left}
.compact{flex:0 0 auto}
.labelOnly{display:block;font-size:.8em;margin:0;color:#aaa}
select{background:#0f1b22;color:#f2f0ea;border:1px solid #35515f;border-radius:4px;padding:6px;font-size:.85em}
.status{background:#132028;border:1px solid #284451;border-radius:10px;padding:10px 12px;margin-bottom:12px;color:#c7edf0;font-size:.86em}
.hint{font-size:.76em;color:#87b7bc;margin-top:6px}
#log{background:#0f1b22;color:#9df19d;font-size:.72em;height:110px;overflow-y:auto;padding:6px;border-radius:6px;margin-top:10px;font-family:monospace;white-space:pre-wrap}
@media (max-width:720px){
  .controlRow{grid-template-columns:1fr}
  .toggleLabel{min-width:0;flex:1 1 auto}
}
</style></head><body>
<h1>&#127918; Scoreboard Remote</h1>
<p class="lead">AP control page for animations, text, and particles</p>
<div class="status" id="netStatus">Loading status…</div>
<div class="grid">

<div class="card">
  <h2>Target</h2>
  <div class="row">
    <label>Display</label>
    <select id="dispSel" onchange="refreshStatus()">
      <option value="1">1</option><option value="2">2</option><option value="3">3</option>
      <option value="4">4</option><option value="5">5</option><option value="6">6</option>
    </select>
  </div>
  <div class="row">
    <button onclick="dispCmd('/clear')">Clear Display</button>
    <button onclick="refreshStatus()">Refresh Status</button>
  </div>
  <div class="row">
    <label>Refresh Speed</label>
    <input type="range" id="refreshMs" min="5" max="100" value="20" oninput="sliderInput('refreshMs','refreshMsV',this.value);sendP()">
    <span class="val" id="refreshMsV">20</span>
  </div>
  <div class="hint">Use one page to target any of the six scoreboard displays.</div>
</div>

<div class="card">
  <h2>Animation</h2>
  <div class="row">
    <label>Bank</label>
    <select id="animSlot" onchange="assignAnimation(this.value)"></select>
  </div>
  <div class="row">
    <label class="toggle"><input type="checkbox" id="animRun" onchange="setAnimationRunning(this.checked)">Running</label>
  </div>
  <div class="hint" id="animState">Loaded in display: default | stopped</div>
</div>

<div class="card wide">
  <h2>Text</h2>
  <div class="row">
    <label class="toggle"><input type="checkbox" id="textEn" checked onchange="dispCmd('/text/enable '+(this.checked?1:0))">Text layer on</label>
    <select id="mode" onchange="setMode(this.value)">
      <option value="text">Text (static)</option>
      <option value="scroll_up">Scroll Up</option>
      <option value="scroll_down">Scroll Down</option>
    </select>
  </div>
  <div class="hint">Comma-separated values replace the whole text stack in one shot.</div>
  <div class="row">
    <input type="text" id="txt" placeholder="hello player, the game, will start, GO">
  </div>
  <div class="row">
    <button onclick="applyTextStack()">Load List</button>
    <button onclick="dispCmd('/text/clear')">Clear List</button>
  </div>
  <div class="row">
    <label>Brightness</label><input type="range" id="tBright" min="0" max="255" value="255" oninput="sliderInput('tBright','tBrV',this.value);debouncedDispCmd('textBrightness','/text/brightness '+this.value)"><span class="val" id="tBrV">255</span>
  </div>
  <div class="row">
    <label>Text Colour</label><input type="color" id="tCol" value="#ffffff" onchange="sendCol('/display/'+disp()+'/color',this.value)">
  </div>
</div>

<div class="card wide">
  <h2>Particles</h2>
  <div class="row">
    <label class="toggle"><input type="checkbox" id="partEn" onchange="dispCmd('/particles/enable '+(this.checked?1:0))">Enable</label>
    <label class="toggle"><input type="checkbox" id="pRun" onchange="setParticlesRunning(this.checked)">Run Simulation</label>
    <button class="compact" onclick="dispCmd('/screen2particles')">Render To Particles</button>
  </div>
  <div class="subhead">Dynamics</div>
  <div class="controlRow">
    <label>Count</label>
    <input class="stretch" type="range" id="pCnt" min="0" max="128" value="6" oninput="sliderInput('pCnt','pCntV',this.value);sendP()">
    <span class="val" id="pCntV">6</span>
    <span class="controlTail">particles</span>
  </div>
  <div class="controlRow">
    <label>Compute Interval</label>
    <input class="stretch" type="range" id="pStep" min="5" max="60" value="20" oninput="sliderInput('pStep','pStepV',this.value);sendP()">
    <span class="val" id="pStepV">20</span>
    <span class="controlTail">ms</span>
  </div>
  <div class="controlRow">
    <label>Elasticity</label>
    <input class="stretch" type="range" id="pEl" min="0" max="1" step="0.01" value="0.92" oninput="sliderInput('pEl','pElV',this.value);sendP()">
    <span class="val" id="pElV">0.92</span>
    <span class="controlTail">particle + wall</span>
  </div>
  <div class="controlRow">
    <label>Damping</label>
    <input class="stretch" type="range" id="pDamp" min="0.90" max="1" step="0.0005" value="0.9998" oninput="sliderInput('pDamp','pDampV',this.value);sendP()">
    <span class="val" id="pDampV">0.9998</span>
    <span class="controlTail">motion</span>
  </div>
  <div class="controlRow">
    <label>Temperature</label>
    <input class="stretch" type="range" id="pTmp" min="0" max="50" step="0.5" value="0" oninput="sliderInput('pTmp','pTmpV',this.value);sendP()">
    <span class="val" id="pTmpV">0</span>
    <span class="controlTail">jitter</span>
  </div>
  <div class="controlRow">
    <label class="toggleLabel"><input type="checkbox" id="pGrvEn" checked onchange="sendP()"><span>Gravity</span></label>
    <input class="stretch" type="range" id="pGrv" min="0" max="50" step="0.5" value="18" oninput="sliderInput('pGrv','pGrvV',this.value);sendP()">
    <span class="val" id="pGrvV">18</span>
    <span class="controlTail">scale</span>
  </div>
  <div class="controlRow">
    <label class="toggleLabel"><input type="checkbox" id="pAttEn" checked onchange="sendP()"><span>Attract</span></label>
    <input class="stretch" type="range" id="pAtt" min="-20" max="20" step="0.5" value="0" oninput="sliderInput('pAtt','pAttV',this.value);sendP()">
    <span class="val" id="pAttV">0</span>
    <span class="controlTail">strength</span>
  </div>
  <div class="controlRow">
    <label class="labelOnly">Range</label>
    <input class="stretch" type="range" id="pAtR" min="0.5" max="10" step="0.1" value="3" oninput="sliderInput('pAtR','pAtRV',this.value);sendP()">
    <span class="val" id="pAtRV">3</span>
    <span class="controlTail">range</span>
  </div>
  <div class="controlRow">
    <label class="toggleLabel"><input type="checkbox" id="pSprEn" onchange="sendP()"><span>Spring</span></label>
    <input class="stretch" type="range" id="pSpr" min="-20" max="20" step="0.5" value="0" oninput="sliderInput('pSpr','pSprV',this.value);sendP()">
    <span class="val" id="pSprV">0</span>
    <span class="controlTail">strength</span>
  </div>
  <div class="controlRow">
    <label class="labelOnly">Range</label>
    <input class="stretch" type="range" id="pSprR" min="0.5" max="20" step="0.1" value="5" oninput="sliderInput('pSprR','pSprRV',this.value);sendP()">
    <span class="val" id="pSprRV">5</span>
    <span class="controlTail">range</span>
  </div>
  <div class="controlRow">
    <label class="toggleLabel"><input type="checkbox" id="pCoulEn" onchange="sendP()"><span>Coulomb</span></label>
    <input class="stretch" type="range" id="pCoul" min="-20" max="20" step="0.5" value="0" oninput="sliderInput('pCoul','pCoulV',this.value);sendP()">
    <span class="val" id="pCoulV">0</span>
    <span class="controlTail">strength</span>
  </div>
  <div class="controlRow">
    <label class="labelOnly">Range</label>
    <input class="stretch" type="range" id="pCoulR" min="0.5" max="30" step="0.1" value="10" oninput="sliderInput('pCoulR','pCoulRV',this.value);sendP()">
    <span class="val" id="pCoulRV">10</span>
    <span class="controlTail">range</span>
  </div>
  <div class="controlRow">
    <label class="toggleLabel"><input type="checkbox" id="pColEn" checked onchange="sendP()"><span>Collision</span></label>
    <input class="stretch" type="range" id="pRad" min="0.1" max="2.0" step="0.05" value="0.45" oninput="sliderInput('pRad','pRadV',this.value);sendP()">
    <span class="val" id="pRadV">0.45</span>
    <span class="controlTail">radius</span>
  </div>
  <div class="subhead">Rendering</div>
  <div class="row">
    <label>Render Style</label>
    <select id="pSty" onchange="sendP()">
      <option value="0">Point</option><option value="1">Square</option>
      <option value="2">Circle</option><option value="3">Text</option>
      <option value="4" selected>Glow</option>
    </select>
  </div>
  <div class="controlRow">
    <label>Glow Sigma</label>
    <input class="stretch" type="range" id="pSig" min="0.2" max="4" step="0.1" value="1.2" oninput="sliderInput('pSig','pSigV',this.value);sendP()">
    <span class="val" id="pSigV">1.2</span>
    <span class="controlTail">spread</span>
  </div>
  <div class="controlRow">
    <label>Wavelength</label>
    <input class="stretch" type="range" id="pWav" min="0" max="8" step="0.1" value="0" oninput="sliderInput('pWav','pWavV',this.value);sendP()">
    <span class="val" id="pWavV">0</span>
    <span class="controlTail">wave</span>
  </div>
  <div class="row">
    <label>Particle Colour</label><input type="color" id="pCol" value="#6464ff" onchange="sendCol('/display/'+disp()+'/particles/color',this.value)">
    <label>Brightness</label><input type="range" id="pBright" min="0" max="255" value="255" oninput="sliderInput('pBright','pBrV',this.value);debouncedDispCmd('particleBrightness','/particles/brightness '+this.value)"><span class="val" id="pBrV">255</span>
  </div>
  <div class="row">
    <label class="toggle"><input type="checkbox" id="spCol" onchange="sendP()">Speed Colour</label>
  </div>
</div>

</div>

<div id="log"></div>

<script>
function V(id,v){document.getElementById(id).textContent=v}
function log(s){let l=document.getElementById('log');l.textContent+=s+'\n';l.scrollTop=l.scrollHeight}
let _debounce={};
let _displayState=null;
let _refreshTimer=null;
let _activeSliders={};
function disp(){return document.getElementById('dispSel').value;}
function queueRefresh(delay=260){
  clearTimeout(_refreshTimer);
  _refreshTimer=setTimeout(()=>{
    _refreshTimer=null;
    refreshStatus();
  },delay);
}
function requestCommand(c){
  return fetch('/cmd?q='+encodeURIComponent(c))
    .then(async r=>{if(!r.ok) throw new Error(await r.text()); return r.text();})
    .then(()=>{log('> '+c); return c;});
}
function cmd(c,opt={}){
  return requestCommand(c)
    .then(()=>{
      if(opt.refresh===false){
        queueRefresh(opt.refreshDelay||260);
        return null;
      }
      return refreshStatus();
    })
    .catch(e=>log('ERR: '+e));
}
function dispCmd(path,opt){return cmd('/display/'+disp()+path,opt);}
function seq(cmds,opt={}){
  let chain=Promise.resolve();
  cmds.forEach(c=>{ chain=chain.then(()=>requestCommand(c)); });
  return chain.then(()=>{
    if(opt.refresh===false){
      queueRefresh(opt.refreshDelay||260);
      return null;
    }
    return refreshStatus();
  }).catch(e=>log('ERR: '+e));
}
function debouncedCmd(key,c,delay=70){
  clearTimeout(_debounce[key]);
  _debounce[key]=setTimeout(()=>{
    delete _debounce[key];
    cmd(c,{refresh:false,refreshDelay:320});
  },delay);
}
function debouncedDispCmd(key,path,delay=70){
  debouncedCmd(key,'/display/'+disp()+path,delay);
}
function sendCol(addr,hex){
  let r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);
  cmd(addr+' '+r+' '+g+' '+b);
}
function setStatusText(text){document.getElementById('netStatus').textContent=text;}
function rgbToHex(r,g,b){
  let h=v=>Math.max(0,Math.min(255,Number(v)||0)).toString(16).padStart(2,'0');
  return '#'+h(r)+h(g)+h(b);
}
function setSelect(id,value){
  let el=document.getElementById(id),v=String(value);
  if([...el.options].some(o=>o.value===v)) el.value=v;
}
function setSlider(id,value,labelId){
  if(value===undefined||value===null) return;
  let until=_activeSliders[id]||0;
  if(until>Date.now()) return;
  let text=String(value);
  document.getElementById(id).value=text;
  if(labelId) V(labelId,text);
}
function sliderInput(id,labelId,value){
  _activeSliders[id]=Date.now()+700;
  V(labelId,value);
}
function setColor(id,arr){
  if(Array.isArray(arr)&&arr.length===3){
    document.getElementById(id).value=rgbToHex(arr[0],arr[1],arr[2]);
  }
}
function slotLabel(item){
  let name=animationDisplayName(item && item.name, item && item.slot);
  return item.slot+': '+name;
}
function animationDisplayName(name,slot){
  let text=(name||'').trim();
  if(!text || text.toLowerCase()==='empty' || (!slot && text.toLowerCase()==='default')) return 'no script';
  return text;
}
function updateSlotOptions(slots){
  let sel=document.getElementById('animSlot');
  let current=_displayState&&_displayState.animationSlot!==undefined?String(_displayState.animationSlot):(sel.value||'');
  sel.innerHTML='';
  if(!(slots||[]).some(item=>String(item.slot)===current)){
    let stub=document.createElement('option');
    stub.value='';
    stub.textContent=animationDisplayName(_displayState&&_displayState.animationName,_displayState&&_displayState.animationSlot);
    sel.appendChild(stub);
  }
  (slots||[]).forEach(item=>{
    let opt=document.createElement('option');
    opt.value=String(item.slot);
    opt.textContent=slotLabel(item);
    sel.appendChild(opt);
  });
  sel.value=[...sel.options].some(o=>o.value===current)?current:'';
}
function updateAnimationState(state){
  let slot=state&&state.animationSlot?state.animationSlot:0;
  let running=!!(state&&state.animationRunning);
  let name=animationDisplayName(state&&state.animationName,slot);
  let msg='Loaded in display: '+name;
  if(slot){ msg+=' ('+slot+')'; }
  msg+=running?' | running':' | stopped';
  document.getElementById('animState').textContent=msg;
  document.getElementById('animRun').checked=running;
}
function setMode(value){
  if(value==='scroll_up' || value==='scroll_down'){
    seq(['/display/'+disp()+'/mode '+value,'/scrollcontinuous 1']);
    return;
  }
  dispCmd('/mode '+value);
}
function applyDisplayState(state){
  if(!state) return;
  _displayState=state;
  document.getElementById('textEn').checked=!!state.textEnabled;
  document.getElementById('partEn').checked=!!state.particlesEnabled;
  setSelect('mode',state.mode||'text');
  if(Array.isArray(state.textItems)){
    document.getElementById('txt').value=state.textItems.join(', ');
  }
  setSlider('tBright',state.textBrightness,'tBrV');
  setColor('tCol',state.textColor);
  let particles=state.particles||{};
  setSlider('refreshMs',particles.renderMs,'refreshMsV');
  document.getElementById('spCol').checked=!!particles.speedColor;
  document.getElementById('pGrvEn').checked=particles.gravityEnabled!==false;
  document.getElementById('pAttEn').checked=particles.attractEnabled!==false;
  document.getElementById('pSprEn').checked=!!particles.springEnabled;
  document.getElementById('pCoulEn').checked=!!particles.coulombEnabled;
  document.getElementById('pColEn').checked=particles.collisionEnabled!==false;
  document.getElementById('pRun').checked=particles.physicsPaused!==true;
  setSlider('pCnt',particles.count,'pCntV');
  setSlider('pStep',particles.substepMs,'pStepV');
  setSlider('pGrv',particles.gravityScale,'pGrvV');
  setSlider('pEl',particles.elasticity,'pElV');
  setSlider('pDamp',particles.damping,'pDampV');
  setSlider('pRad',particles.radius,'pRadV');
  setSlider('pSig',particles.glowSigma,'pSigV');
  setSlider('pWav',particles.glowWavelength,'pWavV');
  setSlider('pTmp',particles.temperature,'pTmpV');
  setSlider('pAtt',particles.attractStrength,'pAttV');
  setSlider('pAtR',particles.attractRange,'pAtRV');
  setSlider('pSpr',particles.springStrength,'pSprV');
  setSlider('pSprR',particles.springRange,'pSprRV');
  setSlider('pCoul',particles.coulombStrength,'pCoulV');
  setSlider('pCoulR',particles.coulombRange,'pCoulRV');
  setSelect('pSty',particles.renderStyle===undefined?4:particles.renderStyle);
  setColor('pCol',state.particleColor);
  setSlider('pBright',state.particleBrightness,'pBrV');
  updateAnimationState(state);
}
function refreshStatus(){
  return fetch('/status?display='+encodeURIComponent(disp()))
    .then(async r=>{if(!r.ok) throw new Error(await r.text()); return r.json();})
    .then(data=>{
      let wifi=data.wifi||{};
      let state=wifi.apActive ? ('AP '+(wifi.ssid||'')+' @ '+(wifi.ip||'0.0.0.0')) : 'WiFi inactive';
      let anim=data.animating ? 'animating' : 'idle';
      let storage=data.storageReady ? 'storage ready' : 'storage off';
      let clients=typeof wifi.clients === 'number' ? wifi.clients : 0;
      setStatusText(state+'  |  '+clients+' client(s)  |  '+anim+'  |  '+storage);
      applyDisplayState(data.displayState||null);
      updateSlotOptions(data.bankSlots||[]);
      return data;
    })
    .catch(e=>{setStatusText('Status unavailable: '+e);log('ERR: '+e);});
}
function applyTextStack(){
  let t=document.getElementById('txt').value.trim();
  if(!t)return;
  dispCmd('/text/stack "'+t.replace(/"/g,'')+'"');
}
function assignAnimation(slot){
  if(!slot) return Promise.resolve();
  let shouldRun=document.getElementById('animRun').checked;
  let commands=['/display/'+disp()+'/animation '+slot];
  if(shouldRun){ commands.push('/display/'+disp()+'/animation/start'); }
  return seq(commands);
}
function setAnimationRunning(enabled){
  if(enabled){
    if(!_displayState||!_displayState.animationSlot){
      let selected=document.getElementById('animSlot').value;
      if(selected){
        assignAnimation(selected);
        return;
      }
      document.getElementById('animRun').checked=false;
      log('Pick an animation from the bank first.');
      return;
    }
    dispCmd('/animation/start');
    return;
  }
  dispCmd('/animation/stop');
}
function setParticlesRunning(enabled){
  dispCmd('/particles/pause '+(enabled?0:1));
}
function sendPNow(){
  let g=e=>''+document.getElementById(e).value;
  let c=e=>document.getElementById(e).checked?'1':'0';
  cmd('/display/'+disp()+'/particles '+g('pCnt')+' '+g('refreshMs')+' '+g('pGrv')+' '+g('pEl')+' '+g('pEl')
    +' '+g('pRad')+' '+g('pSty')+' '+g('pSig')+' '+g('pTmp')
    +' '+g('pAtt')+' '+g('pAtR')+' '+c('pGrvEn')+' '+g('pStep')+' '+g('pDamp')+' '+g('pWav')
    +' '+c('spCol')+' '+g('pSpr')+' '+g('pSprR')+' '+c('pSprEn')
    +' '+g('pCoul')+' '+g('pCoulR')+' '+c('pCoulEn')
    +' 0 10 0 '+c('pColEn')+' '+c('pAttEn'),
    {refresh:false,refreshDelay:340});
}
function sendP(){
  clearTimeout(_debounce.particles);
  _debounce.particles=setTimeout(()=>{
    delete _debounce.particles;
    sendPNow();
  },60);
}
window.addEventListener('load',()=>{refreshStatus();});
</script></body></html>)rawhtml");
        return p;
    }
};
