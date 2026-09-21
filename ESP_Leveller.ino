/*================================================================================
  ESP LEVELLER — WEB ONLY (no OLED, no buttons, no buzzer)
  Board: LuatOS ESP32C3-CORE (and cheap clones thereof)
  --------------------------------------------------------------------------------
  WIRING (sensor pod, either module works — auto-detected at boot):
  - GY-521 (MPU6050) or GY-BMI160: VCC->3V3, GND->GND
  - Board IO4 -> SDA  (board's dedicated I2C_SDA pin)
  - Board IO5 -> SCL  (board's dedicated I2C_SCL pin)
  NOTE: marketplace "GY-LSM6DS3" boards often actually carry a BMI160
  (chips are pin-compatible, sellers mix them up). Firmware detects
  the real chip by ID register, so the label doesn't matter.

  BOARD-SPECIFIC NOTES (LuatOS ESP32C3-CORE):
  - External SPI flash uses GPIO11-17 (DIO mode). DO NOT use GPIO11-17
    for peripherals. Onboard LEDs: D4=GPIO12, D5=GPIO13 (active-high);
    D4 is used here as the game-status LED.
  - USB is a CH343 USB-UART bridge: in Arduino IDE set
    "USB CDC On Boot: Disabled" and "Flash Mode: DIO".
  - Upload speed / serial log: 115200 works everywhere (921600 is the
    LuatOS factory default but clone CH343 drivers can be flaky at it).
  - Strapping pins: GPIO9 (BOOT) must not be pulled low at power-on;
    GPIO8 should not be pulled low externally. GPIO18/19 are USB D-/D+
    on the USB-native variant — avoid.

  WEB UI:
  - Connect phone to WiFi: ESPLevellerAP / levelup123
  - Open http://192.168.4.1 (or http://leveller.local)
  - State is PUSHED via Server-Sent Events (/api/events) at 10 Hz

  LIBRARIES (Library Manager):
  - "ESP Async WebServer" (ESP32Async/mathieucarbou fork) + "Async TCP"

  HARDWARE NOTE:
  - TX power capped at 8.5dBm: harmless on the LuatOS board's beefier
    LDO, and keeps clone boards (whose regulators/antenna matching are
    hit-or-miss) stable too.

  IDE:
  - Board: ESP32C3 Dev Module
  - USB CDC On Boot: Disabled   <- CH343 board, critical!
  - Flash Mode: DIO             <- external 2-wire flash, critical!
================================================================================*/

#include <Wire.h>
#include <MPU6050_tockn.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <ESPAsyncWebServer.h>

// ---------------- SERIAL / DEBUG OUTPUT ----------------
// v1.0.0: serial logging is OFF by default (release builds stay quiet on
// a headless kiosk). Flip ENABLE_SERIAL to 1 during bring-up to watch the
// boot sequence / AP IP / sensor detection on the Serial Monitor.
#ifndef ENABLE_SERIAL
#define ENABLE_SERIAL 0
#endif

#if ENABLE_SERIAL
#define SLOG_BEGIN()      Serial.begin(115200)
#define SLOGF(...)        Serial.printf(__VA_ARGS__)
#define SLOG(msg)         Serial.println(msg)
#define SLOG_P(msg)       Serial.print(msg)
#else
#define SLOG_BEGIN()
#define SLOGF(...)
#define SLOG(msg)
#define SLOG_P(msg)
#endif

// ---------------- PIN DEFINITIONS (LuatOS ESP32C3-CORE) ----------------
#define I2C_SDA 4   // board's dedicated I2C_SDA (mux function per LuatOS pin table)
#define I2C_SCL 5   // board's dedicated I2C_SCL
#define LED_STATUS 12   // D4 onboard LED, active-high (GPIO12/13 only safe LEDs; 11-17 are flash pins)

// ---------------- WIFI ----------------
const char* WIFI_SSID = "ESPLevellerAP";
const char* WIFI_PASS = "levelup123";

