#include "standalone/StandaloneApp.h"
#include "standalone/RemoteDashboard.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <IPAddress.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <string.h>

#include <hal/hal_arduino.h>
#include <integrations/common/link_integrations_manager.h>
#include <integrations/common/link_integrations_store.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#include "controller/RmtOutputAdapter.h"
#include "wifi_tx_power.h"
#include "version.h"

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
const char *kAutoTargetKey = "auto-target";
const char *kAutoBedThresholdKey = "auto-bed";

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

const float kModeTempsC[] = {
    0.0f, 55.0f, 60.0f, 65.0f, 70.0f, 75.0f, 80.0f, 85.0f,
};

DNSServer s_dnsServer;
WebServer s_server(80);
WebSocketsServer s_statusSocket(81);
RmtOutputAdapter s_output{RmtOutputConfig{}};
idryer::cloud::LinkIntegrationsStore s_integrationStore;
// The integration manager treats this as an optional publisher. Passing null
// keeps Bambu LAN MQTT and Moonraker entirely on the local network.
idryer::cloud::LinkIntegrationsManager s_integrations(nullptr,
                                                      &s_integrationStore);

uint8_t s_mode = 0;
bool s_apMode = false;
String s_apSsid;
String s_stationSsid;
String s_wifiCredentialSource = "none";
uint16_t s_timerMinutes = 0;
uint32_t s_timerOffAtMs = 0;
uint32_t s_restartAtMs = 0;
bool s_otaActive = false;
bool s_otaOk = false;
size_t s_otaBytesWritten = 0;
String s_otaError;
float s_autoTargetC = 60.0f;
uint8_t s_autoBedThresholdC = 100;

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
void resetLedPattern();

bool autoHeatAllowed(float bedTargetC) {
  return bedTargetC >= static_cast<float>(s_autoBedThresholdC);
}

void applyIntegrationTarget(float targetTempC, const char *source) {
  ControllerOutputCommand cmd{};
  if (targetTempC > 0.0f) {
    cmd.mode = ControllerOutputMode::TargetTemperature;
    cmd.targetTempC = targetTempC;
  } else {
    cmd.mode = ControllerOutputMode::Off;
  }
  s_mode = 0; // Integration targets are not one of the seven manual modes.
  s_timerOffAtMs = 0;
  s_output.apply(cmd);
  s_output.forceFrame();
  resetLedPattern();
  Serial.printf("[REMOTE] %s target=%.1fC pulse=%u\n", source,
                (double)cmd.targetTempC, s_output.getLastPulseCode());
}

void onMoonrakerVirtualChamber(
    void *, const idryer::cloud::VirtualChamberData &data) {
  // Kept only as a compatibility callback for older Moonraker installations.
  // RC3-style control below is driven by the bed *setpoint* in the full status.
  (void)data;
}

void onMoonrakerStatus(void *, const idryer::cloud::MoonrakerStatus &status) {
  if (s_integrations.getActive() != idryer::cloud::ActiveIntegration::Moonraker)
    return;
  applyIntegrationTarget(autoHeatAllowed(status.bedTarget) ? s_autoTargetC : 0.0f,
                         "moonraker auto");
}

void onBambuPrinterStatus(
    void *, const idryer::cloud::BambuPrinterStatus &status) {
  if (s_integrations.getActive() != idryer::cloud::ActiveIntegration::Bambu)
    return;
  applyIntegrationTarget(autoHeatAllowed(status.bedTarget) ? s_autoTargetC : 0.0f,
                         "bambu auto");
}

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

  if (s_integrations.getActive() != idryer::cloud::ActiveIntegration::None)
    s_integrations.setActive(idryer::cloud::ActiveIntegration::None);
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

void loadAutoSettings() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true))
    return;
  const float target = prefs.getFloat(kAutoTargetKey, 60.0f);
  const uint8_t threshold = prefs.getUChar(kAutoBedThresholdKey, 100);
  prefs.end();
  if (target >= 45.0f && target <= 80.0f)
    s_autoTargetC = target;
  if (threshold >= 40 && threshold <= 120)
    s_autoBedThresholdC = threshold;
}

