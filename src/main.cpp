// iHeater Link — composition root.
//
// Паттерн — один-в-один с iDryer-Storage/src/main.cpp:
//   1. Аппаратный слой    — RmtOutputAdapter (RMT pulses → STM32 iHeater).
//   2. Меню + NVS         — MenuBridge::begin() загружает настройки до loop().
//   3. Фасад              — iDryer::Link (WiFi/MQTT/claim/integrations/local-WS).
//   4. MQTT-команды       — runtime()->setCommandHandler(handleMqttCommand)
//                           вызывается ПОСЛЕ begin(); покрывает ВСЕ команды.
//   5. Local-WS-команды   — onRequest(applyRequest); фасад маппит drying/stop/
//                           storage/find → typed Request → applyRequest.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <iDryer.h>
#include <idryer_integrations.h>
#include <runtime/idryer_runtime.h>
#include <integrations/common/link_integrations_types.h>

#include "controller/RmtOutputAdapter.h"
#include "heater/auto_heat.h"
#include "heater/MenuBridge.h"

// ── Конфигурация устройства ──────────────────────────────────────────────────
static const iDryer::Config CFG = {
    .deviceType = iDryer::DeviceType::IHeaterLink,
    .unitsCount = 1,

    // iHeater Link не имеет своих сенсоров — камеру меряет принтер.
    // hasHeaterPower=true: в telemetry шлём 0/1 по текущей команде RMT.
    .hasAirTemp      = false,
    .hasAirHumidity  = false,
    .hasHeaterTemp   = false,
    .hasHeaterPower  = true,
    .hasFanStatus    = false,
    .hasScales       = false,
    .hasRfid         = false,

    .allowHa         = true,
    .allowBambu      = true,
    .allowMoonraker  = true,

    .telemetryPeriodMs = 0,      // публикуем вручную (iheater_link-specific payload)
    .statusPeriodMs    = 5000,
};

// ── Аппаратный слой ──────────────────────────────────────────────────────────
// offCode=10 — «питание есть, нагрев выкл». RMT-таск шлёт кадр каждый
// framePeriodMs независимо от WiFi: STM32 знает что связь жива.
static iheaterlink::RmtOutputAdapter s_output{iheaterlink::RmtOutputConfig{}};

// Deadline авто-Off для команды Start (durationS из портала).
// 0 = таймер не активен.
static uint32_t s_dryingDeadlineMs = 0;

// Указатель на MenuBridge — инициализируется в setup() после begin().
static iheaterlink::MenuBridge* s_menuBridge = nullptr;

// ── Lazy-singleton фасада ────────────────────────────────────────────────────
// Создаётся при первом обращении — уже внутри setup(), когда Arduino runtime
// поднята. Прямая static-инициализация до main() на ESP32 undefined behaviour.
static iDryer::Link& device() {
    static iDryer::Link instance(CFG);
    return instance;
}

// ── Вспомогательная функция парсинга unitId ──────────────────────────────────
// "U1"–"U4" → 0–3; всё остальное (включая nullptr) → 0xFF (broadcast).
static uint8_t pickUnit(JsonObjectConst data) {
    const char* s = data["unitId"] | (const char*)nullptr;
    if (s && s[0] == 'U' && s[1] >= '1' && s[1] <= '4' && s[2] == '\0')
        return static_cast<uint8_t>(s[1] - '1');
    return 0;
}

// ── Shared apply-logic ────────────────────────────────────────────────────────
// Единственная точка применения бизнес-команды на железо + обновление
// device().status.* + немедленная публикация в портал.
// Вызывается из ДВУХ путей:
//   • handleMqttCommand  (MQTT → runtime.setCommandHandler)
// Применяет нагрев — общая логика для drying/storage. duration=0 → без deadline.
static void applyHeating(uint8_t u, float targetTempC, uint32_t durationS,
                         iDryer::UnitMode mode, const char* tag) {
    iheaterlink::ControllerOutputCommand cmd;
    cmd.mode        = iheaterlink::ControllerOutputMode::TargetTemperature;
    cmd.targetTempC = targetTempC;
    s_output.apply(cmd);
    s_dryingDeadlineMs = durationS ? (millis() + durationS * 1000u) : 0;
    device().status.mode[u]        = mode;
    device().status.targetTempC[u] = targetTempC;
    device().status.durationS[u]   = durationS;
    device().status.elapsedS[u]    = 0;
    device().telemetry.heaterPower01[u] = 1.0f;
    Serial.printf("[CMD] %s temp=%.1f duration=%us\n", tag, (double)targetTempC, durationS);
    device().publishStatusNow();
}

