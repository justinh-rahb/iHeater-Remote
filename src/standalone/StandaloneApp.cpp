#include "standalone/StandaloneApp.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <IPAddress.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#include "controller/RmtOutputAdapter.h"

namespace iheaterlink {
namespace standalone {

#ifndef IHEATER_BOOT_BUTTON_PIN
#define IHEATER_BOOT_BUTTON_PIN 9
#endif

#ifndef IHEATER_STATUS_LED_PIN
#define IHEATER_STATUS_LED_PIN 8
#endif

#ifndef IHEATER_STATUS_LED_ACTIVE_LOW
#define IHEATER_STATUS_LED_ACTIVE_LOW 1
#endif

#ifndef IHEATER_AP_PASSWORD
#define IHEATER_AP_PASSWORD ""
#endif

namespace {

constexpr uint8_t kMaxMode = 7;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kRestartDelayMs = 800;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kMultiClickWindowMs = 650;
constexpr uint32_t kLongPressMs = 2000;
constexpr uint32_t kLedOnMs = 80;
constexpr uint32_t kLedOffMs = 120;
constexpr uint32_t kLedPauseMs = 1600;
constexpr uint16_t kDnsPort = 53;
constexpr uint16_t kMaxTimerMinutes = 24 * 60;
constexpr size_t kMaxWifiSsidLen = 32;
constexpr size_t kMaxWifiPasswordLen = 64;

const char *kPrefsNamespace = "iheater-remote";
const char *kWifiSsidKey = "ssid";
const char *kWifiPasswordKey = "pass";

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

const float kModeTempsC[] = {
    0.0f, 55.0f, 60.0f, 65.0f, 70.0f, 75.0f, 80.0f, 85.0f,
};

DNSServer s_dnsServer;
WebServer s_server(80);
RmtOutputAdapter s_output{RmtOutputConfig{}};

uint8_t s_mode = 0;
bool s_apMode = false;
String s_apSsid;
String s_stationSsid;
String s_wifiCredentialSource = "none";
uint16_t s_timerMinutes = 0;
uint32_t s_timerOffAtMs = 0;
uint32_t s_restartAtMs = 0;

bool s_buttonStableDown = false;
bool s_buttonLastRawDown = false;
bool s_longPressHandled = false;
uint8_t s_pendingClicks = 0;
uint32_t s_buttonLastRawChangeMs = 0;
uint32_t s_buttonDownAtMs = 0;
uint32_t s_clickDeadlineMs = 0;

bool s_ledOn = false;
uint8_t s_ledFlashesDone = 0;
uint32_t s_ledNextAtMs = 0;

bool ledEnabled() { return IHEATER_STATUS_LED_PIN != 0xFF; }

void writeLed(bool on) {
  if (!ledEnabled())
    return;
#if IHEATER_STATUS_LED_ACTIVE_LOW
  digitalWrite(IHEATER_STATUS_LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(IHEATER_STATUS_LED_PIN, on ? HIGH : LOW);
#endif
  s_ledOn = on;
}

void resetLedPattern() {
  s_ledFlashesDone = 0;
  s_ledNextAtMs = 0;
  writeLed(false);
}

void applyMode(uint8_t mode, const char *source) {
  if (mode > kMaxMode)
    mode = kMaxMode;

  s_mode = mode;
  if (mode == 0) {
    s_timerOffAtMs = 0;
  } else if (s_timerMinutes > 0) {
    s_timerOffAtMs = millis() + (uint32_t)s_timerMinutes * 60u * 1000u;
  } else {
    s_timerOffAtMs = 0;
  }

  ControllerOutputCommand cmd;
  if (mode == 0) {
    cmd.mode = ControllerOutputMode::Off;
    cmd.targetTempC = 0.0f;
  } else {
    cmd.mode = ControllerOutputMode::TargetTemperature;
    cmd.targetTempC = kModeTempsC[mode];
  }
  s_output.apply(cmd);
  s_output.forceFrame();
  resetLedPattern();

  Serial.printf("[REMOTE] %s mode=%u target=%.1fC timer=%umin pulse=%u\n",
                source, mode, (double)cmd.targetTempC, s_timerMinutes,
                s_output.getLastPulseCode());
}

void updateHeatTimer() {
  if (s_mode == 0 || s_timerOffAtMs == 0)
    return;

  if ((int32_t)(millis() - s_timerOffAtMs) >= 0) {
    applyMode(0, "timer");
  }
}

uint32_t timerRemainingSeconds() {
  if (s_mode == 0 || s_timerOffAtMs == 0)
    return 0;

  const int32_t remainingMs = (int32_t)(s_timerOffAtMs - millis());
  if (remainingMs <= 0)
    return 0;
  return ((uint32_t)remainingMs + 999u) / 1000u;
}

String localIpString() {
  return s_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

const char *wifiModeString() { return s_apMode ? "ap" : "sta"; }

bool loadStoredWifiCredentials(String &ssid, String &password) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true))
    return false;