bool saveAutoSettings(float target, int threshold) {
  if (target < 45.0f || target > 80.0f || threshold < 40 || threshold > 120)
    return false;
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false))
    return false;
  const bool ok = prefs.putFloat(kAutoTargetKey, target) > 0 &&
                  prefs.putUChar(kAutoBedThresholdKey, threshold) > 0;
  prefs.end();
  if (ok) {
    s_autoTargetC = target;
    s_autoBedThresholdC = static_cast<uint8_t>(threshold);
  }
  return ok;
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

String htmlEscape(const char *raw) {
  String out;
  for (const char *p = raw ? raw : ""; *p; ++p) {
    switch (*p) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '\"': out += "&quot;"; break;
    case '\'': out += "&#39;"; break;
    default: out += *p; break;
    }
  }
  return out;
}

void handleSetupPage() {
  if (captiveRedirectIfNeeded())
    return;
  const auto active = s_integrations.getActive();
  const auto &bambu = s_integrations.bambuConfig();
  const auto &moon = s_integrations.moonrakerConfig();
  const String source = active == idryer::cloud::ActiveIntegration::Bambu
      ? "bambu" : active == idryer::cloud::ActiveIntegration::Moonraker
      ? "moonraker" : "manual";
  String page = R"HTML(<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>iHeater setup</title><style>:root{color-scheme:dark;--bg:#090d14;--p:#101722;--l:#29384b;--t:#eaf1fa;--m:#91a2b8;--a:#5ba9ff;font-family:system-ui,sans-serif}body{margin:0;background:var(--bg);color:var(--t)}main{max-width:620px;margin:auto;padding:24px 15px}.card{background:var(--p);border:1px solid var(--l);border-radius:12px;padding:16px;margin:14px 0}h1{font-size:23px}h2{font-size:13px;text-transform:uppercase;letter-spacing:.08em;color:var(--m)}label{display:grid;gap:6px;margin:12px 0;color:var(--m);font-size:13px}input,select,button{min-height:43px;border-radius:8px;border:1px solid #38506b;background:#0c131d;color:var(--t);font:inherit;padding:0 10px}button{background:#1d5d99;border-color:#69b4ff;font-weight:700;width:100%;margin-top:8px}.note{font-size:13px;line-height:1.45;color:var(--m)}a{color:var(--a)}</style><main><p><a href="/">← Dashboard</a></p><h1>Wi-Fi &amp; printer setup</h1><p class=note>All binding configuration stays on this device. Blank password/access-code fields preserve their stored values.</p><form method=post action="/api/setup"><section class=card><h2>Wi-Fi</h2><label>Network name<input name=ssid value="__SSID__" maxlength=32></label><label>Password <input name=password type=password placeholder="(unchanged)" maxlength=64></label></section><section class=card><h2>Control source</h2><label>Bind this heater to<select name=type id=source onchange=showSource()><option value=manual __MANUAL__>Manual controls only</option><option value=moonraker __MOON__>Klipper (Moonraker)</option><option value=bambu __BAMBU__>Bambu (LAN)</option></select></label><div id=moon><label>Host / IP<input name=host value="__MOON_HOST__" maxlength=63></label><label>Port<input name=port type=number value="__MOON_PORT__" min=1 max=65535></label><label>API key <input name=apiKey type=password placeholder="(unchanged)" maxlength=64></label></div><div id=bambu><label>Printer IP<input name=ip value="__BAMBU_IP__" maxlength=15></label><label>Serial<input name=serial value="__BAMBU_SERIAL__" maxlength=19></label><label>LAN access code<input name=lanAccessCode type=password placeholder="(unchanged)" maxlength=15></label><p class=note>Enable LAN Only Mode on the printer and use its access code.</p></div></section><section class=card><h2>Auto control</h2><p class=note>With a printer source selected, heat engages when its bed setpoint reaches this threshold.</p><label>iHeater target (45–80 C)<input name=autoTargetC type=number value="__AUTO_TARGET__" min=45 max=80></label><label>Bed threshold (40–120 C)<input name=autoBedThresholdC type=number value="__AUTO_BED__" min=40 max=120></label></section><button>Save local setup</button></form></main><script>function showSource(){let s=document.getElementById('source').value;document.getElementById('moon').style.display=s==='moonraker'?'block':'none';document.getElementById('bambu').style.display=s==='bambu'?'block':'none'}showSource()</script></html>)HTML";
  page.replace("__SSID__", htmlEscape(s_stationSsid.c_str()));
  page.replace("__MOON_HOST__", htmlEscape(moon.host));
  page.replace("__MOON_PORT__", String(moon.port));
  page.replace("__BAMBU_IP__", htmlEscape(bambu.ip));
  page.replace("__BAMBU_SERIAL__", htmlEscape(bambu.serial));
  page.replace("__AUTO_TARGET__", String(s_autoTargetC, 0));
  page.replace("__AUTO_BED__", String(s_autoBedThresholdC));
  page.replace("<h1>Wi-Fi &amp; printer setup</h1>",
               "<div style='font-size:23px;font-weight:800;letter-spacing:-1px'><span style='color:#5ba9ff'>i</span>Dryer</div><h1>Wi-Fi &amp; printer setup</h1>");
  page.replace("</form></main>",
               String("</form><p class=note><a href='/fw'>Firmware update</a> · iHeater Remote v") + VERSION_STR + "</p></main>");
  page.replace("__MANUAL__", source == "manual" ? "selected" : "");
  page.replace("__MOON__", source == "moonraker" ? "selected" : "");
  page.replace("__BAMBU__", source == "bambu" ? "selected" : "");
  s_server.send(200, "text/html", page);
}