static void applyStop(uint8_t u) {
    iheaterlink::ControllerOutputCommand cmd;
    cmd.mode        = iheaterlink::ControllerOutputMode::Off;
    cmd.targetTempC = 0.0f;
    s_output.apply(cmd);
    s_dryingDeadlineMs = 0;
    device().status.mode[u]        = iDryer::UnitMode::Idle;
    device().status.targetTempC[u] = 0.0f;
    device().status.durationS[u]   = 0;
    device().status.elapsedS[u]    = 0;
    device().telemetry.heaterPower01[u] = 0.0f;
    Serial.println("[CMD] Stop");
    device().publishStatusNow();
}

// ── Парсинг target temperature из payload ────────────────────────────────────
// Портал шлёт {params:{temperature, duration}}, legacy — {targetTemperature}.
static float pickTemp(JsonObjectConst data) {
    JsonObjectConst params = data["params"];
    if (params && params["temperature"].is<float>())
        return params["temperature"].as<float>();
    if (data["targetTemperature"].is<float>())
        return data["targetTemperature"].as<float>();
    return 0.0f;
}

// Длительность в секундах (портал шлёт минуты).
static uint32_t pickDurationSec(JsonObjectConst data) {
    JsonObjectConst params = data["params"];
    uint32_t durMin = 0;
    if (params && params["duration"].is<uint32_t>())
        durMin = params["duration"].as<uint32_t>();
    else if (data["durationMinutes"].is<uint32_t>())
        durMin = data["durationMinutes"].as<uint32_t>();
    return durMin * 60u;
}

// ── iHeater Link–специфичная телеметрия ──────────────────────────────────────
// Публикует поля, которых нет в стандартном idryer-core payload:
//   outputMode, targetTempC, active — бэкенд использует их для heaterIntent
//   (telemetry.handler.ts → broadcastTelemetryUpdate → фронт HeaterDashboardCard).
// temperature/humidity в units[] = 0: датчиков нет, но поля нужны бэкенду.
// CFG.telemetryPeriodMs = 0, чтобы idryer-core не публиковал свой урезанный payload.
static void publishIHeaterTelemetry() {
    auto* mqttClient = device().mqttClient();
    if (!mqttClient) return;

    const auto cmd = s_output.getLastCommand();
    const bool heating = (cmd.mode == iheaterlink::ControllerOutputMode::TargetTemperature);

    using AI = idryer::cloud::ActiveIntegration;
    const AI activeAI = device().integrationsManager()->getActive();

    StaticJsonDocument<384> doc;
    doc["deviceType"] = "iheater_link";
    doc["active"]     = idryer::cloud::activeIntegrationToString(activeAI);
    doc["outputMode"] = heating ? 1 : 0;
    doc["targetTempC"]= cmd.targetTempC;

    JsonArray units = doc.createNestedArray("units");
    JsonObject u1   = units.createNestedObject();
    u1["unitId"]    = "U1";
    u1["temperature"]= 0;
    u1["humidity"]  = 0;
    u1["heaterPower"]= heating ? 100 : 0;
    u1["fanStatus"] = false;

    mqttClient->publishTelemetry(doc);
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // 1. RMT-выход поднимаем первым: устройство держит pulse=Off независимо
    //    от состояния WiFi/MQTT.
    s_output.begin();

    // 2. Поднять весь SDK-стек: WiFi/Improv → claim → MQTT → integrations →
    //    LAN-WS. После begin() устройство уже тянется к облаку.
    device().begin();

    // 3. MenuBridge — NVS + дефолты + загрузка сохранённых значений.
    //    mqttClient() доступен только после begin().
    //    begin() внутри вызывает emitActiveIfChanged() → setActive() на mgr,
    //    поэтому wire активного соединения — ДО begin().
    static iheaterlink::MenuBridge s_menuBridgeInst(device().mqttClient());
    s_menuBridge = &s_menuBridgeInst;

    auto* mgr = device().integrationsManager();
    s_menuBridgeInst.setActiveConnectionCallback(
        [mgr](iheaterlink::ActiveConnection kind) {
            using AI = idryer::cloud::ActiveIntegration;
            AI target = AI::None;
            switch (kind) {
                case iheaterlink::ActiveConnection::Bambu:
                    target = AI::Bambu;     break;
                case iheaterlink::ActiveConnection::Moonraker:
                    target = AI::Moonraker; break;
                case iheaterlink::ActiveConnection::Ha:
                    target = AI::Ha;        break;
                default: break;
            }
            mgr->setActive(target);
        });
    s_menuBridgeInst.begin();   // загружает NVS, эмитит активную интеграцию

    // 4. Авто-нагрев: VirtualChamber (Moonraker) и BambuPrinterStatus → RMT.
    //    wireAutoHeat сохраняет указатель на s_output до подписки колбэков.
    iheaterlink::wireAutoHeat(&s_output);
    mgr->setVirtualChamberCallback(iheaterlink::onVirtualChamberUpdate);
    mgr->setBambuPrinterStatusCallback(iheaterlink::onBambuPrinterStatusUpdate);

    // 5. Команды портала / Local-WS — единый путь через onCommand.
    //    Built-in (link_integration/bambu_apply/ping) обрабатывает либа сама,
    //    наши onCommand-callback'и вызываются как post-hook'и.
    auto& link = device();

    link.onCommand("drying", [](JsonObjectConst data) {
        applyHeating(pickUnit(data), pickTemp(data), pickDurationSec(data),
                     iDryer::UnitMode::Drying, "Start");
    });
    link.onCommand("storage", [](JsonObjectConst data) {
        applyHeating(pickUnit(data), pickTemp(data), 0,
                     iDryer::UnitMode::Storage, "Storage");
    });
    link.onCommand("stop", [](JsonObjectConst data) {
        applyStop(pickUnit(data));
    });
    link.onCommand("find",         [](JsonObjectConst) { /* iHeater Link не имеет индикатора */ });
    link.onCommand("clear_errors", [](JsonObjectConst) { /* нет UART-ошибок */ });

    // get_config / set — меню iHeater Link (mat_*, *_en, host/port интеграций).
    link.onCommand("get_config", [](JsonObjectConst) {
        if (s_menuBridge) s_menuBridge->publishFullConfig();
    });
    link.onCommand("set", [](JsonObjectConst data) {
        if (s_menuBridge) s_menuBridge->applySetCommand(data);
    });

    // link_integration: сама команда обрабатывается built-in, здесь только
    // post-hook — sync menu-toggle (bambu_en/moon_en/ha_en) для эксклюзивности.
    link.onCommand("link_integration", [](JsonObjectConst data) {
        const char* type    = data["type"]    | (const char*)nullptr;
        const bool  enabled = data["enabled"] | false;
        if (!type || !enabled || !s_menuBridge) return;
        const char* bind = nullptr;
        if      (strcmp(type, "moonraker") == 0) bind = "moon_en";
        else if (strcmp(type, "bambu")     == 0) bind = "bambu_en";
        else if (strcmp(type, "ha")        == 0) bind = "ha_en";
        if (!bind) return;
        StaticJsonDocument<32> doc;
        doc["bind"] = bind;
        doc["val"]  = true;
        s_menuBridge->applySetCommand(doc.as<JsonObjectConst>());
    });
}