  ssid = prefs.getString(kWifiSsidKey, "");
  password = prefs.getString(kWifiPasswordKey, "");
  prefs.end();

  ssid.trim();
  return ssid.length() > 0;
}

bool loadWifiCredentials(String &ssid, String &password, String &source) {
  if (loadStoredWifiCredentials(ssid, password)) {
    source = "stored";
    return true;
  }

#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  ssid = WIFI_SSID;
  password = WIFI_PASSWORD;
  ssid.trim();
  if (ssid.length() > 0) {
    source = "build";
    return true;
  }
#endif

  source = "none";
  return false;
}

bool saveStoredWifiCredentials(const String &ssid, const String &password) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false))
    return false;

  const bool ok = prefs.putString(kWifiSsidKey, ssid) > 0 &&
                  prefs.putString(kWifiPasswordKey, password) >= 0;
  prefs.end();
  return ok;
}

bool clearStoredWifiCredentials() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false))
    return false;

  prefs.remove(kWifiSsidKey);
  prefs.remove(kWifiPasswordKey);
  prefs.end();
  return true;
}

void scheduleRestart() { s_restartAtMs = millis() + kRestartDelayMs; }

void updateRestart() {
  if (s_restartAtMs == 0)
    return;

  if ((int32_t)(millis() - s_restartAtMs) >= 0) {
    Serial.println("[REMOTE] Restarting");
    Serial.flush();
    ESP.restart();
  }
}

bool isIpLiteral(const String &host) {
  for (size_t i = 0; i < host.length(); ++i) {
    const char c = host[i];
    if ((c < '0' || c > '9') && c != '.' && c != ':')
      return false;
  }
  return host.length() > 0;
}

bool captiveRedirectIfNeeded() {
  if (!s_apMode)
    return false;

  const String host = s_server.hostHeader();
  if (host.length() == 0 || isIpLiteral(host))
    return false;

  s_server.sendHeader("Location", String("http://") + kApIp.toString() + "/",
                      true);
  s_server.send(302, "text/plain", "");
  return true;
}