void handleFirmwarePage() {
  if (captiveRedirectIfNeeded())
    return;
  String page = R"HTML(<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>iDryer firmware update</title><style>:root{color-scheme:dark;--bg:#090d14;--p:#101722;--l:#29384b;--t:#eaf1fa;--m:#91a2b8;--a:#5ba9ff;--bad:#ff8893;font-family:system-ui,sans-serif}body{margin:0;background:var(--bg);color:var(--t)}main{max-width:560px;margin:auto;padding:36px 15px}.logo{font-size:24px;font-weight:800;letter-spacing:-1px}.logo i{font-style:normal;color:var(--a)}.card{background:var(--p);border:1px solid var(--l);border-radius:12px;padding:18px;margin:16px 0}h1{font-size:22px;margin:14px 0 7px}.note{color:var(--m);font-size:13px;line-height:1.5}input,button{box-sizing:border-box;width:100%;min-height:45px;border-radius:8px;border:1px solid #38506b;background:#0c131d;color:var(--t);font:inherit;padding:8px;margin:8px 0}button{background:#1d5d99;border-color:#69b4ff;font-weight:700;cursor:pointer}.warning{color:var(--bad)}a{color:var(--a)}</style><main><p><a href="/">← Dashboard</a></p><div class=logo><i>i</i>Dryer</div><h1>Firmware update</h1><p class=note>Installed: iHeater Remote v__VERSION__</p><section class=card><p class="note warning"><strong>Heating is stopped before the update is written.</strong> Do not disconnect power while the upload is in progress.</p><input id=file accept=".bin,application/octet-stream" type=file><button id=install>Install update</button><p class=note id=msg></p></section></main><script>const f=document.getElementById('file'),m=document.getElementById('msg');document.getElementById('install').onclick=()=>{let file=f.files[0];if(!file){m.textContent='Choose a firmware .bin first.';return}let x=new XMLHttpRequest(),d=new FormData();d.append('firmware',file,file.name);x.open('POST','/api/ota');x.upload.onprogress=e=>m.textContent=e.lengthComputable?'Uploading '+Math.round(e.loaded/e.total*100)+'%':'Uploading…';x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(_){}if(x.status>=200&&x.status<300){m.textContent='Update written. Rebooting…';setTimeout(waitForDevice,1500)}else m.textContent=r.error||'Update failed.'};x.onerror=()=>m.textContent='Update failed.';x.send(d)};function waitForDevice(){let end=Date.now()+120000,t=setInterval(async()=>{try{let r=await fetch('/',{cache:'no-store'});if(r.ok){clearInterval(t);location.href='/'}}catch(_){}if(Date.now()>end){clearInterval(t);m.textContent='The device is restarting. Reopen the dashboard when it is back online.'}},1000)}</script></main>)HTML";
  page.replace("__VERSION__", VERSION_STR);
  s_server.send(200, "text/html", page);
}

