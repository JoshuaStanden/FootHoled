/**
 * FootHoled H1 browser UI prototype (development aid only).
 *
 * The browser implementation is independent of the ESP32 firmware. Its
 * synthetic module list and localStorage preferences are for navigation
 * testing, not evidence of hardware detection or NVS persistence.
 *
 * Keep the Recon / Analyze / Expand hierarchy aligned with docs/UI_NAVIGATION.md.
 * Escape user-facing strings before injecting HTML into the document.
 */
"use strict";
const app=document.getElementById('app');
const defaults={theme:'dark',brightness:70,privacy:true,mask:4,sound:false,developer:true};
let settings={...defaults,...JSON.parse(localStorage.getItem('footholed-ui-alpha')||'{}')};
let path=['Home'];
const recon=['Wi-Fi','Bluetooth / BLE','GNSS','NFC / RFID','Infrared','Radio tools'];
const analyze=['Observations','Sessions','Compare','Statistics','Saved logs'];
const expand=['Discover modules','Uninitialized modules','Installed modules','Manual setup','Diagnostics'];
const utility=['Display','Privacy','Storage','Device','Developer','About'];
// Explicitly synthetic inventory; never treat these entries as discovered hardware.
let demoModules=[{name:'MCP23017 GPIO expander',bus:'I²C',status:'Uninitialized'},{name:'INA228 power monitor',bus:'I²C',status:'Uninitialized'},{name:'GY-GPS6MV2 GNSS',bus:'UART',status:'Not detected'}];
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const save=()=>localStorage.setItem('footholed-ui-alpha',JSON.stringify(settings));
const btn=(title,action)=>`<button data-go="${esc(action||title)}">${esc(title)} →</button>`;
const list=items=>`<div class="list">${items.map(i=>btn(i)).join('')}</div>`;
const banner='<p class="tag">DEMONSTRATION ONLY · NO HARDWARE SCANNING</p>';
function goto(name){path.push(name);render()}
function home(){path=['Home'];render()}
function back(){if(path.length>1)path.pop();render()}
function card(title,body){return `<div class="card"><strong>${esc(title)}</strong><div class="muted">${body}</div></div>`}
// Central renderer for the browser-only prototype. Real LVGL screens live in
// firmware/src/ui.cpp and should be updated separately when UX changes.
function render(){let name=path.at(-1);let html='';
 if(name==='Home') html=`<h1>FootHoled</h1><p>Recon. Analyze. Expand.</p><div class="tiles"><button class="tile" data-go="Recon"><span class="ico">◎</span>RECON<small>Discover</small></button><button class="tile" data-go="Analyze"><span class="ico">◈</span>ANALYZE<small>Inspect</small></button><button class="tile" data-go="Expand"><span class="ico">▣</span>EXPAND<small>Modules</small></button></div>${card('Development build','Touch navigation works. Wireless functions are not connected.')}`;
 else if(name==='Recon')html=`<h1>Recon</h1><p>Discover and collect observations.</p>${banner}${list(recon)}`;
 else if(name==='Analyze')html=`<h1>Analyze</h1><p>Inspect captured information.</p>${banner}${list(analyze)}`;
 else if(name==='Expand')html=`<h1>Expand</h1><p>Discover, initialize and test modules.</p>${banner}${list(expand)}${card('Module status',`${demoModules.filter(m=>m.status==='Initialized').length} initialized · ${demoModules.filter(m=>m.status==='Uninitialized').length} awaiting setup (synthetic)`)}`;
 else if(name==='Settings')html=`<h1>Settings</h1><p>Local prototype preferences.</p>${list(utility)}`;
 else if(name==='Display')html=`<h1>Display</h1><div class="row"><label for="bright">Brightness: <span id="brightval">${settings.brightness}</span>%</label></div><input id="bright" type="range" min="10" max="100" value="${settings.brightness}"><div class="row"><label for="theme">Theme</label><select id="theme"><option value="dark">Dark</option><option value="light">Light (preview)</option></select></div><p class="muted">Prototype preference only; does not control the Waveshare backlight.</p>`;
 else if(name==='Privacy')html=`<h1>Privacy</h1><div class="row"><label for="privacy">Mask identifiers in UI</label><input type="checkbox" id="privacy" ${settings.privacy?'checked':''}></div><label for="mask">Leading characters masked: <strong id="maskval">${settings.mask}</strong></label><input id="mask" type="range" min="1" max="15" value="${settings.mask}"><h2>Live synthetic preview</h2><div id="preview"></div><p class="muted">Raw logs would require separate protection; masking does not encrypt data.</p>`;
 else if(name==='Discover modules')html=`<h1>Discover modules</h1>${banner}<p>Actual bus probing is not implemented.</p><button id="discover">Run simulated discovery</button><div id="discovery"></div>`;
 else if(name==='Uninitialized modules'||name==='Installed modules'||name==='Diagnostics')html=`<h1>${esc(name)}</h1>${banner}${demoModules.filter(m=>name==='Uninitialized modules'?m.status==='Uninitialized':name==='Installed modules'?m.status==='Initialized':true).map((m,i)=>card(m.name,`${m.bus} · ${m.status} · synthetic entry`)+`<button data-module="${esc(m.name)}">${name==='Diagnostics'?'View diagnostics':'Configure'} →</button>`).join('')||card('No entries','Nothing in this demonstration category.')}`;
 else if(name.startsWith('Module:')){let m=demoModules.find(x=>x.name===name.slice(7));html=`<h1>${esc(m?.name||'Module')}</h1>${banner}${card('Interface',`${esc(m?.bus||'Unknown')} · ${esc(m?.status||'Unknown')}`)}<p>Demo setup changes only the simulated registry.</p><div class="btnrow"><button id="initmodule">Simulate initialization</button><button id="resetmodule">Reset demo state</button></div>`}
 else if(name==='Manual setup')html=`<h1>Manual module setup</h1>${banner}<p>Choose a known device. SPI/UART modules often cannot be automatically discovered.</p>${list(demoModules.map(m=>'Module:'+m.name))}`;
 else if(name==='Device')html=`<h1>Device</h1>${card('Board','Waveshare ESP32-S3-Touch-LCD-3.5B (target; not connected)')}${card('Firmware','FootHoled H1 UI Alpha · browser demo')}`;
 else if(name==='About')html=`<h1>About</h1>${card('FootHoled','Recon. Analyze. Expand.')}<p>Modular offline-first handheld research platform. Experimental development branch.</p>`;
 else if(name==='Developer')html=`<h1>Developer</h1><p>Simulated data is explicitly marked and never comes from live RF scans.</p><button id="resetprefs">Reset prototype settings</button>`;
 else if(name==='Storage')html=`<h1>Storage</h1>${card('MicroSD','Driver pending. No files were read or written.')}`;
 else html=`<h1>${esc(name)}</h1>${banner}${card('Not implemented','This screen is navigable, but its hardware service has not been integrated.')}<button data-go="Settings">Open settings →</button>`;
 app.innerHTML=html;
 app.querySelectorAll('[data-go]').forEach(b=>b.addEventListener('click',()=>goto(b.dataset.go)));
 app.querySelectorAll('[data-module]').forEach(b=>b.addEventListener('click',()=>goto('Module:'+b.dataset.module)));
 if(name==='Privacy'){
  const update=()=>{let m=settings.mask;let mask=s=>settings.privacy?'•'.repeat(Math.min(m,s.length))+s.slice(m):s;
   document.getElementById('preview').innerHTML=card('SSID',esc(mask('FootHoledDemoWiFi')))+card('BSSID',esc(mask('AA:BB:CC:11:22:33')))+card('IPv4',esc(mask('192.0.2.55')))+card('NFC',esc(mask('04AABBCCDDEE')))};
  document.getElementById('mask').oninput=e=>{settings.mask=+e.target.value;document.getElementById('maskval').textContent=settings.mask;save();update()};
  document.getElementById('privacy').onchange=e=>{settings.privacy=e.target.checked;save();update()};update();
 }
 if(name==='Display'){document.getElementById('bright').oninput=e=>{settings.brightness=+e.target.value;document.getElementById('brightval').textContent=settings.brightness;save()};document.getElementById('theme').value=settings.theme;document.getElementById('theme').onchange=e=>{settings.theme=e.target.value;save();document.querySelector('.device').style.filter=settings.theme==='light'?'brightness(1.25)':'none'}}
 if(name==='Discover modules')document.getElementById('discover').onclick=()=>document.getElementById('discovery').innerHTML=demoModules.map(m=>card(m.name,`${m.bus} · ${m.status} · SIMULATED`)).join('');
 if(name.startsWith('Module:')){let m=demoModules.find(x=>x.name===name.slice(7));document.getElementById('initmodule').onclick=()=>{m.status='Initialized';render()};document.getElementById('resetmodule').onclick=()=>{m.status='Uninitialized';render()}}
 if(name==='Developer')document.getElementById('resetprefs').onclick=()=>{settings={...defaults};save();render()};
}
document.getElementById('settings').onclick=()=>goto('Settings');document.getElementById('back').onclick=back;document.getElementById('home').onclick=home;render();