void loop() {
    device().loop();   // WiFi/MQTT/LocalAccess + auto-status

    // Ручная телеметрия каждые 5 сек (iheater_link-specific payload).
    static uint32_t s_lastTelemetryMs = 0;
    if ((uint32_t)(millis() - s_lastTelemetryMs) >= 5000u) {
        s_lastTelemetryMs = millis();
        publishIHeaterTelemetry();
    }

    // Тикаем elapsedS раз в секунду пока активный режим.
    static uint32_t s_lastTickMs = 0;
    if ((uint32_t)(millis() - s_lastTickMs) >= 1000u) {
        s_lastTickMs = millis();
        const auto m = device().status.mode[0];
        if (m == iDryer::UnitMode::Drying || m == iDryer::UnitMode::Storage)
            device().status.elapsedS[0]++;
    }

    // Авто-Off по deadline сушки. Сравнение со знаком корректно при wrap millis().
    if (s_dryingDeadlineMs && (int32_t)(millis() - s_dryingDeadlineMs) >= 0) {
        iheaterlink::ControllerOutputCommand cmd;
        cmd.mode        = iheaterlink::ControllerOutputMode::Off;
        cmd.targetTempC = 0.0f;
        s_output.apply(cmd);
        s_dryingDeadlineMs = 0;

        device().status.mode[0]        = iDryer::UnitMode::Idle;
        device().status.targetTempC[0] = 0.0f;
        device().status.durationS[0]   = 0;
        device().status.elapsedS[0]    = 0;
        device().telemetry.heaterPower01[0] = 0.0f;
        device().publishStatusNow();
        Serial.println("[CMD] drying deadline expired → Off");
    }
}