String statusJson() {
  updateHeatTimer();

  StaticJsonDocument<1024> doc;
  doc["mode"] = s_mode;
  const auto output = s_output.getLastCommand();
  doc["targetTempC"] = output.targetTempC;
  doc["heating"] = output.mode == ControllerOutputMode::TargetTemperature;
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
  doc["otaActive"] = s_otaActive;
  doc["otaOk"] = s_otaOk;
  doc["otaBytesWritten"] = s_otaBytesWritten;
  doc["otaError"] = s_otaError;
  doc["firmwareVersion"] = VERSION_STR;
  doc["integration"] = idryer::cloud::activeIntegrationToString(
      s_integrations.getActive());
  doc["bambuState"] = idryer::cloud::bambuConnectionStateToString(
      s_integrations.bambuState());
  doc["moonrakerState"] = idryer::cloud::moonrakerConnectionStateToString(
      s_integrations.moonrakerState());
  const auto &bambu = s_integrations.bambuPrinterStatus();
  doc["bambuFilament"] = bambu.trayType;
  doc["bambuPrintState"] = bambu.gcodeState;

  String out;
  serializeJson(doc, out);
  return out;
}

void sendActionStatus() {
  s_server.send(200, "application/json", statusJson());
}

void pushStatusIfChanged(bool force = false) {
  static String lastStatus;
  static uint32_t lastCheckMs = 0;
  const uint32_t now = millis();
  if (!force && now - lastCheckMs < 250)
    return;
  lastCheckMs = now;

  String status = statusJson();
  if (force || status != lastStatus) {
    s_statusSocket.broadcastTXT(status);
    lastStatus = status;
  }
}

void onStatusSocketEvent(uint8_t client, WStype_t type, uint8_t *, size_t) {
  if (type == WStype_CONNECTED) {
    String status = statusJson();
    s_statusSocket.sendTXT(client, status);
  }
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
  sendActionStatus();
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
  sendActionStatus();
}

bool applySetupForm(String &error) {
  const String type = s_server.arg("type");
  StaticJsonDocument<384> doc;
  doc["type"] = type;

  if (type == "bambu") {
    const auto &old = s_integrations.bambuConfig();
    const String ip = s_server.arg("ip");
    const String serial = s_server.arg("serial");
    const String code = s_server.arg("lanAccessCode");
    doc["enabled"] = true;
    doc["ip"] = ip.length() ? ip : old.ip;
    doc["serial"] = serial.length() ? serial : old.serial;
    // Secrets are write-only: a blank field explicitly means keep the stored code.
    doc["lanAccessCode"] = code.length() ? code : old.lanAccessCode;
    s_integrations.handleLinkIntegrationCommand(doc.as<JsonObjectConst>());
    if (!s_integrations.bambuConfig().configured()) {
      error = "Bambu requires IP, serial number, and LAN access code.";
      return false;
    }
    s_integrations.setActive(idryer::cloud::ActiveIntegration::Bambu);
  } else if (type == "moonraker") {
    const auto &old = s_integrations.moonrakerConfig();
    const int port = s_server.arg("port").toInt();
    const String host = s_server.arg("host");
    const String key = s_server.arg("apiKey");
    doc["enabled"] = true;
    doc["host"] = host.length() ? host : old.host;
    doc["port"] = port > 0 && port <= 65535 ? port : old.port;
    doc["apiKey"] = key.length() ? key : old.apiKey;
    doc["ssl"] = s_server.arg("ssl") == "1";
    s_integrations.handleLinkIntegrationCommand(doc.as<JsonObjectConst>());
    if (!s_integrations.moonrakerConfig().configured()) {
      error = "Moonraker requires a host or IP address.";
      return false;
    }
    s_integrations.setActive(idryer::cloud::ActiveIntegration::Moonraker);
  } else if (type == "manual") {
    s_integrations.setActive(idryer::cloud::ActiveIntegration::None);
    applyIntegrationTarget(0.0f, "binding disabled");
  } else {
    error = "Unknown control source.";
    return false;
  }

  const String target = s_server.arg("autoTargetC");
  const String threshold = s_server.arg("autoBedThresholdC");
  if ((target.length() || threshold.length()) &&
      !saveAutoSettings(target.length() ? target.toFloat() : s_autoTargetC,
                        threshold.length() ? threshold.toInt() : s_autoBedThresholdC)) {
    error = "Auto target must be 45–80 C and bed threshold 40–120 C.";
    return false;
  }
  return true;
}

