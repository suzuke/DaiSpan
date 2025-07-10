#pragma once

#include <Arduino.h>

// 簡化調試界面 HTML 常量
const char DEBUG_HTML_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>調試</title>
<style>body{font-family:Arial;margin:10px;background:#f0f0f0;font-size:14px}
.panel{background:white;padding:10px;margin:10px 0;border-radius:5px}
.btn{background:#007cba;color:white;border:none;padding:5px 10px;margin:2px;border-radius:3px;cursor:pointer}
.status{padding:5px 0;border-bottom:1px solid #eee}
#logContainer{background:#f8f8f8;border:1px solid #ddd;height:200px;overflow-y:auto;padding:5px;font-family:monospace;font-size:12px}
</style></head><body>
<h1>DaiSpan 調試</h1>
<div class="panel"><h3>系統狀態</h3>
<div class="status">記憶體: <span id="freeHeap">-</span></div>
<div class="status">HomeKit: <span id="homeKitStatus">-</span></div>
</div>
<div class="panel"><h3>HomeKit狀態</h3>
<div class="status">電源: <span id="power">-</span></div>
<div class="status">模式: <span id="mode">-</span></div>
<div class="status">溫度: <span id="temp">-</span></div>
<div class="status">風扇: <span id="fan">-</span></div>
</div>
<div class="panel"><h3>測試</h3>
<button class="btn" onclick="sendCommand('diagnostics')">診斷</button>
<button class="btn" onclick="clearLogs()">清除</button>
<button class="btn" onclick="toggleSerialLog()">串口日誌</button>
</div>
<div class="panel"><h3>日誌</h3>
<div style="margin-bottom:5px">
<label><input type="checkbox" id="showSerial" checked> 串口日誌</label>
<label><input type="checkbox" id="showDebug" checked> 調試日誌</label>
</div>
<div id="logContainer">連接中...</div>
</div>
<script>
let ws=null;
function connect(){
const wsUrl='ws://'+window.location.hostname+':8081';
ws=new WebSocket(wsUrl);
ws.onopen=()=>{sendCommand('get_status')};
ws.onmessage=(e)=>{
try{const d=JSON.parse(e.data);
if(d.type=='system_status'){
document.getElementById('freeHeap').textContent=(d.free_heap/1024).toFixed(1)+'KB';
document.getElementById('homeKitStatus').textContent=d.homekit_initialized?'OK':'ERROR';
}else if(d.type=='homekit_status'&&d.thermostat){
document.getElementById('power').textContent=d.thermostat.power?'ON':'OFF';
document.getElementById('mode').textContent=d.thermostat.mode;
document.getElementById('temp').textContent=d.thermostat.target_temp+'°C';
document.getElementById('fan').textContent=d.thermostat.fan_speed;
}else if(d.type=='log'&&document.getElementById('showDebug').checked){
addLogEntry(d.data,'debug');
}else if(d.type=='serial_log'&&document.getElementById('showSerial').checked){
addLogEntry(d.data,'serial');
}else if(d.type=='serial_log_history'&&d.logs){
for(let log of d.logs)addLogEntry(log,'serial');
}}catch(e){}
};
ws.onclose=()=>setTimeout(connect,3000);
}
function sendCommand(cmd,params={}){
if(ws&&ws.readyState===1)ws.send(JSON.stringify({command:cmd,...params}));
}
function addLogEntry(data,type){
const c=document.getElementById('logContainer');
const color=type==='serial'?'#008000':'#0066cc';
c.innerHTML+=`<span style="color:${color}">${data}</span><br>`;
c.scrollTop=c.scrollHeight;
if(c.children.length>100)c.removeChild(c.firstChild);
}
function clearLogs(){
document.getElementById('logContainer').innerHTML='';
}
function toggleSerialLog(){
const enabled=document.getElementById('showSerial').checked;
sendCommand('set_serial_log_level',{level:enabled?2:0});
}
connect();
setInterval(()=>{if(ws&&ws.readyState===1)sendCommand('get_status')},10000);
</script></body></html>
)HTML";

class DebugWebClient {
public:
    // 獲取調試界面的 HTML
    static String getDebugHTML() {
        return String(DEBUG_HTML_TEMPLATE);
    }
    
private:
    // HTML 模板組件
    static String getHTMLHeader();
    static String getCSS();
    static String getJavaScript();
    static String getMainInterface();
};