// ---------------- SENSOR & STORAGE ----------------
// Auto-detected at boot: GY-521 (MPU6050, addr 0x68/0x69) or GY-BMI160
// (Bosch, same I2C addresses). Distinguished by ID registers — both
// chips answer on the same addresses, WHO_AM_I/CHIP_ID is the only
// reliable discriminator (marketplace boards are often mislabeled).
enum ImuType { IMU_NONE, IMU_MPU6050, IMU_BMI160 };
ImuType imuType = IMU_NONE;
uint8_t imuAddr = 0;

MPU6050 mpu6050(Wire);
Preferences prefs;
AsyncWebServer server(80);
AsyncEventSource events("/api/events");

// ---------------- LOW-LEVEL I2C HELPERS (BMI160 driver) ----------------
bool i2cAlive(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

uint8_t imuReadReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  if (Wire.requestFrom((int)addr, 1) != 1) return 0;
  return Wire.read();
}

void imuWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// Raw accel read (g). Returns false on bus error (caller keeps last tilt).
bool bmi160ReadAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(imuAddr);
  Wire.write(0x12);                       // ACC_DATA_X_LSB, 6 bytes, LSB first
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)imuAddr, 6) != 6) return false;
  int16_t rx = Wire.read() | (Wire.read() << 8);
  int16_t ry = Wire.read() | (Wire.read() << 8);
  int16_t rz = Wire.read() | (Wire.read() << 8);
  ax = rx / 16384.0f;                     // ±2g range = 16384 LSB/g
  ay = ry / 16384.0f;
  az = rz / 16384.0f;
  return true;
}

void bmi160Init() {
  imuWriteReg(imuAddr, 0x7E, 0xB6);       // soft reset
  delay(10);
  imuReadReg(imuAddr, 0x7E);              // BMI160 I2C quirk: dummy read after reset
  delay(5);
  imuWriteReg(imuAddr, 0x40, 0x28);       // ACC_CONF: 100 Hz ODR, normal BW
  imuWriteReg(imuAddr, 0x41, 0x03);       // ACC_RANGE: ±2g
  imuWriteReg(imuAddr, 0x7E, 0x11);       // CMD: accelerometer -> normal mode
  delay(5);
}

// Unified accel read in g — the ONLY sensor call the game logic uses.
bool readAccelG(float &ax, float &ay, float &az) {
  if (imuType == IMU_BMI160)  return bmi160ReadAccel(ax, ay, az);
  if (imuType == IMU_MPU6050) {
    mpu6050.update();
    ax = mpu6050.getAccX(); ay = mpu6050.getAccY(); az = mpu6050.getAccZ();
    return true;
  }
  return false;
}

const char* imuTypeName() {
  switch (imuType) {
    case IMU_MPU6050: return "MPU6050";
    case IMU_BMI160:  return "BMI160";
    default:          return "NONE";
  }
}

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

// ---------------- TIMING / DEBOUNCE ----------------
const unsigned long MPU_INTERVAL_MS   = 20;   // 50 Hz sensor reads
const unsigned long SSE_INTERVAL_MS   = 100;  // 10 Hz push to browser
const unsigned long NVS_SAVE_DELAY_MS = 3000; // debounce flash writes
unsigned long lastMpuRead  = 0;
unsigned long lastSsePush  = 0;
unsigned long lastThrTouch = 0;
bool thrDirty = false;
volatile bool calRequested = false;  // set by web task, consumed in loop()

// ---------------- FORWARD DECLARATIONS ----------------
void startCountdown();
void calibrateDevice();
void saveThresholdIfDue();
void saveSettings();
void buildStateJson(char* buf, size_t len);
void pushStateEvent(bool force);
void updateStatusLed();
void setupWebServer();

//================= WEB PAGE =================
// State arrives via SSE (/api/events); fetch() only for actions.
const char* HTML_PAGE = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>ESP Leveller</title>
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
<h1>&#x1F6E1; BARRETT LEVELLER</h1>
<div id='conn'></div>
<div id='status'>IDLE</div>
<div id='timer'>00.00</div>
<div id='tiltbar'><div id='tiltfill'></div></div>
<div class='row small'><span id='tilttxt'>Tilt 0.0 / 15 deg</span></div>
<div class='row'>
  <button id='minus'>&#x2796;</button>
  <input type='range' id='thrSlider' min='5' max='90' step='1' value='15' style='flex:1;accent-color:#0af'>
  <button id='plus'>&#x2795;</button>