void handleIntegrationPost() {
  String error;
  if (!applySetupForm(error)) {
    s_server.send(400, "application/json", String("{\"error\":\"") + error + "\"}");
    return;
  }
  sendActionStatus();
}

void handleSetupPost() {
  String error;
  if (!applySetupForm(error)) {
    s_server.send(400, "text/plain", error);
    return;
  }

  String ssid = s_server.arg("ssid");
  const String password = s_server.arg("password");
  ssid.trim();
  if (ssid.length()) {
    if (ssid.length() > kMaxWifiSsidLen || password.length() > kMaxWifiPasswordLen ||
        (password.length() && password.length() < 8)) {
      s_server.send(400, "text/plain", "Invalid Wi-Fi credentials.");
      return;
    }
    // An empty Wi-Fi password preserves the stored password only when the SSID
    // is unchanged; a new network still needs its password.
    if (!password.length() && ssid != s_stationSsid) {
      s_server.send(400, "text/plain", "Enter the password for a new Wi-Fi network.");
      return;
    }
    String storedPassword;
    if (!password.length())
      loadStoredWifiCredentials(s_stationSsid, storedPassword);
    if (!saveStoredWifiCredentials(ssid, password.length() ? password : storedPassword)) {
      s_server.send(500, "text/plain", "Could not save Wi-Fi credentials.");
      return;
    }
    s_stationSsid = ssid;
    s_wifiCredentialSource = "stored";
    scheduleRestart();
  }
  s_server.sendHeader("Location", "/setup", true);
  s_server.send(303, "text/plain", "Saved");
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
  sendActionStatus();
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
  sendActionStatus();
}

