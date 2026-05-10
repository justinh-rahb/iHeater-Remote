// iHeater Link — точка входа: RMT-выход → SDK begin() → MenuBridge →
// onCommand-обработчики.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <iDryer.h>
#include <idryer_integrations.h>
#include <integrations/common/link_integrations_types.h>
#include <runtime/idryer_runtime.h>

#include <core/idryer_errbus.h>

#include "controller/RmtOutputAdapter.h"
#include "heater/MenuBridge.h"
#include "heater/auto_heat.h"

#include <menu_state.h>         // menu.log_portal / log_printer / log_device / log_debug
#include <hal/hal_types.h>      // g_hal->logger->setLevel()

// ── Конфигурация устройства ──────────────────────────────────────────────────
// Заполняется один раз при портировании прошивки на новый продукт.
// После изменения — перепрошить устройство; портал подхватит при следующем
// boot.
static const iDryer::Config CFG = {
    // Тип устройства — определяет логику карточки на портале.
    // Варианты: Dryer, Heater, StorageLink, IHeaterLink.
    .deviceType = iDryer::DeviceType::IHeaterLink,

    // Количество независимых камер с нагревателем (1 для iHeater Link).
    .unitsCount = 1,

    // ── Периферия ────────────────────────────────────────────────────────────
    // iHeater Link не имеет своих сенсоров — камеру меряет принтер.
    // hasHeaterPower=true: в telemetry шлём 0/1 по текущей команде RMT.
    .hasHeaterPower = true,  // управляемый нагреватель (мощность 0..1)
    .hasFanStatus = false,   // вентилятор
    .hasLed = false,         // адресная LED-лента
    .hasScales = false,      // весовой датчик
    .hasRfid = false,        // RFID-метка катушки
    .hasAirTemp = false,     // датчик температуры воздуха
    .hasAirHumidity = false, // датчик влажности воздуха
    .hasHeaterTemp = false,  // датчик температуры нагревателя

    // ── Интеграции ───────────────────────────────────────────────────────────
    .allowHa = true,        // Home Assistant MQTT
    .allowBambu = true,     // Bambu Lab LAN MQTT
    .allowMoonraker = true, // Moonraker WebSocket

    // ── Периоды публикации ───────────────────────────────────────────────────
    .telemetryPeriodMs = 5000,
    .statusPeriodMs = 5000,

    // ── Идентификация (отображается на портале) ──────────────────────────────
    .hardwareVersion = "LINK-v1",
    .firmwareVersion = "1.0.0",

    // Название продукта — отображается в колонке «Тип» на странице устройств.
    // Задаётся свободно: любая строка UTF-8.
    .model = "iHeater Link",
};

// Флаг portal-логирования: управляется через меню → bind log_portal.
static bool s_logPortal = false;

// RMT-выход на STM32 iHeater; шлёт кадр keepalive каждые framePeriodMs
// независимо от WiFi.
static iheaterlink::RmtOutputAdapter s_output{iheaterlink::RmtOutputConfig{}};

// Deadline авто-Off для команды Start (durationS из портала).
// 0 = таймер не активен.
static uint32_t s_dryingDeadlineMs = 0;

// Указатель на MenuBridge — инициализируется в setup() после begin().
static iheaterlink::MenuBridge *s_menuBridge = nullptr;

// Счётчик для ротации тестовых ошибок (0..3).
static uint8_t s_errCycle = 0;

// Единственный экземпляр Link: создаётся при первом вызове (в setup)
static iDryer::Link &device() {
  static iDryer::Link instance(CFG);
  return instance;
}

// "U1"–"U4" → 0–3; иначе → 0 (нет unitId → первый юнит).
static uint8_t pickUnit(JsonObjectConst data) {
  const char *s = data["unitId"] | (const char *)nullptr;
  if (s && s[0] == 'U' && s[1] >= '1' && s[1] <= '4' && s[2] == '\0')
    return static_cast<uint8_t>(s[1] - '1');
  return 0;
}