</div>
<div class='row'>
  <span id='thrtxt' style='font-weight:700;font-size:1.2rem'>15&deg;</span>
  <label class='small' style='margin-left:8px'><input type='checkbox' id='snd' checked> sound</label>
</div>
<div class='row'>
  <button id='calBtn'>CALIBRATE</button>
  <button id='abortBtn'>STOP</button>
</div>
<button id='startBtn'>START</button>
<div class='info'>WiFi: ESPLevellerAP &bull; 192.168.4.1</div>
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
const slider=$('thrSlider');
slider.oninput=()=>{ $('thrtxt').textContent=slider.value+'\u00B0'; };          // live feedback
slider.onchange=()=>post('/api/threshold?val='+slider.value);                    // commit on release
document.addEventListener('pointerdown',()=>{try{ctx();}catch(e){}},{once:true});
$('snd').onchange=()=>{if($('snd').checked) beep(1500,80);};
function fmt(ms){
  const s=Math.floor(ms/1000), c=Math.floor((ms%1000)/10);
  return String(s).padStart(2,'0')+'.'+String(c).padStart(2,'0');
}
// --- SSE: state is pushed by the device ---
function applyState(s){
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
  if(document.activeElement!==slider) slider.value=s.thr;   // don't fight the user's finger
  $('startBtn').textContent=s.state==='IDLE'?'START':(s.state==='GAMEOVER'?'BACK TO MENU':'...');
}
function connect(){
  const es=new EventSource('/api/events');
  es.onmessage=e=>{ $('conn').textContent=''; try{ applyState(JSON.parse(e.data)); }catch(_){} };
  es.onerror=()=>{ $('conn').textContent='DISCONNECTED'; };
}
connect();
// Poll fallback only if SSE never connected within 3s
setTimeout(()=>{
  if($('conn').textContent==='DISCONNECTED'){
    setInterval(async ()=>{
      try{
        const s=await (await fetch('/api/state',{cache:'no-store'})).json();
        $('conn').textContent='';
        applyState(s);
      }catch(e){ $('conn').textContent='DISCONNECTED'; }
    },1000);
  }
},3000);
</script>
</body>
</html>
)rawliteral";

void setup() {
  SLOG_BEGIN();
  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);

  // Auto-detect IMU: GY-521 (MPU6050) and GY-BMI160 both sit at 0x68/0x69.
  // WHO_AM_I (0x75) = 0x68 -> MPU6050; CHIP_ID (0x00) = 0xD1 -> BMI160.
  const uint8_t addrs[2] = {0x68, 0x69};
  for (uint8_t a : addrs) {
    if (!i2cAlive(a)) continue;
    if (imuReadReg(a, 0x75) == 0x68) {          // MPU6050 WHO_AM_I
      imuType = IMU_MPU6050; imuAddr = a; break;
    }
    if (imuReadReg(a, 0x00) == 0xD1) {          // BMI160 CHIP_ID
      imuType = IMU_BMI160;  imuAddr = a; break;
    }
  }
  if (imuType == IMU_NONE) {
    SLOG("No IMU found (MPU6050/BMI160) — check pod wiring (SDA=IO4, SCL=IO5)");
    // Distress blink so an unattended board is diagnosable at a glance
    for (;;) {
      digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
      delay(100);
    }
  }
  SLOGF("%s found at 0x%02X\n", imuTypeName(), imuAddr);

  if (imuType == IMU_MPU6050) mpu6050.begin();
  else bmi160Init();
  // No calcGyroOffsets(): tilt now comes from the accelerometer, which
  // needs no gyro bias calibration. Saves ~7s at boot.

  prefs.begin("esp_leveller", false);
  thresholdAngle = prefs.getFloat("threshold", 15.0);
  baseAngleX     = prefs.getFloat("baseX", 0.0);
  baseAngleY     = prefs.getFloat("baseY", 0.0);

  setupWebServer();
  SLOG("Ready — connect to WiFi and open 192.168.4.1");
}

