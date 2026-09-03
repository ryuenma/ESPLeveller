/*================================================================================
  ESP32-C3 SHIELD LEVELLER — WEB ONLY (no OLED, no buttons, no buzzer)
  --------------------------------------------------------------------------------
  WIRING:
  - ESP32 3V3  -> GY-521 VCC
  - ESP32 GND  -> GY-521 GND
  - ESP32 GPIO5 -> GY-521 SDA
  - ESP32 GPIO6 -> GY-521 SCL

  WEB UI:
  - Connect phone to WiFi: LevellerAP2 / levelup123
  - Open http://192.168.4.1
  - Full UI: timer, tilt bar, threshold, START, CALIBRATE, ABORT, phone sounds

  IDE:
  - Board: ESP32C3 Dev Module
  - USB CDC On Boot: Enabled
================================================================================*/

#include <Wire.h>
#include <MPU6050_tockn.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ---------------- PIN DEFINITIONS ----------------
#define I2C_SDA 1
#define I2C_SCL 0

// ---------------- WIFI ----------------
const char* WIFI_SSID = "LevellerAP2";
const char* WIFI_PASS = "levelup123";

// ---------------- SENSOR & STORAGE ----------------
MPU6050 mpu6050(Wire);
Preferences prefs;
WebServer server(80);

// ---------------- GAME STATES ----------------
enum State { IDLE, COUNTDOWN, PLAYING, GAMEOVER };
State currentState = IDLE;

// ---------------- GAME VARIABLES ----------------
float thresholdAngle = 15.0;
float baseAngleX = 0.0;
float baseAngleY = 0.0;
float lastMaxTilt = 0.0;

unsigned long gameStartTime      = 0;
unsigned long countdownStartTime = 0;
unsigned long countdownDelay     = 0;
unsigned long finalTime          = 0;

// ---------------- FORWARD DECLARATIONS ----------------
void startCountdown();
void calibrateDevice();
void saveSettings();
void handleState();
void setupWebServer();