// Применяет нагрев: RMT-команда + обновление status + publishStatusNow().
// duration=0 → без таймера.
static void applyHeating(uint8_t u, float targetTempC, uint32_t durationS,
                         iDryer::UnitMode mode, const char *tag) {
  iheaterlink::ControllerOutputCommand cmd;
  cmd.mode = iheaterlink::ControllerOutputMode::TargetTemperature;
  cmd.targetTempC = targetTempC;
  s_output.apply(cmd);
  s_dryingDeadlineMs = durationS ? (millis() + durationS * 1000u) : 0;
  device().status.mode[u] = mode;
  device().status.targetTempC[u] = targetTempC;
  device().status.durationS[u] = durationS;
  device().status.elapsedS[u] = 0;
  device().telemetry.heaterPower01[u] = 1.0f;
  Serial.printf("[CMD] %s temp=%.1f duration=%us\n", tag, (double)targetTempC,
                durationS);
  device().publishStatusNow();
}

static void applyStop(uint8_t u) {
  iheaterlink::ControllerOutputCommand cmd;
  cmd.mode = iheaterlink::ControllerOutputMode::Off;
  cmd.targetTempC = 0.0f;
  s_output.apply(cmd);
  s_dryingDeadlineMs = 0;
  device().status.mode[u] = iDryer::UnitMode::Idle;
  device().status.targetTempC[u] = 0.0f;
  device().status.durationS[u] = 0;
  device().status.elapsedS[u] = 0;
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

// Добавляет deviceType/active/outputMode/targetTempC в каждый пакет телеметрии.
static void enrichTelemetry(JsonObject root) {
  const auto cmd = s_output.getLastCommand();
  const bool heating =
      (cmd.mode == iheaterlink::ControllerOutputMode::TargetTemperature);

  using AI = idryer::cloud::ActiveIntegration;
  const AI activeAI = device().integrationsManager()->getActive();

  root["deviceType"] = iDryer::deviceTypeToString(CFG.deviceType);
  root["active"] = idryer::cloud::activeIntegrationToString(activeAI);
  root["outputMode"] = heating ? 1 : 0;
  root["targetTempC"] = cmd.targetTempC;
}

// Синхронизирует флаги логирования из меню → клиентов интеграций.
// Вызывается при старте (после MenuBridge::begin) и после каждого set-команды.
static void applyLogFlags() {
    s_logPortal = (bool)menu.log_portal;
    auto* mgr = device().integrationsManager();
    if (mgr) mgr->setLogPayloads((bool)menu.log_printer);
    iheaterlink::setLogDecisions((bool)menu.log_device);
    if (idryer::hal::g_hal && idryer::hal::g_hal->logger) {
        idryer::hal::g_hal->logger->setLevel(
            menu.log_debug
                ? idryer::hal::LogLevel::Debug
                : idryer::hal::LogLevel::Info);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  // 1. RMT-выход поднимаем первым: устройство держит pulse=Off независимо
  // от состояния WiFi/MQTT.
  s_output.begin();

  // 2. Поднять весь SDK-стек: WiFi/Improv → claim → MQTT → integrations →
  // LAN-WS. После begin() устройство уже тянется к облаку.
  device().onClaimPin([](const char* pin, uint32_t expires) {
      Serial.printf("CLAIM_PIN:%s:%lu\n", pin, expires);
      Serial.flush();
  });
  device().begin();

  // 3. MenuBridge: загружает NVS, эмитит активную интеграцию. Колбэк
  // назначается ДО begin().
  static iheaterlink::MenuBridge s_menuBridgeInst(device().mqttClient());
  s_menuBridge = &s_menuBridgeInst;

  auto *mgr = device().integrationsManager();
  s_menuBridgeInst.setActiveConnectionCallback(
      [mgr](iheaterlink::ActiveConnection kind) {
        using AI = idryer::cloud::ActiveIntegration;
        AI target = AI::None;
        switch (kind) {
        case iheaterlink::ActiveConnection::Bambu:
          target = AI::Bambu;
          break;
        case iheaterlink::ActiveConnection::Moonraker:
          target = AI::Moonraker;
          break;
        case iheaterlink::ActiveConnection::Ha:
          target = AI::Ha;
          break;
        default:
          break;
        }
        mgr->setActive(target);
      });
  s_menuBridgeInst.begin(); // загружает NVS, эмитит активную интеграцию
  applyLogFlags();          // синхронизирует log_portal/log_printer/log_device из NVS

  // 4. Авто-нагрев: VirtualChamber (Moonraker) и BambuPrinterStatus → RMT.
  //    wireAutoHeat сохраняет указатель на s_output до подписки колбэков.
  iheaterlink::wireAutoHeat(&s_output);
  mgr->setVirtualChamberCallback(iheaterlink::onVirtualChamberUpdate);
  mgr->setBambuPrinterStatusCallback(iheaterlink::onBambuPrinterStatusUpdate);

  // 5. Добавляем deviceType/active/outputMode/targetTempC в каждый publish
  // телеметрии.
  device().onTelemetryPublish(enrichTelemetry);

  // 6. Периодические задачи через cooperative scheduler фасада.
  //    Тик elapsedS раз в секунду, пока режим активен.
  device().every(1000, []() {
    const auto m = device().status.mode[0];
    if (m == iDryer::UnitMode::Drying || m == iDryer::UnitMode::Storage)
      device().status.elapsedS[0]++;
  });
  //    Авто-Off по deadline drying — раз в полсекунды проверяем дедлайн.
  device().every(500, []() {
    if (!s_dryingDeadlineMs)
      return;
    if ((int32_t)(millis() - s_dryingDeadlineMs) < 0)
      return;
    iheaterlink::ControllerOutputCommand cmd;
    cmd.mode = iheaterlink::ControllerOutputMode::Off;
    cmd.targetTempC = 0.0f;
    s_output.apply(cmd);
    s_dryingDeadlineMs = 0;
    device().status.mode[0] = iDryer::UnitMode::Idle;
    device().status.targetTempC[0] = 0.0f;
    device().status.durationS[0] = 0;
    device().status.elapsedS[0] = 0;
    device().telemetry.heaterPower01[0] = 0.0f;
    device().publishStatusNow();
    Serial.println("[CMD] drying deadline expired → Off");
  });

  // 7. HA controls — продуктовые кнопки в HA UI. Публикуются автоматически
  //    при HA-коннекте, нажатия маршрутизируются в наш callback.
  auto &ha = device().ha();
  ha.button(
      "heat_50", "Heat 50°C",
      []() { applyHeating(0, 50.0f, 0, iDryer::UnitMode::Drying, "Start"); },
      "mdi:thermometer");
  ha.button(
      "heat_55", "Heat 55°C",
      []() { applyHeating(0, 55.0f, 0, iDryer::UnitMode::Drying, "Start"); },
      "mdi:thermometer");
  ha.button(
      "heat_60", "Heat 60°C",
      []() { applyHeating(0, 60.0f, 0, iDryer::UnitMode::Drying, "Start"); },
      "mdi:thermometer-high");
  ha.button("stop", "Stop", []() { applyStop(0); }, "mdi:stop-circle");

  // 8. Команды портала / Local-WS — обработчики через onCommand.
  auto &link = device();

  link.onCommand("drying", [](JsonObjectConst data) {
    applyHeating(pickUnit(data), pickTemp(data), pickDurationSec(data),
                 iDryer::UnitMode::Drying, "Start");
  });
  link.onCommand("storage", [](JsonObjectConst data) {
    applyHeating(pickUnit(data), pickTemp(data), 0, iDryer::UnitMode::Storage,
                 "Storage");
  });
  link.onCommand("stop",
                 [](JsonObjectConst data) { applyStop(pickUnit(data)); });
  link.onCommand(
      "find", [](JsonObjectConst) { /* iHeater Link не имеет индикатора */ });
  link.onCommand("clear_errors", [](JsonObjectConst) { /* нет UART-ошибок */ });

  // get_config / set / invoke — команды управления меню (menu_protocol_v1).
  link.onCommand("get_config", [](JsonObjectConst) {
    if (s_menuBridge)
      s_menuBridge->publishFullConfig();
  });
  link.onCommand("set", [](JsonObjectConst data) {
    if (s_logPortal) {
      char buf[256];
      serializeJson(data, buf, sizeof(buf));
      HAL_LOG_INFO("PORTAL", "← set %s", buf);
    }
    if (s_menuBridge)
      s_menuBridge->applySetCommand(data);
    applyLogFlags();
  });
  // invoke: heat.start/heat.stop — здесь; остальные действия → MenuBridge.
  link.onCommand("invoke", [](JsonObjectConst data) {
    const char *action = data["action"] | "";
    JsonObjectConst args = data["args"];

    if (strcmp(action, "heat.start") == 0) {
      float tempC = args["tempC"] | 0.0f;
      uint32_t durMin = args["durationMin"] | 0u;
      applyHeating(0, tempC, durMin * 60u, iDryer::UnitMode::Drying,
                   "invoke:heat.start");
    } else if (strcmp(action, "heat.stop") == 0) {
      applyStop(0);
    } else if (s_menuBridge) {
      s_menuBridge->applyInvokeCommand(data);
    }
  });

  // link_integration: либа переключает интеграцию, здесь — синхронизируем
  // меню-тогглы.
  link.onCommand("link_integration", [](JsonObjectConst data) {
    if (s_logPortal) {
      char buf[256];
      serializeJson(data, buf, sizeof(buf));
      HAL_LOG_INFO("PORTAL", "← link_integration %s", buf);
    }
    const char *type = data["type"] | (const char *)nullptr;
    const bool enabled = data["enabled"] | false;
    if (!type || !enabled || !s_menuBridge)
      return;
    const char *bind = nullptr;
    if (strcmp(type, "moonraker") == 0)
      bind = "moon_en";
    else if (strcmp(type, "bambu") == 0)
      bind = "bambu_en";
    else if (strcmp(type, "ha") == 0)
      bind = "ha_en";
    if (!bind)
      return;
    StaticJsonDocument<32> doc;
    doc["bind"] = bind;
    doc["val"] = true;
    s_menuBridge->applySetCommand(doc.as<JsonObjectConst>());
  });

  // 9. Шина ошибок → raiseEvent() → MQTT events topic.
  //    ErrSeverity CRITICAL не имеет аналога в EventKind — маппим в Error.
  error_set_handler([](const ErrorEvent *ev) {
    iDryer::EventKind kind;
    switch (ev->severity) {
    case ERRSEV_INFO:
      kind = iDryer::EventKind::Info;
      break;
    case ERRSEV_WARNING:
      kind = iDryer::EventKind::Warning;
      break;
    default:
      kind = iDryer::EventKind::Error;
      break;
    }
    char event_key[48];
    snprintf(event_key, sizeof(event_key), "%s_%s", errsrc_name(ev->source),
             errcode_name(ev->code));
    device().raiseEvent(kind, event_key, ev->msg, ev->ctrl_id);
  });

  // 10. Тестовые ошибки — закомментировано, верификация пройдена.
  // device().every(20000, []() {
  //     static const ErrSeverity kSev[]  = { ERRSEV_INFO, ERRSEV_WARNING,
  //     ERRSEV_ERROR, ERRSEV_CRITICAL }; static const ErrSource   kSrc[]  = {
  //     ERRSRC_HEATER, ERRSRC_THERM, ERRSRC_AIR, ERRSRC_LINK }; static const
  //     ErrCode     kCode[] = { ERRC_OVER_MAX, ERRC_SENSOR_INVALID,
  //     ERRC_TIMEOUT, ERRC_NO_RESPONSE }; static const char*       kMsg[]  = {
  //         "test: heater over max", "test: sensor invalid",
  //         "test: air timeout",     "test: link no response"
  //     };
  //     const uint8_t i = s_errCycle++ & 3;
  //     POST_ERROR(kSev[i], 0, kSrc[i], kCode[i], kMsg[i],
  //     static_cast<int32_t>(i * 100));
  // });
}

void loop() {
  device()
      .loop(); // WiFi/MQTT/LocalAccess + auto-telemetry/status + every() tasks
  error_process_all(); // шина ошибок → error_set_handler → raiseEvent
}