void sendJsonStatus() {
  updateHeatTimer();

  StaticJsonDocument<768> doc;
  doc["mode"] = s_mode;
  doc["targetTempC"] = kModeTempsC[s_mode];
  doc["heating"] = s_mode != 0;
  doc["timerMinutes"] = s_timerMinutes;
  doc["timerActive"] = s_mode != 0 && s_timerOffAtMs != 0;
  doc["timerRemainingSeconds"] = timerRemainingSeconds();
  doc["pulseCode"] = s_output.getLastPulseCode();
  doc["rmtEnabled"] = s_output.isEnabled();
  doc["wifiMode"] = wifiModeString();
  doc["ip"] = localIpString();
  doc["ssid"] = s_apMode ? s_apSsid : WiFi.SSID();
  doc["configuredWifiSsid"] = s_stationSsid;
  doc["wifiCredentialSource"] = s_wifiCredentialSource;
  doc["restartPending"] = s_restartAtMs != 0;
  doc["uptimeMs"] = millis();

  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

void handleModePost() {
  if (!s_server.hasArg("mode")) {
    s_server.send(400, "application/json", "{\"error\":\"missing mode\"}");
    return;
  }
  const int requested = s_server.arg("mode").toInt();
  if (requested < 0 || requested > kMaxMode) {
    s_server.send(400, "application/json", "{\"error\":\"mode range is 0..7\"}");
    return;
  }

  applyMode(static_cast<uint8_t>(requested), "web");
  sendJsonStatus();
}

void handleTimerPost() {
  if (!s_server.hasArg("minutes")) {
    s_server.send(400, "application/json", "{\"error\":\"missing minutes\"}");
    return;
  }

  const int requested = s_server.arg("minutes").toInt();
  if (requested < 0 || requested > kMaxTimerMinutes) {
    s_server.send(400, "application/json",
                  "{\"error\":\"minutes range is 0..1440\"}");
    return;
  }

  s_timerMinutes = static_cast<uint16_t>(requested);
  if (s_mode != 0 && s_timerMinutes > 0) {
    s_timerOffAtMs = millis() + (uint32_t)s_timerMinutes * 60u * 1000u;
  } else {
    s_timerOffAtMs = 0;
  }

  Serial.printf("[REMOTE] web timer=%umin\n", s_timerMinutes);
  sendJsonStatus();
}

void handleWifiPost() {
  if (!s_server.hasArg("ssid")) {
    s_server.send(400, "application/json", "{\"error\":\"missing ssid\"}");
    return;
  }

  String ssid = s_server.arg("ssid");
  String password = s_server.hasArg("password") ? s_server.arg("password") : "";
  ssid.trim();

  if (ssid.length() == 0 || ssid.length() > kMaxWifiSsidLen) {
    s_server.send(400, "application/json",
                  "{\"error\":\"ssid length must be 1..32\"}");
    return;
  }

  if (password.length() > kMaxWifiPasswordLen ||
      (password.length() > 0 && password.length() < 8)) {
    s_server.send(400, "application/json",
                  "{\"error\":\"password length must be 0 or 8..64\"}");
    return;
  }

  if (!saveStoredWifiCredentials(ssid, password)) {
    s_server.send(500, "application/json", "{\"error\":\"save failed\"}");
    return;
  }

  s_stationSsid = ssid;
  s_wifiCredentialSource = "stored";
  scheduleRestart();
  Serial.printf("[REMOTE] saved WiFi SSID=%s, restarting\n", ssid.c_str());
  sendJsonStatus();
}

void handleWifiClearPost() {
  if (!clearStoredWifiCredentials()) {
    s_server.send(500, "application/json", "{\"error\":\"clear failed\"}");
    return;
  }

  s_stationSsid = "";
  s_wifiCredentialSource = "none";
  scheduleRestart();
  Serial.println("[REMOTE] cleared stored WiFi credentials, restarting");
  sendJsonStatus();
}

void handleRoot() {
  if (captiveRedirectIfNeeded())
    return;

  static const char page[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>iHeater Remote</title>
<style>
:root{color-scheme:dark light;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#101314;color:#eef3f1}
body{margin:0;min-height:100vh;background:#101314}
main{width:min(760px,calc(100vw - 32px));margin:0 auto;padding:28px 0 36px}
h1{font-size:clamp(28px,6vw,44px);line-height:1;margin:0 0 18px}
.panel{border:1px solid #2a3332;border-radius:8px;background:#171d1c;padding:18px;margin:16px 0}
.status{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
.metric{border:1px solid #293231;border-radius:6px;padding:12px;background:#111615}
.label{display:block;color:#9fb0aa;font-size:12px;text-transform:uppercase;letter-spacing:.08em}
.value{display:block;margin-top:4px;font-size:24px;font-weight:700}
.modes{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}
.timer{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:10px;align-items:center}
.timer label{grid-column:span 2;color:#9fb0aa;font-size:13px;font-weight:700;text-transform:uppercase;letter-spacing:.08em}
.timer input{grid-column:span 2}
.timer button{min-height:46px}
.timer button.active{border-color:#8fd0bb;background:#1c4a3e}
.form{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;align-items:end}
.form label{display:grid;gap:6px;color:#9fb0aa;font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:.08em}
.form label.wide{grid-column:span 2}
input{min-height:46px;border:1px solid #3a4643;border-radius:6px;background:#101514;color:#f6fbf9;padding:0 12px;font:inherit;font-weight:700}
button{appearance:none;border:1px solid #3a4643;border-radius:6px;background:#22302c;color:#f6fbf9;min-height:48px;font:inherit;font-weight:700;cursor:pointer}
button:hover{background:#2b3b36}
button.active{border-color:#8fd0bb;background:#1c4a3e}
button.stop{grid-column:span 4;background:#4b2424;border-color:#724141}
button.secondary{background:#202826}
.small{color:#9fb0aa;font-size:14px;line-height:1.45}
@media (max-width:560px){.status,.modes{grid-template-columns:1fr 1fr}.timer,.form{grid-template-columns:1fr 1fr}.timer label,.timer input,.form label.wide{grid-column:span 2}button.stop{grid-column:span 2}}
</style>
</head>
<body>
<main>
<h1>iHeater Remote</h1>
<section class="panel status">
<div class="metric"><span class="label">Mode</span><span class="value" id="mode">-</span></div>
<div class="metric"><span class="label">Target</span><span class="value" id="target">-</span></div>
<div class="metric"><span class="label">Timer</span><span class="value" id="timer">-</span></div>
<div class="metric"><span class="label">Remaining</span><span class="value" id="remaining">-</span></div>
<div class="metric"><span class="label">Network</span><span class="value" id="network">-</span></div>
<div class="metric"><span class="label">Pulse</span><span class="value" id="pulse">-</span></div>
<div class="metric"><span class="label">Wi-Fi Config</span><span class="value" id="wifiConfig">-</span></div>
</section>
<section class="panel">
<div class="modes" id="modes"></div>
</section>
<section class="panel">
<div class="timer" id="timers">
<button data-minutes="0">Until Off</button>
<button data-minutes="30">30 min</button>
<button data-minutes="60">1 hr</button>
<button data-minutes="120">2 hr</button>
<button data-minutes="240">4 hr</button>
<button data-minutes="480">8 hr</button>
<label for="customMinutes">Custom minutes</label>
<input id="customMinutes" inputmode="numeric" min="0" max="1440" step="1" type="number">
<button id="applyTimer">Set</button>
</div>
</section>
<section class="panel">
<div class="form">
<label class="wide" for="wifiSsid">Wi-Fi SSID<input id="wifiSsid" maxlength="32" autocomplete="username"></label>
<label class="wide" for="wifiPassword">Wi-Fi Password<input id="wifiPassword" maxlength="64" type="password" autocomplete="current-password"></label>
<button id="saveWifi">Save Wi-Fi</button>
<button id="clearWifi" class="secondary">Forget Wi-Fi</button>
</div>
<p class="small" id="wifiMessage"></p>
</section>
<p class="small" id="detail"></p>
</main>
<script>
const temps=[0,55,60,65,70,75,80,85];
const modes=document.getElementById("modes");
const timers=document.getElementById("timers");
const customMinutes=document.getElementById("customMinutes");
const wifiSsid=document.getElementById("wifiSsid");
const wifiPassword=document.getElementById("wifiPassword");
const wifiMessage=document.getElementById("wifiMessage");
temps.forEach((temp,mode)=>{
  const b=document.createElement("button");
  b.textContent=mode===0?"Off":`Mode ${mode} - ${temp} C`;
  b.dataset.mode=mode;
  b.className=mode===0?"stop":"";
  b.onclick=()=>fetch(`/api/mode?mode=${mode}`,{method:"POST"}).then(refresh);
  modes.appendChild(b);
});
timers.querySelectorAll("button[data-minutes]").forEach(b=>{
  b.onclick=()=>setTimer(Number(b.dataset.minutes));
});
document.getElementById("applyTimer").onclick=()=>{
  const minutes=Number(customMinutes.value||0);
  setTimer(minutes);
};
customMinutes.addEventListener("keydown",event=>{
  if(event.key==="Enter") setTimer(Number(customMinutes.value||0));
});
document.getElementById("saveWifi").onclick=saveWifi;
document.getElementById("clearWifi").onclick=clearWifi;
function setTimer(minutes){
  if(!Number.isFinite(minutes)) minutes=0;
  const value=Math.max(0,Math.min(1440,Math.round(minutes)));
  return fetch(`/api/timer?minutes=${value}`,{method:"POST"}).then(refresh);
}
async function postForm(path,fields){
  const body=new URLSearchParams(fields);
  const r=await fetch(path,{method:"POST",body});
  const s=await r.json().catch(()=>({error:"request failed"}));
  if(!r.ok) throw new Error(s.error||"request failed");
  return s;
}
async function saveWifi(){
  wifiMessage.textContent="";
  try{
    await postForm("/api/wifi",{ssid:wifiSsid.value.trim(),password:wifiPassword.value});
    wifiPassword.value="";
    wifiMessage.textContent="Saved. Rebooting...";
    await refresh();
  }catch(err){
    wifiMessage.textContent=err.message;
  }
}
async function clearWifi(){
  wifiMessage.textContent="";
  try{
    await postForm("/api/wifi/clear",{});
    wifiPassword.value="";
    wifiMessage.textContent="Forgotten. Rebooting...";
    await refresh();
  }catch(err){
    wifiMessage.textContent=err.message;
  }
}
function formatDuration(seconds){
  if(seconds<=0) return "-";
  const h=Math.floor(seconds/3600);
  const m=Math.floor((seconds%3600)/60);
  const s=seconds%60;
  if(h>0) return `${h}h ${String(m).padStart(2,"0")}m`;
  if(m>0) return `${m}m ${String(s).padStart(2,"0")}s`;
  return `${s}s`;
}
async function refresh(){
  const s=await fetch("/api/status",{cache:"no-store"}).then(r=>r.json());
  document.getElementById("mode").textContent=s.mode===0?"Off":`MODE_TEMP_${s.mode}`;
  document.getElementById("target").textContent=s.mode===0?"0 C":`${s.targetTempC} C`;
  document.getElementById("timer").textContent=s.timerMinutes===0?"Until Off":`${s.timerMinutes} min`;
  document.getElementById("remaining").textContent=s.timerActive?formatDuration(s.timerRemainingSeconds):"-";
  document.getElementById("network").textContent=s.wifiMode.toUpperCase();
  document.getElementById("pulse").textContent=s.pulseCode;
  document.getElementById("wifiConfig").textContent=s.wifiCredentialSource.toUpperCase();
  document.getElementById("detail").textContent=`${s.ssid} - ${s.ip}`;
  document.querySelectorAll("button[data-mode]").forEach(b=>b.classList.toggle("active",Number(b.dataset.mode)===s.mode));
  document.querySelectorAll("button[data-minutes]").forEach(b=>b.classList.toggle("active",Number(b.dataset.minutes)===s.timerMinutes));
  if(document.activeElement!==customMinutes) customMinutes.value=s.timerMinutes;
  if(document.activeElement!==wifiSsid) wifiSsid.value=s.configuredWifiSsid||"";
}
refresh();
setInterval(refresh,1000);
</script>
</body>
</html>
)HTML";
  s_server.send_P(200, "text/html", page);
}

void configureRoutes() {
  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/api/status", HTTP_GET, sendJsonStatus);
  s_server.on("/api/mode", HTTP_POST, handleModePost);
  s_server.on("/api/timer", HTTP_POST, handleTimerPost);
  s_server.on("/api/wifi", HTTP_POST, handleWifiPost);
  s_server.on("/api/wifi/clear", HTTP_POST, handleWifiClearPost);
  s_server.on("/api/off", HTTP_POST, []() {
    applyMode(0, "web");
    sendJsonStatus();
  });
  s_server.on("/generate_204", HTTP_GET, handleRoot);
  s_server.on("/gen_204", HTTP_GET, handleRoot);
  s_server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  s_server.on("/library/test/success.html", HTTP_GET, handleRoot);
  s_server.on("/ncsi.txt", HTTP_GET, handleRoot);
  s_server.on("/connecttest.txt", HTTP_GET, handleRoot);
  s_server.on("/redirect", HTTP_GET, handleRoot);
  s_server.onNotFound([]() {
    if (s_apMode) {
      handleRoot();
    } else {
      s_server.send(404, "application/json", "{\"error\":\"not found\"}");
    }
  });
}

void startAccessPoint() {
  uint64_t mac = ESP.getEfuseMac();
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "iHeater Remote %04X",
           static_cast<unsigned>((mac >> 32) & 0xFFFF));
  s_apSsid = ssid;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
  const char *password = IHEATER_AP_PASSWORD;
  if (strlen(password) >= 8) {
    WiFi.softAP(ssid, password);
  } else {
    WiFi.softAP(ssid);
  }
  s_apMode = true;
  s_dnsServer.start(kDnsPort, "*", kApIp);

  Serial.printf("[REMOTE] AP SSID=%s IP=%s\n", ssid,
                WiFi.softAPIP().toString().c_str());
  Serial.println("[REMOTE] Captive portal DNS started");
}

void configureWiFi() {
  WiFi.persistent(false);
  WiFi.setSleep(false);

  String wifiSsid;
  String wifiPassword;
  String credentialSource;
  if (loadWifiCredentials(wifiSsid, wifiPassword, credentialSource)) {
    s_stationSsid = wifiSsid;
    s_wifiCredentialSource = credentialSource;

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    Serial.printf("[REMOTE] Connecting to WiFi SSID=%s source=%s\n",
                  wifiSsid.c_str(), credentialSource.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (uint32_t)(millis() - start) < kWifiConnectTimeoutMs) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      s_apMode = false;
      Serial.printf("[REMOTE] WiFi connected IP=%s\n",
                    WiFi.localIP().toString().c_str());
      return;
    }

    Serial.println("[REMOTE] WiFi connect timeout, starting AP");
    WiFi.disconnect(true);
  } else {
    s_stationSsid = "";
    s_wifiCredentialSource = "none";
  }

  startAccessPoint();
}

void updateButton() {
  const uint32_t now = millis();
  const bool rawDown = digitalRead(IHEATER_BOOT_BUTTON_PIN) == LOW;

  if (rawDown != s_buttonLastRawDown) {
    s_buttonLastRawDown = rawDown;
    s_buttonLastRawChangeMs = now;
  }

  if ((uint32_t)(now - s_buttonLastRawChangeMs) >= kButtonDebounceMs &&
      rawDown != s_buttonStableDown) {
    s_buttonStableDown = rawDown;
    if (s_buttonStableDown) {
      s_buttonDownAtMs = now;
      s_longPressHandled = false;
    } else if (!s_longPressHandled) {
      if (s_pendingClicks < 255)
        ++s_pendingClicks;
      s_clickDeadlineMs = now + kMultiClickWindowMs;
    }
  }

  if (s_buttonStableDown && !s_longPressHandled &&
      (uint32_t)(now - s_buttonDownAtMs) >= kLongPressMs) {
    s_pendingClicks = 0;
    s_longPressHandled = true;
    applyMode(0, "button:long");
  }

  if (s_pendingClicks && !s_buttonStableDown &&
      (int32_t)(now - s_clickDeadlineMs) >= 0) {
    const uint8_t mode = s_pendingClicks > kMaxMode ? kMaxMode : s_pendingClicks;
    s_pendingClicks = 0;
    applyMode(mode, "button:click");
  }
}

void updateLed() {
  if (!ledEnabled())
    return;

  const uint32_t now = millis();
  if (s_mode == 0) {
    if (s_ledOn)
      writeLed(false);
    return;
  }

  if ((int32_t)(now - s_ledNextAtMs) < 0)
    return;

  if (s_ledOn) {
    writeLed(false);
    ++s_ledFlashesDone;
    if (s_ledFlashesDone >= s_mode) {
      s_ledFlashesDone = 0;
      s_ledNextAtMs = now + kLedPauseMs;
    } else {
      s_ledNextAtMs = now + kLedOffMs;
    }
  } else {
    writeLed(true);
    s_ledNextAtMs = now + kLedOnMs;
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("[REMOTE] iHeater Remote firmware starting");

  if (ledEnabled()) {
    pinMode(IHEATER_STATUS_LED_PIN, OUTPUT);
    writeLed(false);
  }
  pinMode(IHEATER_BOOT_BUTTON_PIN, INPUT_PULLUP);

  s_output.begin();
  applyMode(0, "boot");

  configureWiFi();
  configureRoutes();
  s_server.begin();
  Serial.printf("[REMOTE] Web UI: http://%s/\n",
                localIpString().c_str());
}

void loop() {
  if (s_apMode)
    s_dnsServer.processNextRequest();
  s_server.handleClient();
  updateButton();
  updateHeatTimer();
  updateLed();
  updateRestart();
  delay(2);
}

} // namespace standalone
} // namespace iheaterlink
