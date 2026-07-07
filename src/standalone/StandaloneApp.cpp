#include "standalone/StandaloneApp.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <IPAddress.h>
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
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kMultiClickWindowMs = 650;
constexpr uint32_t kLongPressMs = 2000;
constexpr uint32_t kLedOnMs = 80;
constexpr uint32_t kLedOffMs = 120;
constexpr uint32_t kLedPauseMs = 1600;
constexpr uint16_t kDnsPort = 53;

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

  Serial.printf("[STANDALONE] %s mode=%u target=%.1fC pulse=%u\n", source,
                mode, (double)cmd.targetTempC, s_output.getLastPulseCode());
}

String localIpString() {
  return s_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

const char *wifiModeString() { return s_apMode ? "ap" : "sta"; }

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
  StaticJsonDocument<384> doc;
  doc["mode"] = s_mode;
  doc["targetTempC"] = kModeTempsC[s_mode];
  doc["heating"] = s_mode != 0;
  doc["pulseCode"] = s_output.getLastPulseCode();
  doc["rmtEnabled"] = s_output.isEnabled();
  doc["wifiMode"] = wifiModeString();
  doc["ip"] = localIpString();
  doc["ssid"] = s_apMode ? s_apSsid : WiFi.SSID();
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

void handleRoot() {
  if (captiveRedirectIfNeeded())
    return;

  static const char page[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>iHeater Standalone</title>
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
button{appearance:none;border:1px solid #3a4643;border-radius:6px;background:#22302c;color:#f6fbf9;min-height:48px;font:inherit;font-weight:700;cursor:pointer}
button:hover{background:#2b3b36}
button.active{border-color:#8fd0bb;background:#1c4a3e}
button.stop{grid-column:span 4;background:#4b2424;border-color:#724141}
.small{color:#9fb0aa;font-size:14px;line-height:1.45}
@media (max-width:560px){.status,.modes{grid-template-columns:1fr 1fr}button.stop{grid-column:span 2}}
</style>
</head>
<body>
<main>
<h1>iHeater Standalone</h1>
<section class="panel status">
<div class="metric"><span class="label">Mode</span><span class="value" id="mode">-</span></div>
<div class="metric"><span class="label">Target</span><span class="value" id="target">-</span></div>
<div class="metric"><span class="label">Network</span><span class="value" id="network">-</span></div>
<div class="metric"><span class="label">Pulse</span><span class="value" id="pulse">-</span></div>
</section>
<section class="panel">
<div class="modes" id="modes"></div>
</section>
<p class="small" id="detail"></p>
</main>
<script>
const temps=[0,55,60,65,70,75,80,85];
const modes=document.getElementById("modes");
temps.forEach((temp,mode)=>{
  const b=document.createElement("button");
  b.textContent=mode===0?"Off":`Mode ${mode} - ${temp} C`;
  b.dataset.mode=mode;
  b.className=mode===0?"stop":"";
  b.onclick=()=>fetch(`/api/mode?mode=${mode}`,{method:"POST"}).then(refresh);
  modes.appendChild(b);
});
async function refresh(){
  const s=await fetch("/api/status",{cache:"no-store"}).then(r=>r.json());
  document.getElementById("mode").textContent=s.mode===0?"Off":`MODE_TEMP_${s.mode}`;
  document.getElementById("target").textContent=s.mode===0?"0 C":`${s.targetTempC} C`;
  document.getElementById("network").textContent=s.wifiMode.toUpperCase();
  document.getElementById("pulse").textContent=s.pulseCode;
  document.getElementById("detail").textContent=`${s.ssid} - ${s.ip}`;
  document.querySelectorAll("button[data-mode]").forEach(b=>b.classList.toggle("active",Number(b.dataset.mode)===s.mode));
}
refresh();
setInterval(refresh,2000);
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
  snprintf(ssid, sizeof(ssid), "iHeater-Standalone-%04X",
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

  Serial.printf("[STANDALONE] AP SSID=%s IP=%s\n", ssid,
                WiFi.softAPIP().toString().c_str());
  Serial.println("[STANDALONE] Captive portal DNS started");
}

void configureWiFi() {
  WiFi.persistent(false);
  WiFi.setSleep(false);

#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[STANDALONE] Connecting to WiFi SSID=%s\n", WIFI_SSID);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (uint32_t)(millis() - start) < kWifiConnectTimeoutMs) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    s_apMode = false;
    Serial.printf("[STANDALONE] WiFi connected IP=%s\n",
                  WiFi.localIP().toString().c_str());
    return;
  }

  Serial.println("[STANDALONE] WiFi connect timeout, starting AP");
  WiFi.disconnect(true);
#endif

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
  Serial.println("[STANDALONE] iHeater local firmware starting");

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
  Serial.printf("[STANDALONE] Web UI: http://%s/\n",
                localIpString().c_str());
}

void loop() {
  if (s_apMode)
    s_dnsServer.processNextRequest();
  s_server.handleClient();
  updateButton();
  updateLed();
  delay(2);
}

} // namespace standalone
} // namespace iheaterlink
