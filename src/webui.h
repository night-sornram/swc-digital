// webui.h — single-page config UI served from PROGMEM
//
// Tabs are segmented: shared Status/WiFi/Display/Update plus the Usage tab.
// The config JSON mirrors the nested Settings layout: { ..shared.., usage:{...} }.
#pragma once
#include <Arduino.h>

static const char WEBUI_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmallTV</title>
<style>
:root{--bg:#0e1116;--card:#171c24;--mut:#8b96a5;--fg:#e6edf3;--acc:#3fb950;--acc2:#2f81f7;--red:#f85149;--bd:#262d38}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg);font-size:15px}
header{padding:14px 16px;border-bottom:1px solid var(--bd);display:flex;align-items:center;gap:10px}
header h1{font-size:17px;margin:0;font-weight:600}
header .dot{width:9px;height:9px;border-radius:50%;background:var(--mut)}
header .dot.ok{background:var(--acc)}
nav{display:flex;gap:4px;padding:8px;overflow-x:auto;border-bottom:1px solid var(--bd);position:sticky;top:0;background:var(--bg);z-index:5}
nav button{background:none;border:0;color:var(--mut);padding:8px 12px;border-radius:8px;font-size:14px;cursor:pointer;white-space:nowrap}
nav button.active{background:var(--card);color:var(--fg)}
main{padding:16px;max-width:680px;margin:0 auto}
.tab{display:none}.tab.active{display:block}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.04em;color:var(--mut);margin:0 0 12px}
label{display:block;margin:10px 0 4px;font-size:13px;color:var(--mut)}
input[type=text],input[type=password],input[type=number],input[type=url],select{
 width:100%;padding:9px 10px;background:#0b0e13;border:1px solid var(--bd);border-radius:8px;color:var(--fg);font-size:15px}
input[type=range]{width:100%}
.row{display:flex;gap:10px}.row>*{flex:1}
.chk{display:flex;align-items:center;gap:8px;margin:8px 0}
.chk input{width:18px;height:18px}
.chk label{margin:0;color:var(--fg);font-size:14px}
button.btn{background:var(--acc);color:#04130a;border:0;padding:10px 16px;border-radius:9px;font-size:15px;font-weight:600;cursor:pointer}
button.btn.sec{background:#222b36;color:var(--fg)}
button.btn.danger{background:var(--red);color:#1a0606}
button.btn:disabled{opacity:.5}
.muted{color:var(--mut);font-size:13px}
table{width:100%;border-collapse:collapse}
td{padding:6px 4px}
.symrow input{margin:0}
.kv{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid var(--bd)}
.kv:last-child{border:0}.kv b{font-weight:600}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#0b0e13;border:1px solid var(--bd);padding:10px 16px;border-radius:10px;opacity:0;transition:.3s;pointer-events:none}
.toast.show{opacity:1}
.net{padding:8px;border:1px solid var(--bd);border-radius:8px;margin:4px 0;cursor:pointer;display:flex;justify-content:space-between}
.net:hover{border-color:var(--acc2)}
.bar{height:8px;background:#0b0e13;border-radius:6px;overflow:hidden;margin-top:8px}
.bar>div{height:100%;width:0;background:var(--acc2);transition:.2s}
small.hint{display:block;color:var(--mut);margin-top:4px;font-size:12px}
.chip{display:inline-block;margin-left:8px;padding:2px 8px;border-radius:10px;font-size:11px;font-weight:600;letter-spacing:.03em;background:var(--acc2);color:#fff;vertical-align:middle}
.usage-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:6px}
.usage-grid>div{background:#0b0e13;border:1px solid var(--bd);border-radius:8px;padding:10px;display:flex;flex-direction:column;gap:4px}
.usage-grid>div>span:first-child{color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.04em}
.usage-grid>div>b{font-size:22px;font-weight:600}
.usage-grid>div>span:last-child{font-size:12px}
</style></head>
<body>
<header><span id="dot" class="dot"></span><h1>SmallTV</h1><span id="chip" class="chip" style="display:none"></span><span id="hi" class="muted"></span></header>
<nav>
 <button data-t="status" class="active">Status</button>
 <button data-t="wifi">WiFi</button>
 <button data-t="display">Display</button>
 <button data-t="update">Update</button>
</nav>
<main>
 <!-- Unpaired banner (3.1+): shows pair instructions when /api/identity reports paired=false. -->
 <div id="pairHint" style="display:none;background:#1a2a36;padding:10px;border-radius:6px;margin-bottom:12px">
  <b>This device is unpaired.</b> Join its setup Wi-Fi (password shown on the device screen),
  open <code id="pairUrl"></code>, and run <code>wifi_usage_service.py pair --url &lt;url&gt;</code> on your Mac.
  Once paired, the browser will prompt for username <code>admin</code> and your pairkey.
 </div>
 <!-- STATUS -->
 <section id="status" class="tab active">
  <div class="card"><h2>Device</h2><div id="statusBox" class="muted">Loading...</div></div>
 </section>

 <!-- WIFI -->
 <section id="wifi" class="tab">
  <div class="card"><h2>Saved networks</h2>
   <button class="btn sec" onclick="scan()">Scan networks</button>
   <div id="scanList"></div>
   <table id="wifiTable"></table>
   <button class="btn sec" style="margin-top:10px" onclick="addWifi()">+ Add network</button>
   <div style="margin-top:14px"><button class="btn" onclick="saveWifi()">Save &amp; connect (reboots)</button></div>
   <small class="hint">2.4&nbsp;GHz only. Up to 4 networks; at boot the device joins the strongest one it can see. Tap a scan result to fill a row. Leave a password blank to keep the stored one.</small>
  </div>
  <div class="card"><h2>Device name</h2>
   <label>Hostname</label><input id="hostname" type="text" placeholder="smalltv">
   <small class="hint">Reachable as <code>http://&lt;hostname&gt;.local</code> via mDNS. Running several SmallTVs? Give each its own name (<code>smalltv-desk</code>, <code>smalltv-shelf</code>) so browsers and the clawdmeter daemon's <code>--push-to</code> reach the right device. Saving a new name reboots the device.</small>
  </div>
  <div class="card"><h2>Setup hotspot (AP)</h2>
   <label>AP name</label><input id="apSsid" type="text">
   <label>AP password <span class="muted">(blank = open, else min 8 chars)</span></label>
   <input id="apPass" type="text" placeholder="(unchanged)">
   <small class="hint">The AP appears when no WiFi is configured or the connection fails.</small>
  </div>
 </section>

 <!-- DISPLAY (shared) -->
 <section id="display" class="tab">
  <div class="card"><h2>Screen</h2>
   <label>Brightness: <span id="brVal"></span>%</label>
   <input id="brightness" type="range" min="0" max="100" oninput="brVal.textContent=this.value">
   <div class="chk"><input id="autoBrightness" type="checkbox"><label>Auto-brightness (light sensor on A0)</label></div>
   <label>Orientation</label>
   <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option>
    <option value="2">180&deg;</option><option value="3">270&deg;</option></select>
   <div class="chk"><input id="backlightInverted" type="checkbox"><label>Backlight is active-low (try if screen stays dark)</label></div>
  </div>
  <div class="card"><h2>Display mode</h2>
   <label>What this device shows</label>
   <select id="usage-mode">
    <option value="codex">Codex</option>
    <option value="zai">Z.ai</option>
    <option value="auto">Auto (rotate)</option>
    <option value="vitals">Vitals (Mac)</option>
    <option value="weather">Weather + Clock</option>
   </select>
   <label>Rotation seconds per screen (2&ndash;3600)</label>
   <input id="usage-rotate" type="number" min="2" max="3600" step="1">
  </div>
  <div class="card" id="weather-card" style="display:none">
   <h2>Weather location</h2>
   <label>City label (short, on screen)</label>
   <input type="text" id="weather-city" maxlength="6" placeholder="BKK">
   <label>City name</label>
   <input type="text" id="weather-city-name" maxlength="24" placeholder="Bangkok">
   <label>Latitude</label>
   <input type="text" id="weather-lat" placeholder="13.7563">
   <label>Longitude</label>
   <input type="text" id="weather-lon" placeholder="100.5018">
   <small class="hint">The title bar shows the city label. The Mac adapter uses the lat/lon from <code>wifi-usage.toml</code> to fetch weather.</small>
  </div>
  <div class="card"><h2>Clock &amp; night mode</h2>
   <label>Timezone</label>
   <select id="tz"></select>
   <div class="muted" id="clockNow" style="margin:8px 0">Clock: -</div>
   <div class="chk"><input id="nightEnabled" type="checkbox"><label>Dim or blank the screen on a nightly schedule</label></div>
   <div class="row">
    <div><label>From</label><input id="nightStart" type="time"></div>
    <div><label>To</label><input id="nightEnd" type="time"></div>
   </div>
   <label>Night brightness: <span id="nlVal"></span>% <span class="muted">(0 = screen off)</span></label>
   <input id="nightLevel" type="range" min="0" max="100" oninput="nlVal.textContent=this.value">
   <small class="hint">Needs internet once to set the clock over NTP (no on-screen clock, this just drives the schedule). While the window is active it overrides the brightness and auto-brightness above. Times are local to the selected timezone; DST is handled automatically. After a reboot the schedule resumes once the clock re-syncs, so the screen may show normal brightness for a few seconds.</small>
  </div>
  <div class="card">
   <button class="btn sec" id="usage-refresh" type="button">Refresh status</button>
   <small class="hint">Refresh reads live data from the device. Status tab shows full details.</small>
  </div>
 </section>

 <!-- UPDATE -->
 <section id="update" class="tab">
  <div class="card"><h2>Update from GitHub</h2>
   <div class="muted">Installed: <b id="fwVer">-</b></div>
   <div style="margin-top:10px">
    <button class="btn sec" onclick="checkUpdate()" id="chkBtn">Check for latest</button>
    <button class="btn" style="margin-left:8px" onclick="selfUpdate()" id="ghUpBtn" disabled>Update now</button>
   </div>
   <div id="ghMsg" class="muted" style="margin-top:8px"></div>
   <small class="hint">Pulls the newest release straight from <a id="repoLink" href="https://github.com/giovi321/smalltv-mod/releases" target="_blank">the GitHub repo</a>. HTTPS OTA is tight on the ESP8266; if it fails, use the manual upload below.</small>
  </div>
  <div class="card"><h2>Manual update (OTA)</h2>
   <input id="fw" type="file" accept=".bin">
   <div style="margin-top:12px"><button class="btn" onclick="upload()" id="upBtn">Upload &amp; flash</button></div>
   <div class="bar"><div id="upBar"></div></div>
   <div id="upMsg" class="muted" style="margin-top:8px"></div>
   <small class="hint">Upload a firmware.bin from the <a href="https://github.com/giovi321/smalltv-mod/releases" target="_blank">releases page</a> or a local build. The device reboots when done.</small>
  </div>
  <div class="card"><h2>Settings backup</h2>
   <button class="btn sec" onclick="location.href='/api/export'">Export settings</button>
   <input id="cfgFile" type="file" accept=".json,application/json" style="margin-top:10px">
   <div style="margin-top:10px"><button class="btn" onclick="importCfg()">Import &amp; reboot</button></div>
   <small class="hint">The export is the device's <code>config.json</code>, including WiFi passwords in clear text; treat the file accordingly. Import applies everything and reboots.</small>
  </div>
  <div class="card"><h2>Maintenance</h2>
   <button class="btn sec" onclick="reboot()">Reboot</button>
   <button class="btn danger" style="margin-left:8px" onclick="factory()">Factory reset</button>
  </div>
 </section>
</main>

<div style="text-align:center;padding:0 0 16px"><button class="btn" onclick="saveAll()">Save settings</button></div>
<div style="text-align:center;padding:0 0 24px;font-size:12px">
 <a id="footRepo" href="https://github.com/giovi321/smalltv-mod" target="_blank" style="color:var(--acc2);text-decoration:none">GitHub: giovi321/smalltv-mod</a>
 <span id="footVer" class="muted"></span>
</div>
<div id="toast" class="toast"></div>

<script>
var C={};
function $(id){return document.getElementById(id)}
// null-safe field helpers: a lean build removes some feature tabs entirely
function sv(id,v){var e=$(id);if(e)e.value=(v!=null?v:'')}
function sc(id,v){var e=$(id);if(e)e.checked=!!v}
function gv(id){var e=$(id);return e?e.value:''}
function gc(id){var e=$(id);return e?e.checked:false}
function toast(m){var t=$('toast');t.textContent=m;t.classList.add('show');setTimeout(function(){t.classList.remove('show')},2200)}
function j(url,opt){return fetch(url,opt).then(function(r){return r.json()})}

// tabs
document.querySelectorAll('nav button').forEach(function(b){b.onclick=function(){
 document.querySelectorAll('nav button').forEach(function(x){x.classList.remove('active')});
 document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('active')});
 b.classList.add('active');$(b.dataset.t).classList.add('active');
}});

// field groups by their location in the nested config (ticker-only fields removed)
// IANA -> POSIX TZ. The device stores/uses the POSIX rule; this map lives in the
// browser so the firmware carries no tz database (same idea as the cash finder).
var TZMAP={
 '':'UTC0','UTC':'UTC0',
 'Europe/London':'GMT0BST,M3.5.0/1,M10.5.0','Europe/Dublin':'GMT0IST,M3.5.0/1,M10.5.0',
 'Europe/Lisbon':'WET0WEST,M3.5.0/1,M10.5.0',
 'Europe/Rome':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Paris':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Berlin':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Madrid':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Amsterdam':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Brussels':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Zurich':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Vienna':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Warsaw':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Prague':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Stockholm':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Oslo':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Copenhagen':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Athens':'EET-2EEST,M3.5.0/3,M10.5.0/4','Europe/Helsinki':'EET-2EEST,M3.5.0/3,M10.5.0/4',
 'Europe/Bucharest':'EET-2EEST,M3.5.0/3,M10.5.0/4','Europe/Kyiv':'EET-2EEST,M3.5.0/3,M10.5.0/4',
 'Europe/Istanbul':'<+03>-3','Europe/Moscow':'MSK-3',
 'America/New_York':'EST5EDT,M3.2.0,M11.1.0','America/Toronto':'EST5EDT,M3.2.0,M11.1.0',
 'America/Chicago':'CST6CDT,M3.2.0,M11.1.0','America/Denver':'MST7MDT,M3.2.0,M11.1.0',
 'America/Phoenix':'MST7','America/Los_Angeles':'PST8PDT,M3.2.0,M11.1.0',
 'America/Anchorage':'AKST9AKDT,M3.2.0,M11.1.0','America/Sao_Paulo':'<-03>3',
 'America/Mexico_City':'CST6','America/Bogota':'<-05>5','America/Argentina/Buenos_Aires':'<-03>3',
 'Asia/Dubai':'<+04>-4','Asia/Karachi':'PKT-5','Asia/Kolkata':'IST-5:30',
 'Asia/Dhaka':'<+06>-6','Asia/Bangkok':'<+07>-7','Asia/Jakarta':'WIB-7',
 'Asia/Shanghai':'CST-8','Asia/Hong_Kong':'HKT-8','Asia/Singapore':'<+08>-8',
 'Asia/Taipei':'CST-8','Asia/Tokyo':'JST-9','Asia/Seoul':'KST-9',
 'Australia/Perth':'AWST-8','Australia/Sydney':'AEST-10AEDT,M10.1.0,M4.1.0/3',
 'Australia/Adelaide':'ACST-9:30ACDT,M10.1.0,M4.1.0/3','Australia/Brisbane':'AEST-10',
 'Pacific/Auckland':'NZST-12NZDT,M9.5.0,M4.1.0/3','Pacific/Honolulu':'HST10'};
function fillTz(){var s=$('tz');if(!s)return;var keys=Object.keys(TZMAP).filter(function(k){return k!==''});
 keys.sort();s.innerHTML='<option value="">UTC</option>'+keys.map(function(k){return '<option value="'+k+'">'+k+'</option>'}).join('');}

function hideFeat(name){
 var b=document.querySelector('nav button[data-t="'+name+'"]'); if(b)b.remove();
 var sec=$(name); if(sec)sec.remove();
}
function loadConfig(){return j('/api/config').then(function(c){C=c;
 var f=c.features||{}; ['usage'].forEach(function(k){if(f[k]===false)hideFeat(k)});
 // shared
 ['apSsid','apPass','hostname'].forEach(function(k){$(k).value=c[k]!=null?c[k]:''});
 renderWifi(c.wifi||(c.staSsid?[{ssid:c.staSsid,passSet:c.staPassSet}]:[]));
 $('brightness').value=c.brightness; $('brVal').textContent=c.brightness;
 $('rotation').value=c.rotation;
 $('autoBrightness').checked=!!c.autoBrightness;
 $('backlightInverted').checked=!!c.backlightInverted;
 // header chip = which chip this firmware was built for
 var chipName={esp8266:'ESP8266',esp32c2:'ESP32-C2',esp32:'ESP32'}[c.chip]||'';
 var chE=$('chip'); if(chE&&chipName){chE.textContent=chipName;chE.style.display='inline-block';}
 // clock slice
 fillTz(); var ck=c.clock||{};
 if(ck.tz && !(ck.tz in TZMAP)){var _ts=$('tz'); if(_ts){var _o=document.createElement('option');_o.value=ck.tz;_o.textContent=ck.tz;_ts.appendChild(_o);}}
 sv('tz',ck.tz||''); sc('nightEnabled',!!ck.nightEnabled);
 sv('nightStart',ck.nightStart||'22:00'); sv('nightEnd',ck.nightEnd||'07:00');
 sv('nightLevel',ck.nightLevel!=null?ck.nightLevel:0); $('nlVal')&&($('nlVal').textContent=(ck.nightLevel!=null?ck.nightLevel:0));
 var ap=$('apPass'); if(ap)ap.placeholder=c.apPassSet?'(unchanged)':'(open)';
 // usage slice: mode + rotation (default + per-provider).
 if(c.usage){
  sv('usage-mode',c.usage.mode||'auto');
  sv('usage-rotate',c.usage.autoRotateSec!=null?c.usage.autoRotateSec:30);
 }
 var w = c.weather || {};
 if($('weather-city')) sv('weather-city', w.city || 'BKK');
 if($('weather-city-name')) sv('weather-city-name', w.cityName || 'Bangkok');
 if($('weather-lat')) sv('weather-lat', w.lat !== undefined ? w.lat : 13.7563);
 if($('weather-lon')) sv('weather-lon', w.lon !== undefined ? w.lon : 100.5018);
})}

function esc(s){return (''+(s==null?'':s)).replace(/[<>&"]/g,function(c){return {'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]})}

function collect(){
 var o={
  brightness:parseInt(gv('brightness'))||0,
  rotation:parseInt(gv('rotation')),
  autoBrightness:gc('autoBrightness'),
  backlightInverted:gc('backlightInverted'),
  hostname:gv('hostname'), apSsid:gv('apSsid'), apPass:gv('apPass'),
  wifi:collectWifi()};
 // clock slice
 if($('tz')){var _tzn=gv('tz'); var _tzp=(_tzn in TZMAP)?TZMAP[_tzn]:((C.clock&&C.clock.tz===_tzn&&C.clock.tzPosix)?C.clock.tzPosix:'UTC0');
  o.clock={tz:_tzn,tzPosix:_tzp,
  nightEnabled:gc('nightEnabled'),nightStart:gv('nightStart')||'22:00',
  nightEnd:gv('nightEnd')||'07:00',nightLevel:parseInt(gv('nightLevel'))||0};}
 // usage slice (3.0.0): mode + autoRotateSec live here, not in the top-level mode.
 if($('usage-mode')){o.usage={mode:gv('usage-mode')||'auto',
  autoRotateSec:parseInt(gv('usage-rotate'))||30};}
 if($('weather-card')){o.weather={city:gv('weather-city')||'BKK',cityName:gv('weather-city-name')||'Bangkok',lat:parseFloat(gv('weather-lat'))||13.7563,lon:parseFloat(gv('weather-lon'))||100.5018};}
 return o;
}
function saveAll(){j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collect())})
 .then(function(r){toast(r.reboot?'Saved — rebooting...':'Saved');if(r.reboot)setTimeout(function(){location.reload()},6000);loadStatus()})}

function saveWifi(){
 var o={wifi:collectWifi()};
 j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).then(function(){
  toast('Saved, rebooting to connect...');j('/api/reboot',{method:'POST'});
 });
}

// wifi networks (up to 4)
function renderWifi(arr){var t=$('wifiTable');if(!t)return;t.innerHTML='';arr.forEach(addWifiRow);if(!arr.length)addWifiRow({})}
function addWifiRow(o){var t=$('wifiTable');var tr=document.createElement('tr');tr.className='symrow';
 tr.innerHTML='<td style="width:44%"><input class="ws" type="text" autocomplete="off" placeholder="SSID" value="'+esc(o.ssid||'')+'"></td>'+
  '<td><input class="wp" type="password" autocomplete="off" placeholder="'+(o.passSet?'(unchanged)':'password')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';
 t.appendChild(tr);}
function addWifi(){if(document.querySelectorAll('#wifiTable tr').length>=4){toast('Max 4');return}addWifiRow({})}
function collectWifi(){var w=[];document.querySelectorAll('#wifiTable tr').forEach(function(tr){
 var s=tr.querySelector('.ws').value.trim();if(!s)return;
 var e={ssid:s};var p=tr.querySelector('.wp').value;if(p)e.pass=p;w.push(e);});return w}
function scanPick(ssid){var rows=document.querySelectorAll('#wifiTable tr');var tr=null;
 for(var i=0;i<rows.length;i++){if(!rows[i].querySelector('.ws').value.trim()){tr=rows[i];break}}
 if(!tr){if(rows.length>=4){toast('Max 4');return}addWifiRow({});tr=$('wifiTable').lastChild}
 tr.querySelector('.ws').value=ssid;tr.querySelector('.wp').focus();}

// wifi scan
function scan(){$('scanList').innerHTML='<div class="muted">Scanning...</div>';
 j('/api/scan').then(function(l){var h='';l.sort(function(a,b){return b.rssi-a.rssi});
  l.forEach(function(n){h+='<div class="net" onclick="scanPick(this.dataset.s)" data-s="'+
   esc(n.ssid)+'"><span>'+(n.enc?'🔒 ':'')+esc(n.ssid)+'</span><span class="muted">'+n.rssi+' dBm</span></div>'});
  $('scanList').innerHTML=h||'<div class="muted">No networks found</div>';})}

// identity (3.1+): reads the PUBLIC /api/identity to drive the unpaired hint.
// Do NOT use /api/status here — that route is Digest-gated once paired, so the
// hint could never be cleared by re-fetching status. /api/identity is always open.
function loadIdentity(){
 return j('/api/identity').then(function(i){
  var ph = $('pairHint'); if (!ph) return;
  // Show banner only if device reports unpaired AND we have no cached
  // browser credentials that work. If the browser already has valid Basic
  // credentials cached, /api/status below will succeed and hide the banner
  // (paired flag can transiently flicker false during auto-unpair recovery).
  if (i.paired) { ph.style.display = 'none'; return; }
  // Device says unpaired. Probe /api/status: if it returns 200, the browser
  // has working credentials → device is actually paired (identity lied) →
  // hide the banner.
  fetch('/api/status', {cache:'no-store'}).then(function(r){
   ph.style.display = (r.status === 200) ? 'none' : 'block';
   if (ph.style.display === 'block' && $('pairUrl')) {
    $('pairUrl').textContent = 'http://' + location.host + '/';
   }
  }).catch(function(){ ph.style.display = 'block'; });
 }).catch(function(){});
}

// status
function loadStatus(){j('/api/status').then(function(s){
 $('dot').className='dot'+(s.connected?' ok':'');
 $('hi').textContent=s.mode==='ap'?'setup mode':(s.ip||'');
 var cn=$('clockNow'); if(cn){var ne=!!(C.clock&&C.clock.nightEnabled);var ns=s.night?'  · night mode active':(s.nightHeld?'  · night mode waiting for NTP':'');cn.textContent=!ne?'Clock: NTP runs only when night mode is on':('Clock: '+(s.synced?(s.time||'synced')+(s.tz?' ('+s.tz+')':''):'waiting for NTP...')+ns);}
 var fw=$('fwVer'); if(fw)fw.textContent=s.fw+' '+s.version;
 // Surface the result of a boot-time GitHub update (ESP8266) once on first load,
 // so a failure that happened across the reboot is visible even if the original
 // Update tab was closed. Don't clobber an in-progress check/update message.
 if(!window._otaShown){window._otaShown=1;var gm=$('ghMsg');if(gm&&!gm.textContent&&s.updateMsg&&s.updateMsg!=='updating...')gm.textContent='Last update: '+s.updateMsg}
 var fv=$('footVer'); if(fv)fv.textContent=' v'+s.version;
 if(s.repo){var rl=$('repoLink'); if(rl)rl.href=s.repo+'/releases'; var fr=$('footRepo'); if(fr)fr.href=s.repo;}
 $('statusBox').innerHTML=
  kv('Firmware',s.fw+' '+s.version)+kv('Mode',s.mode.toUpperCase())+
  kv('Network',s.ssid||'-')+kv('IP',s.ip||'-')+kv('mDNS','http://'+(C.hostname||'smalltv')+'.local')+
  kv('Signal',s.rssi?s.rssi+' dBm':'-')+
  kv('Free heap',s.heap+' B')+kv('Uptime',fmtUp(s.uptime))+kv('Last reset',s.reset||'-');
})}
function kv(k,v){return '<div class="kv"><span class="muted">'+k+'</span><b>'+v+'</b></div>'}
function fmtUp(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
 return (d?d+'d ':'')+(h?h+'h ':'')+m+'m'}

// Usage overview (3.0.0): fills the Codex/Z.ai cells from /api/status.usage.
// read-only: never fire a provider API from the UI.
function loadUsageOverview(){
 j('/api/status').then(function(s){
  var u=s.usage||{}; var ps=u.providers||[];
  function byName(n){for(var i=0;i<ps.length;i++){if(ps[i].provider===n)return ps[i]}return null}
  function fmtM(min){
   // Compact d/h/m: 5711 -> "3d 23h 11m", 95 -> "1h 35m", 5 -> "5m".
   if(min==null||min<0)return null;
   var d=Math.floor(min/1440), h=Math.floor((min%1440)/60), m=min%60;
   if(d>0)return d+'d '+h+'h '+m+'m';
   if(h>0)return h+'h '+m+'m';
   return m+'m';
  }
  function fill(pre,p){
   if(!p)return;
   var f=p.five_hour||{}, w=p.weekly||{};
   var ageS=fmtM(p.age_sec!=null&&p.age_sec>=0?Math.floor(p.age_sec/60):null)||'--';
   $(pre+'-5h').textContent=f.used_pct!=null?f.used_pct+'%':'N/A';
   $(pre+'-5h-reset').textContent='RESET '+(fmtM(f.reset_min)||'--');
   $(pre+'-wk').textContent=w.used_pct!=null?w.used_pct+'%':'N/A';
   $(pre+'-wk-reset').textContent='RESET '+(fmtM(w.reset_min)||'--');
   $(pre+'-age').textContent=ageS;
   $(pre+'-state').textContent=p.stale?'STALE':'LIVE';
  }
  fill('codex',byName('codex'));
  fill('zai',byName('zai'));
 }).catch(function(){});
}

// GitHub self-update
function checkUpdate(){$('ghMsg').textContent='Checking GitHub...';$('chkBtn').disabled=true;
 j('/api/checkupdate').then(function(u){$('chkBtn').disabled=false;
  if(!u.ok){$('ghMsg').textContent='Check failed: '+(u.error||'unknown');return}
  if(u.newer){$('ghMsg').innerHTML='Version <b>'+u.latest+'</b> is available (installed '+u.current+').';$('ghUpBtn').disabled=false}
  else{$('ghMsg').textContent='Up to date ('+u.current+').';$('ghUpBtn').disabled=true}
 }).catch(function(){$('chkBtn').disabled=false;$('ghMsg').textContent='Check failed'})}
function selfUpdate(){if(!confirm('Download and flash the latest release from GitHub? The device reboots if it succeeds.'))return;
 $('ghUpBtn').disabled=true;$('chkBtn').disabled=true;
 $('ghMsg').textContent='Downloading and flashing... this can take a couple of minutes and the device may reboot twice.';
 // Installed version, read synchronously from the already-loaded status so the
 // poller below can recognise success (new version) without racing a fetch.
 var cur=(($('fwVer').textContent||'').trim().split(' ').pop())||'';
 j('/api/selfupdate',{method:'POST'}).then(function(){
  var n=0;var t=setInterval(function(){n++;
   j('/api/status').then(function(s){
    if(cur&&s.version&&s.version!==cur){clearInterval(t);$('ghMsg').textContent='Updated to '+s.version+'.';$('chkBtn').disabled=false;return}
    var m=s.updateMsg||'';
    if(m&&m!=='starting...'&&m!=='updating...'){clearInterval(t);$('ghMsg').textContent='Update failed: '+m;$('chkBtn').disabled=false}
   }).catch(function(){});
   if(n>100)clearInterval(t);
  },3000);
 }).catch(function(){$('ghMsg').textContent='Could not start update';$('chkBtn').disabled=false})}

// settings backup
function importCfg(){var f=$('cfgFile').files[0];if(!f){toast('Pick a config .json first');return}
 var r=new FileReader();
 r.onload=function(){var txt=r.result;
  try{JSON.parse(txt)}catch(e){toast('Not valid JSON');return}
  if(!confirm('Apply this configuration and reboot?'))return;
  j('/api/import',{method:'POST',headers:{'Content-Type':'application/json'},body:txt})
   .then(function(){toast('Imported, rebooting...');setTimeout(function(){location.reload()},8000)})
   .catch(function(){toast('Import failed')});
 };
 r.readAsText(f);}

// maintenance
function reboot(){if(confirm('Reboot device?'))j('/api/reboot',{method:'POST'}).then(function(){toast('Rebooting...')})}
function factory(){if(confirm('Erase ALL settings and reboot?'))j('/api/factory',{method:'POST'}).then(function(){toast('Reset, rebooting...')})}

// OTA
function upload(){var f=$('fw').files[0];if(!f){toast('Pick a .bin first');return}
 var fd=new FormData();fd.append('firmware',f,f.name);
 var x=new XMLHttpRequest();x.open('POST','/update');
 $('upBtn').disabled=true;
 x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);$('upBar').style.width=p+'%';$('upMsg').textContent='Uploading '+p+'%'}};
 x.onload=function(){$('upBtn').disabled=false;if(x.status==200){$('upMsg').textContent='Done. Rebooting...';$('upBar').style.width='100%';setTimeout(function(){location.reload()},9000)}else{$('upMsg').textContent='Failed: '+x.responseText}};
 x.onerror=function(){$('upBtn').disabled=false;$('upMsg').textContent='Upload error'};
 x.send(fd);
}

function syncWeatherCard(){var wm=gv('usage-mode');var wc=$('weather-card');if(wc)wc.style.display=(wm==='weather')?'':'none'}

loadIdentity();
loadConfig().then(function(){syncWeatherCard()}).then(loadStatus).then(loadUsageOverview);
setInterval(loadStatus,5000);
// Refresh button (Usage tab): read-only — never fire a provider API from the UI.
var _ur=$('usage-refresh'); if(_ur)_ur.onclick=loadUsageOverview;
var _um=$('usage-mode'); if(_um)_um.addEventListener('change',syncWeatherCard);
</script>
</body></html>)HTMLPAGE";