//================= WEB PAGE =================
const char* HTML_PAGE = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Shield Leveller</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,sans-serif}
  body{background:#111;color:#eee;padding:16px;max-width:480px;margin:auto}
  h1{font-size:1.4rem;text-align:center;margin-bottom:12px}
  #status{text-align:center;padding:8px;border-radius:8px;font-weight:700;margin-bottom:12px;background:#333;font-size:1.1rem}
  #status.PLAYING{background:#063}#status.COUNTDOWN{background:#660}#status.GAMEOVER{background:#600}
  #timer{font-size:3.5rem;text-align:center;font-family:monospace;margin:10px 0}
  #tiltbar{height:22px;border:2px solid #eee;border-radius:11px;overflow:hidden;margin:10px 0 6px}
  #tiltfill{height:100%;width:0%;background:#0af;transition:width 0.1s}
  #tiltfill.warn{background:#fa0}#tiltfill.bad{background:#f33}
  .row{display:flex;gap:8px;align-items:center;justify-content:center;margin:12px 0}
  button{background:#222;color:#eee;border:1px solid #555;border-radius:8px;padding:14px 20px;font-size:1rem;font-weight:600;cursor:pointer}
  button:active{background:#444}
  #startBtn{background:#0af;color:#000;border:none;width:100%;padding:16px;font-size:1.2rem;margin-top:8px}
  #calBtn,#abortBtn{flex:1}
  .small{font-size:.85rem;color:#999;text-align:center}
  #conn{position:fixed;top:4px;right:8px;font-size:.75rem;color:#f66}
  .info{text-align:center;font-size:.8rem;color:#666;margin-top:16px}
</style>
</head>
<body>
<h1>&#x1F6E1; SHIELD LEVELLER</h1>
<div id='conn'></div>
<div id='status'>IDLE</div>
<div id='timer'>00.00</div>
<div id='tiltbar'><div id='tiltfill'></div></div>
<div class='row small'><span id='tilttxt'>Tilt 0.0 / 15 deg</span></div>
<div class='row'>
  <button id='minus'>&#x2796;</button>
  <span id='thrtxt' style='font-weight:700;font-size:1.2rem'>15&deg;</span>
  <button id='plus'>&#x2795;</button>
  <label class='small' style='margin-left:8px'><input type='checkbox' id='snd' checked> sound</label>
</div>
<div class='row'>
  <button id='calBtn'>CALIBRATE</button>
  <button id='abortBtn'>ABORT</button>
</div>
<button id='startBtn'>START</button>
<div class='info'>WiFi: LevellerAP2 &bull; 192.168.4.1</div>
<script>
let last={state:'IDLE'}, lastTick=0, actx=null, thr=15;
const $=id=>document.getElementById(id);
function ctx(){
  if(!actx) actx=new (window.AudioContext||window.webkitAudioContext)();
  if(actx.state==='suspended') actx.resume();
  return actx;
}
function beep(f,d,t,g){
  if(!$('snd').checked) return;
  try{
    t=t||'square'; g=g||0.2;
    const a=ctx(),o=a.createOscillator(),v=a.createGain();
    o.type=t; o.frequency.value=f; v.gain.value=g;
    o.connect(v); v.connect(a.destination);
    o.start(); o.stop(a.currentTime+d/1000);
  }catch(e){}
}
function alarm(){
  if(!$('snd').checked) return;
  try{
    const a=ctx(),o=a.createOscillator(),v=a.createGain();
    o.type='sawtooth'; v.gain.value=.25;
    o.frequency.setValueAtTime(2000,a.currentTime);
    o.frequency.linearRampToValueAtTime(200,a.currentTime+.7);
    o.connect(v); v.connect(a.destination);
    o.start(); o.stop(a.currentTime+.75);
  }catch(e){}
}
function post(u){ fetch(u,{method:'POST',cache:'no-store'}); }
$('startBtn').onclick=()=>{ ctx(); post('/api/start'); };
$('calBtn').onclick =()=>post('/api/calibrate');
$('abortBtn').onclick=()=>post('/api/abort');
$('plus').onclick =()=>post('/api/threshold?val='+(thr+1));
$('minus').onclick=()=>post('/api/threshold?val='+(thr-1));
document.addEventListener('pointerdown',()=>{try{ctx();}catch(e){}},{once:true});
$('snd').onchange=()=>{if($('snd').checked) beep(1500,80);};
function fmt(ms){
  const s=Math.floor(ms/1000), c=Math.floor((ms%1000)/10);
  return String(s).padStart(2,'0')+'.'+String(c).padStart(2,'0');
}
setInterval(async ()=>{
  try{
    const s=await (await fetch('/api/state',{cache:'no-store'})).json();
    $('conn').textContent='';
    if(s.state==='PLAYING'&&last.state==='COUNTDOWN') beep(2000,200);
    if(s.state==='GAMEOVER'&&last.state!=='GAMEOVER') alarm();
    if(s.state==='PLAYING'&&s.tilt>s.thr*0.75){
      const frac=(s.tilt-s.thr*0.75)/(s.thr*0.25);
      const iv=500-frac*440;
      const now=Date.now();
      if(now-lastTick>iv){ beep(2500,15); lastTick=now; }
    }
    last=s; thr=s.thr;
    const st=$('status'); st.textContent=s.state; st.className=s.state;
    $('timer').textContent=fmt(s.t)+(s.state==='GAMEOVER'?' FINAL':'');
    const pct=Math.max(0,Math.min(100,s.tilt/s.thr*100));
    const f=$('tiltfill'); f.style.width=pct+'%';
    f.className=s.tilt>s.thr?'bad':(s.tilt>s.thr*0.75?'warn':'');
    $('tilttxt').textContent='Tilt '+s.tilt.toFixed(1)+' / '+s.thr+' deg';
    $('thrtxt').textContent=s.thr+'\u00B0';
    $('startBtn').textContent=s.state==='IDLE'?'START':(s.state==='GAMEOVER'?'BACK TO MENU':'...');
  }catch(e){$('conn').textContent='DISCONNECTED';}
},100);
</script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  // Verify MPU6050 is alive
  Wire.beginTransmission(0x68);
  if (Wire.endTransmission() != 0) {
    Serial.println("MPU6050 NOT FOUND — check wiring");
    for (;;);
  }
  Serial.println("MPU6050 found at 0x68");

  mpu6050.begin();
  Serial.println("Calibrating gyro (keep still)...");
  mpu6050.calcGyroOffsets(true);
  Serial.println("Gyro calibration done");

  prefs.begin("leveller", false);
  thresholdAngle = prefs.getFloat("threshold", 15.0);
  baseAngleX     = prefs.getFloat("baseX", 0.0);
  baseAngleY     = prefs.getFloat("baseY", 0.0);

  setupWebServer();
  Serial.println("Ready — connect to WiFi and open 192.168.4.1");
}

void loop() {
  server.handleClient();

  mpu6050.update();
  float tiltX = abs(mpu6050.getAngleX() - baseAngleX);
  float tiltY = abs(mpu6050.getAngleY() - baseAngleY);
  float maxTilt = (tiltX > tiltY) ? tiltX : tiltY;
  lastMaxTilt = maxTilt;

  if (currentState == COUNTDOWN) {
    if (millis() - countdownStartTime >= countdownDelay) {
      currentState  = PLAYING;
      gameStartTime = millis();
    }
  }
  else if (currentState == PLAYING) {
    if (maxTilt > thresholdAngle) {
      currentState = GAMEOVER;
      finalTime = millis() - gameStartTime;
    }
  }
}

// ---------------- WEB SERVER ----------------
void setupWebServer() {
  Serial.println("Starting WiFi AP...");
  WiFi.mode(WIFI_AP);
  delay(200);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_15dBm);

  bool apOk = WiFi.softAP(WIFI_SSID, WIFI_PASS, 1, 0, 4);
  Serial.print("softAP result: ");
  Serial.println(apOk ? "OK" : "FAILED");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  MDNS.begin("leveller");

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", HTML_PAGE);
  });

  server.on("/api/state", HTTP_GET, handleState);

  server.on("/api/start", HTTP_POST, []() {
    if (currentState == IDLE) startCountdown();
    else if (currentState == GAMEOVER) currentState = IDLE;
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/calibrate", HTTP_POST, []() {
    calibrateDevice();
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/abort", HTTP_POST, []() {
    if (currentState == COUNTDOWN || currentState == PLAYING) currentState = IDLE;
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/threshold", HTTP_POST, []() {
    if (server.hasArg("val") && currentState == IDLE) {
      int v = server.arg("val").toInt();
      if (v >= 5 && v <= 45) {
        thresholdAngle = v;
        saveSettings();
      }
    }
    server.send(200, "text/plain", "ok");
  });

  server.begin();
  Serial.print("Web UI ready: http://");
  Serial.println(WiFi.softAPIP());
}

void handleState() {
  unsigned long t = 0;
  if (currentState == PLAYING) t = millis() - gameStartTime;
  else if (currentState == GAMEOVER) t = finalTime;

  String st;
  switch (currentState) {
    case IDLE:      st = "IDLE";      break;
    case COUNTDOWN: st = "COUNTDOWN"; break;
    case PLAYING:   st = "PLAYING";   break;
    default:        st = "GAMEOVER";  break;
  }

  String json = "{\"state\":\"" + st +
                "\",\"tilt\":" + String(lastMaxTilt, 1) +
                ",\"thr\":" + String((int)thresholdAngle) +
                ",\"t\":" + String(t) + "}";
  server.send(200, "application/json", json);
}

// ---------------- GAME FUNCTIONS ----------------
void startCountdown() {
  currentState = COUNTDOWN;
  countdownDelay = random(2000, 5001);
  countdownStartTime = millis();
}

void calibrateDevice() {
  mpu6050.update();
  baseAngleX = mpu6050.getAngleX();
  baseAngleY = mpu6050.getAngleY();
  saveSettings();
  currentState = IDLE;
}

void saveSettings() {
  prefs.putFloat("threshold", thresholdAngle);
  prefs.putFloat("baseX", baseAngleX);
  prefs.putFloat("baseY", baseAngleY);
}