void loop() {
  // Rate-limited MPU reads: full-speed I2C starves the single-core
  // C3's WiFi/lwIP task (root cause of the original lag).
  unsigned long now = millis();
  if (now - lastMpuRead >= MPU_INTERVAL_MS) {
    lastMpuRead = now;
    float ax, ay, az;
    if (!readAccelG(ax, ay, az)) {
      // Bus hiccup: keep last tilt, retry next tick (no game glitch).
    } else {
    // Drift-free tilt: angle vs. GRAVITY from the accelerometer.
    // (getAngleX/Y are gyro-integrated and accumulate bias over hours —
    // field-tested: visually-over-threshold players read under it.)
    float roll  = atan2f(ay, az) * 57.2958f;
    float pitch = atan2f(-ax,
                         sqrtf(ay*ay + az*az)) * 57.2958f;
    float tiltX = abs(roll  - baseAngleX);
    if (tiltX > 180) tiltX = 360 - tiltX;
    float tiltY = abs(pitch - baseAngleY);
    if (tiltY > 180) tiltY = 360 - tiltY;
    float maxTilt = (tiltX > tiltY) ? tiltX : tiltY;
    // EMA smoothing: kills accel noise/vibration spikes without lag
    lastMaxTilt = lastMaxTilt * 0.7f + maxTilt * 0.3f;

    if (currentState == COUNTDOWN) {
      if (now - countdownStartTime >= countdownDelay) {
        currentState  = PLAYING;
        gameStartTime = millis();
        pushStateEvent(true);  // state change -> push immediately
      }
    }
    else if (currentState == PLAYING) {
      if (maxTilt > thresholdAngle) {
        currentState = GAMEOVER;
        finalTime = millis() - gameStartTime;
        pushStateEvent(true);
      }
    }
    updateStatusLed();
    }
  }

  // Push state to all connected browsers at 10 Hz.
  // AsyncEventSource queues per client — never blocks, never starves.
  pushStateEvent(false);

  // Debounced flash writes for threshold changes
  saveThresholdIfDue();

  // Calibration runs HERE (loop owns the I2C bus) — doing it in the
  // async web task raced with loop()'s mpu6050.update() and produced
  // corrupted base angles.
  if (calRequested) {
    calRequested = false;
    calibrateDevice();
    pushStateEvent(true);
  }
}

// Onboard D4 LED as a glanceable status light:
//   off      = IDLE
//   blinking = COUNTDOWN
//   on solid = PLAYING
//   fast blinks x3 pattern = GAMEOVER
void updateStatusLed() {
  static unsigned long lastBlink = 0;
  static bool ledOn = false;
  unsigned long period;
  switch (currentState) {
    case COUNTDOWN: period = 200; break;
    case PLAYING:   digitalWrite(LED_STATUS, HIGH); return;
    case GAMEOVER:  period = 500; break;
    default:        digitalWrite(LED_STATUS, LOW); return;
  }
  if (millis() - lastBlink >= period) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_STATUS, ledOn);
  }
}