void handleOtaPost() {
  if (Update.hasError() || !s_otaOk) {
    StaticJsonDocument<192> doc;
    doc["error"] = s_otaError.length() ? s_otaError : Update.errorString();
    doc["bytesWritten"] = s_otaBytesWritten;

    String out;
    serializeJson(doc, out);
    s_server.send(500, "application/json", out);
    return;
  }

  scheduleRestart();

  StaticJsonDocument<192> doc;
  doc["ok"] = true;
  doc["bytesWritten"] = s_otaBytesWritten;
  doc["restartPending"] = true;

  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

void handleOtaUpload() {
  HTTPUpload &upload = s_server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    s_otaActive = true;
    s_otaOk = false;
    s_otaBytesWritten = 0;
    s_otaError = "";

    applyMode(0, "ota:start");
    Serial.printf("[REMOTE] OTA upload start filename=%s\n",
                  upload.filename.c_str());

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      s_otaError = Update.errorString();
      Serial.printf("[REMOTE] OTA begin failed: %s\n", s_otaError.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!Update.hasError()) {
      const size_t written = Update.write(upload.buf, upload.currentSize);
      s_otaBytesWritten += written;
      if (written != upload.currentSize) {
        s_otaError = Update.errorString();
        Serial.printf("[REMOTE] OTA write failed: %s\n", s_otaError.c_str());
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.hasError() && Update.end(true)) {
      s_otaOk = true;
      s_otaError = "";
      Serial.printf("[REMOTE] OTA upload complete bytes=%u\n",
                    (unsigned)s_otaBytesWritten);
    } else {
      s_otaOk = false;
      s_otaError = Update.errorString();
      Serial.printf("[REMOTE] OTA end failed: %s\n", s_otaError.c_str());
    }
    s_otaActive = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    s_otaActive = false;
    s_otaOk = false;
    s_otaError = "upload aborted";
    Serial.println("[REMOTE] OTA upload aborted");
  }
}

void handleRoot() {
  if (captiveRedirectIfNeeded())
    return;

  String page(FPSTR(kRemoteDashboardHtml));
  page.replace(
      "<div class=\"brand\" aria-label=\"iDryer\" style=\"width:60px;background:transparent;color:#eaf1fa;font-size:14px;letter-spacing:-1px\"><span style=\"color:#5ba9ff\">i</span>Dryer</div>",
      "<img class=\"brand\" src=\"/logo.png\" alt=\"iDryer\">");
  page.replace(
      "</style>",
      R"HTML(
/* Desktop refinement: anchor the interface, use the available canvas, and
   keep the navigation pictographic rather than repeating labels. */
.main{width:100%;max-width:1440px;margin:0 auto;padding:30px 42px}
.brand{object-fit:cover}
.connection{display:none}
.tab{font-size:0;position:relative;line-height:0}
.tab::before{content:"";position:absolute;inset:0;margin:auto;display:block;width:23px;height:23px;background:currentColor}
.tab[data-page=home]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m3 10 9-7 9 7'/%3E%3Cpath d='M5 9v11h14V9'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m3 10 9-7 9 7'/%3E%3Cpath d='M5 9v11h14V9'/%3E%3C/svg%3E") center/contain no-repeat}
.tab[data-page=heat]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 2c3 3 3 6 0 9'/%3E%3Cpath d='M7 5c3 3 3 7 0 11'/%3E%3Cpath d='M17 5c3 3 3 7 0 11'/%3E%3Cpath d='M4 19c4 2 12 2 16 0'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 2c3 3 3 6 0 9'/%3E%3Cpath d='M7 5c3 3 3 7 0 11'/%3E%3Cpath d='M17 5c3 3 3 7 0 11'/%3E%3Cpath d='M4 19c4 2 12 2 16 0'/%3E%3C/svg%3E") center/contain no-repeat}
.tab[data-page=timer]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Ccircle cx='12' cy='12' r='9'/%3E%3Cpath d='M12 7v5l3 2'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Ccircle cx='12' cy='12' r='9'/%3E%3Cpath d='M12 7v5l3 2'/%3E%3C/svg%3E") center/contain no-repeat}
.tab.settings::before{display:none}
.grid{grid-template-columns:minmax(0,1fr) minmax(0,1fr);align-items:start}
.grid>.card:first-child{grid-column:auto;display:block}
.grid>.card:first-child h2{margin-bottom:13px}
.grid>.card:first-child .details{margin-top:18px;grid-template-columns:repeat(2,minmax(0,1fr))}
.quick{grid-column:auto;align-self:stretch}
.quick .actions{grid-template-columns:repeat(2,minmax(0,1fr));align-content:start}
.footer a{color:inherit;text-decoration:none}.footer a:hover{color:var(--accent)}
@media(max-width:650px){.rail{height:58px;padding:7px;gap:8px}.brand{width:37px;height:37px;border-radius:10px}.tab{width:42px;min-width:42px;height:39px;flex:0 0 42px;padding:0}.tab::before{width:21px;height:21px}.tab.settings{margin:0 0 0 auto;font-size:22px!important}.main{padding:15px}.card{padding:13px}.state{font-size:22px}.details{gap:7px;margin-top:14px}.metric{padding:9px}.metric strong{font-size:18px}.quick .actions{gap:7px}}
@media(max-width:560px){.grid{grid-template-columns:1fr}.quick{grid-column:1/-1}.quick .actions{grid-template-columns:repeat(2,minmax(0,1fr))}}
</style>)HTML");
  page.replace("<button class=\"tab\" data-page=\"home\" aria-selected=\"true\">HOME</button>",
               "<button class=\"tab\" data-page=\"home\" aria-label=\"Dashboard\" title=\"Dashboard\" aria-selected=\"true\">HOME</button>");
  page.replace("<button class=\"tab\" data-page=\"heat\">HEAT</button>",
               "<button class=\"tab\" data-page=\"heat\" aria-label=\"Heat controls\" title=\"Heat controls\">HEAT</button>");
  page.replace("<button class=\"tab\" data-page=\"timer\">TIMER</button>",
               "<button class=\"tab\" data-page=\"timer\" aria-label=\"Timer\" title=\"Timer\">TIMER</button>");
  page.replace(
      "iDryer Remote <span id=\"version\">—</span> · This remote reports the command it sends to iHeater. Current temperatures and safety-chain state are not available on this hardware.",
      "<a href=\"https://github.com/justinh-rahb/iHeater-Remote\" target=\"_blank\" rel=\"noopener\">iHeater Remote <span id=\"version\">—</span></a>");
  s_server.send(200, "text/html", page);
  return; // The legacy dashboard remains below only for source-history context.

#if 0
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
input[type=file]{padding:10px 12px}
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
<section class="panel">
<div class="form">
<label class="wide" for="otaFile">Firmware .bin<input id="otaFile" accept=".bin,application/octet-stream" type="file"></label>
<button id="uploadOta">Update</button>
</div>
<p class="small" id="otaMessage"></p>
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
const otaFile=document.getElementById("otaFile");
const otaMessage=document.getElementById("otaMessage");
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
document.getElementById("uploadOta").onclick=uploadOta;
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
function uploadOta(){
  otaMessage.textContent="";
  if(!otaFile.files.length){
    otaMessage.textContent="Choose a firmware .bin first.";
    return;
  }

  const file=otaFile.files[0];
  const body=new FormData();
  body.append("firmware",file,file.name);

  const request=new XMLHttpRequest();
  request.open("POST","/api/ota");
  request.upload.onprogress=event=>{
    if(event.lengthComputable){
      otaMessage.textContent=`Uploading ${Math.round(event.loaded*100/event.total)}%`;
    }else{
      otaMessage.textContent=`Uploading ${event.loaded} bytes`;
    }
  };
  request.onload=()=>{
    let response={};
    try{ response=JSON.parse(request.responseText||"{}"); }catch(_){}
    if(request.status>=200&&request.status<300){
      otaMessage.textContent=`Update written (${response.bytesWritten||file.size} bytes). Rebooting...`;
      setTimeout(refresh,500);
    }else{
      otaMessage.textContent=response.error||"Update failed.";
    }
  };
  request.onerror=()=>{ otaMessage.textContent="Update failed."; };
  request.send(body);
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
  if(s.otaActive) otaMessage.textContent=`Updating (${s.otaBytesWritten} bytes written)`;
  if(s.otaError) otaMessage.textContent=s.otaError;
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
#endif
}

void configureRoutes() {
  s_server.serveStatic("/logo.png", LittleFS, "/logo.png");
  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/setup", HTTP_GET, handleSetupPage);
  s_server.on("/fw", HTTP_GET, handleFirmwarePage);
  s_server.on("/api/mode", HTTP_POST, handleModePost);
  s_server.on("/api/timer", HTTP_POST, handleTimerPost);
  s_server.on("/api/integration", HTTP_POST, handleIntegrationPost);
  s_server.on("/api/setup", HTTP_POST, handleSetupPost);
  s_server.on("/api/wifi", HTTP_POST, handleWifiPost);
  s_server.on("/api/wifi/clear", HTTP_POST, handleWifiClearPost);
  s_server.on("/api/ota", HTTP_POST, handleOtaPost, handleOtaUpload);
  s_server.on("/api/off", HTTP_POST, []() {
    applyMode(0, "web");
    sendActionStatus();
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
  applyConfiguredWifiTxPower("remote-ap");
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
    applyConfiguredWifiTxPower("remote-sta");
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

  if (!LittleFS.begin()) {
    Serial.println("[REMOTE] LittleFS unavailable; logo asset will not load");
  }

  configureWiFi();
  loadAutoSettings();
  idryer::hal::initArduinoHal(&Serial, false);
  s_integrations.setDeviceType(idryer::UartDeviceType::IHeaterLink);
  s_integrations.setVirtualChamberCallback(onMoonrakerVirtualChamber);
  s_integrations.setMoonrakerStatusCallback(onMoonrakerStatus);
  s_integrations.setBambuPrinterStatusCallback(onBambuPrinterStatus);
  s_integrations.begin();
  configureRoutes();
  s_server.begin();
  s_statusSocket.begin();
  s_statusSocket.onEvent(onStatusSocketEvent);
  Serial.printf("[REMOTE] Web UI: http://%s/\n",
                localIpString().c_str());
}

void loop() {
  if (s_apMode)
    s_dnsServer.processNextRequest();
  s_server.handleClient();
  s_statusSocket.loop();
  updateButton();
  updateHeatTimer();
  updateLed();
  updateRestart();
  s_integrations.loop();
  pushStatusIfChanged();
  delay(2);
}

} // namespace standalone
} // namespace iheaterlink
