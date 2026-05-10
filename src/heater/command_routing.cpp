#if defined(ESP32) || defined(ESP_PLATFORM)

#include "heater/command_routing.h"
#include "heater/MenuBridge.h"
#include "controller/RmtOutputAdapter.h"

#include <Arduino.h>
#include <string.h>

#include <hal/hal_types.h>
#include <integrations/common/link_integrations_manager.h>

namespace iheaterlink {

namespace {

MenuBridge*                              g_menu         = nullptr;
idryer::cloud::LinkIntegrationsManager* g_integrations = nullptr;
RmtOutputAdapter*                       g_output       = nullptr;

// Время авто-выключения нагрева (ms). 0 = таймер не активен.
uint32_t g_dryingDeadlineMs = 0;

// "U1".."U4" → 0..3, иначе 0xFF.
uint8_t parseUnitId(const char* s) {
    if (!s) return 0xFF;
    if (s[0] == 'U' && s[1] >= '1' && s[1] <= '4') return s[1] - '1';
    return 0xFF;
}

// `commands/drying` — ручной нагрев с порталa.
// Поведение 1-в-1 как legacy CommandHandler::handleStart + LinkCommandSink Start:
//   params.temperature (°C, int) — цель; default 55.
//   params.duration   (минуты, int) — 0 = бесконечно; default 240.
//   Fallback shape: targetTemperature (float °C), durationMinutes (int).
void applyDrying(JsonObjectConst data) {
    if (!g_output) return;

    int    tempC      = 55;
    int    durationMin = 240;
    bool   fromParams = false;

    JsonObjectConst params = data["params"];
    if (params) {
        fromParams = true;
        if (params["temperature"].is<int>())
            tempC = params["temperature"].as<int>();
        if (params["duration"].is<int>())
            durationMin = params["duration"].as<int>();
    } else {
        if (data["targetTemperature"].is<float>())
            tempC = (int)data["targetTemperature"].as<float>();
        if (data["durationMinutes"].is<int>())
            durationMin = data["durationMinutes"].as<int>();
    }

    const uint8_t unitId = data["unitId"].is<const char*>()
                         ? parseUnitId(data["unitId"].as<const char*>())
                         : 0xFF;

    ControllerOutputCommand cmd{};
    cmd.mode        = ControllerOutputMode::TargetTemperature;
    cmd.targetTempC = (float)tempC;
    g_output->apply(cmd);

    if (durationMin > 0) {
        g_dryingDeadlineMs = millis() + (uint32_t)durationMin * 60u * 1000u;
        HAL_LOG_INFO("CMD",
                     "drying applied: unit=%s temp=%dC duration=%dmin (%s)",
                     unitId == 0xFF ? "ALL" : "Ux", tempC, durationMin,
                     fromParams ? "params" : "fallback");
    } else {
        g_dryingDeadlineMs = 0;
        HAL_LOG_INFO("CMD",
                     "drying applied: unit=%s temp=%dC duration=infinite (%s)",
                     unitId == 0xFF ? "ALL" : "Ux", tempC,
                     fromParams ? "params" : "fallback");
    }
}

// `commands/stop` — выключить нагрев и сбросить deadline.
void applyStop(JsonObjectConst data) {
    if (!g_output) return;

    const uint8_t unitId = data["unitId"].is<const char*>()
                         ? parseUnitId(data["unitId"].as<const char*>())
                         : 0xFF;

    ControllerOutputCommand cmd{};
    cmd.mode        = ControllerOutputMode::Off;
    cmd.targetTempC = 0.0f;
    g_output->apply(cmd);
    g_dryingDeadlineMs = 0;

    HAL_LOG_INFO("CMD", "stop applied: unit=%s", unitId == 0xFF ? "ALL" : "Ux");
}

} // namespace

void wireCommandRouting(MenuBridge* menu,
                        idryer::cloud::LinkIntegrationsManager* integrations,
                        RmtOutputAdapter* output) {
    g_menu         = menu;
    g_integrations = integrations;
    g_output       = output;
}

void handleCommand(const char* command, JsonObjectConst data) {
    if (!command) return;

    // Time-sync делается выше — в IdryerRuntime::onMqttCommand, до роутинга.
    // Здесь парсить timestamp нельзя: handleCommand зарегистрирован и как
    // runtime.setCommandHandler (MQTT path), и как local.setCommandSink
    // (local WS path). Local-WS клиенты НЕ должны менять системное время.

    HAL_LOG_INFO("CMD", "MQTT command: %s", command);

    // 2. Меню: get_config / set перехватываются ДО ActionDispatcher.
    //    (legacy HeaterDevice::handleMqttCommand делал то же самое.)
    if (strcmp(command, "get_config") == 0) {
        if (g_menu) g_menu->publishFullConfig();
        return;
    }
    if (strcmp(command, "set") == 0) {
        if (g_menu) g_menu->applySetCommand(data);
        return;
    }

    // 3. Heater control — drying / stop на RMT-выход.
    if (strcmp(command, "drying") == 0) { applyDrying(data); return; }
    if (strcmp(command, "stop")   == 0) { applyStop(data);   return; }

    // 4. Интеграции — link_integration / bambu_apply через LinkIntegrationsManager.
    if (strcmp(command, "link_integration") == 0) {
        if (g_integrations) {
            g_integrations->handleLinkIntegrationCommand(data);
        } else {
            HAL_LOG_WARN("CMD", "link_integration: manager not registered");
            return;
        }

        // Legacy-поведение: если enabled:true — синхронизируем toggle меню.
        // Делается ПОСЛЕ handleLinkIntegrationCommand, чтобы новый host/port
        // уже сохранился до setActive (через эксклюзивность в MenuBridge).
        const char* type    = data["type"]    | (const char*)nullptr;
        const bool  enabled = data["enabled"] | false;
        if (type && enabled && g_menu) {
            const char* bind = nullptr;
            if      (strcmp(type, "moonraker") == 0) bind = "moon_en";
            else if (strcmp(type, "bambu")     == 0) bind = "bambu_en";
            else if (strcmp(type, "ha")        == 0) bind = "ha_en";

            if (bind) {
                StaticJsonDocument<32> doc;
                doc["bind"] = bind;
                doc["val"]  = true;
                g_menu->applySetCommand(doc.as<JsonObjectConst>());
                HAL_LOG_INFO("CMD", "link_integration: switched active to %s", type);
            }
        }
        return;
    }

    if (strcmp(command, "bambu_apply") == 0) {
        if (g_integrations) g_integrations->handleBambuApplyCommand(data);
        else                HAL_LOG_WARN("CMD", "bambu_apply: manager not registered");
        return;
    }

    // 5. Команды, которые на iHeater Link не поддерживаются (нет UART-MCU).
    //    Принимаем, логируем, не пытаемся "улучшить" поведение.
    if (strcmp(command, "storage")      == 0 ||
        strcmp(command, "profile")      == 0 ||
        strcmp(command, "find")         == 0 ||
        strcmp(command, "read_rfid")    == 0 ||
        strcmp(command, "write_rfid")   == 0 ||
        strcmp(command, "clear_errors") == 0 ||
        strcmp(command, "invoke")       == 0)
    {
        HAL_LOG_INFO("CMD", "%s: unsupported on iHeater Link, ignored", command);
        return;
    }

    HAL_LOG_WARN("CMD", "Unknown command: %s", command);
}

void commandRoutingTick() {
    if (g_dryingDeadlineMs == 0 || g_output == nullptr) return;

    // Сравнение со знаком — корректно при wrap'е millis() (49.7 дней).
    if ((int32_t)(millis() - g_dryingDeadlineMs) < 0) return;

    ControllerOutputCommand cmd{};
    cmd.mode        = ControllerOutputMode::Off;
    cmd.targetTempC = 0.0f;
    g_output->apply(cmd);
    g_dryingDeadlineMs = 0;

    HAL_LOG_INFO("CMD", "drying duration expired: output = Off");
}

} // namespace iheaterlink

#endif // ESP32 || ESP_PLATFORM