// ---------------- WEB SERVER ----------------
void setupWebServer() {
  SLOG("Starting WiFi AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS, 1, 0, 4);

  // Cap TX power AFTER the radio is up. Harmless on the LuatOS board,
  // insurance on clones with sketchy regulators/antenna matching.
  esp_wifi_set_max_tx_power(WIFI_POWER_8_5dBm);
  WiFi.setSleep(false);
  delay(100);

  SLOG_P("AP IP: ");
  SLOG(WiFi.softAPIP());

  MDNS.begin("leveller");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", HTML_PAGE);
  });

  // --- SSE: push state instead of browser polling ---
  server.addHandler(&events);
  events.onConnect([](AsyncEventSourceClient *client) {
    char json[128];
    buildStateJson(json, sizeof(json));
    client->send(json, NULL, millis(), 2000);
  });

  // Poll fallback (also used by curl/tests)
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    char json[128];
    buildStateJson(json, sizeof(json));
    request->send(200, "application/json", json);
  });

  server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (currentState == IDLE) startCountdown();
    else if (currentState == GAMEOVER) currentState = IDLE;
    pushStateEvent(true);
    request->send(200, "text/plain", "ok");
  });

  server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
    calRequested = true;   // handled in loop(): owns the I2C bus
    request->send(200, "text/plain", "ok");
  });

  server.on("/api/abort", HTTP_POST, [](AsyncWebServerRequest *request) {
    // STOP: end the round and show elapsed time instead of resetting.
    if (currentState == PLAYING) {
      currentState = GAMEOVER;
      finalTime = millis() - gameStartTime;
    } else if (currentState == COUNTDOWN) {
      currentState = IDLE;  // nothing to show for a cancelled countdown
    }
    pushStateEvent(true);
    request->send(200, "text/plain", "ok");
  });

  server.on("/api/threshold", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("val") && currentState == IDLE) {
      int v = request->getParam("val")->value().toInt();
      if (v >= 5 && v <= 90) {
        thresholdAngle = v;
        thrDirty = true;                 // debounced write; not every click
        lastThrTouch = millis();
        pushStateEvent(true);
      }
    }
    request->send(200, "text/plain", "ok");
  });

  server.begin();
  SLOG_P("Web UI ready: http://");
  SLOG(WiFi.softAPIP());
}

void buildStateJson(char* buf, size_t len) {
  unsigned long t = 0;
  if (currentState == PLAYING) t = millis() - gameStartTime;
  else if (currentState == GAMEOVER) t = finalTime;

  const char* st;
  switch (currentState) {
    case IDLE:      st = "IDLE";      break;
    case COUNTDOWN: st = "COUNTDOWN"; break;
    case PLAYING:   st = "PLAYING";   break;
    default:        st = "GAMEOVER";  break;
  }

  // Static buffer + snprintf: no heap churn on every tick.
  snprintf(buf, len,
           "{\"state\":\"%s\",\"tilt\":%.1f,\"thr\":%d,\"t\":%lu}",
           st, lastMaxTilt, (int)thresholdAngle, t);
}

void pushStateEvent(bool force) {
  static char lastJson[128] = {0};
  if (!force && millis() - lastSsePush < SSE_INTERVAL_MS) return;
  lastSsePush = millis();

  char json[128];
  buildStateJson(json, sizeof(json));

  // Skip identical payloads (mainly avoids spam while IDLE & flat).
  if (!force && strcmp(json, lastJson) == 0) return;
  strncpy(lastJson, json, sizeof(lastJson) - 1);

  events.send(json, NULL, millis());
}

// ---------------- GAME FUNCTIONS ----------------
void startCountdown() {
  currentState = COUNTDOWN;
  countdownDelay = random(2000, 5001);
  countdownStartTime = millis();
}

void calibrateDevice() {
  // Average 10 samples over ~500ms for a stable base angle.
  digitalWrite(LED_STATUS, HIGH);  // LED on = calibrating
  float sumX = 0, sumY = 0;
  const int N = 10;
  for (int i = 0; i < N; i++) {
    float ax, ay, az;
    if (readAccelG(ax, ay, az)) {
      float roll  = atan2f(ay, az) * 57.2958f;
      float pitch = atan2f(-ax,
                           sqrtf(ay*ay + az*az)) * 57.2958f;
      sumX += roll;
      sumY += pitch;
    }
    delay(50);
  }
  baseAngleX = sumX / N;
  baseAngleY = sumY / N;
  saveSettings();  // calibration is intentional & infrequent: write now
  currentState = IDLE;
  digitalWrite(LED_STATUS, LOW);
}

void saveThresholdIfDue() {
  if (thrDirty && (millis() - lastThrTouch >= NVS_SAVE_DELAY_MS)) {
    prefs.putFloat("threshold", thresholdAngle);
    thrDirty = false;
    SLOG("Threshold saved to NVS");
  }
}

void saveSettings() {
  prefs.putFloat("threshold", thresholdAngle);
  prefs.putFloat("baseX", baseAngleX);
  prefs.putFloat("baseY", baseAngleY);
}
