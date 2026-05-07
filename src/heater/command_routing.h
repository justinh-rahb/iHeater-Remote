#pragma once

#include <ArduinoJson.h>

// Единственная точка входа для команд от MQTT и Local WS.
// Регистрируется через runtime.setCommandHandler(...) и
// local.setCommandSink(...) — один обработчик, два транспорта.
//
// Поведение совместимо с legacy iheaterlink::HeaterDevice::handleMqttCommand:
//   - get_config / set перехватываются ДО ActionDispatcher и идут в MenuBridge;
//   - drying / stop применяются на RmtOutputAdapter (с deadline-таймером);
//   - link_integration / bambu_apply делегируются в LinkIntegrationsManager;
//   - после link_integration с enabled:true — toggle bambu_en/moon_en/ha_en
//     прокидывается в applySetCommand (эксклюзивность + setActive в меню).
//   - неподдерживаемые на iHeater Link команды (storage/profile/find/...)
//     логируются как unsupported и игнорируются.

namespace idryer { namespace cloud {
class LinkIntegrationsManager;
}}

namespace iheaterlink {

class MenuBridge;
class RmtOutputAdapter;

/// Привязать модуль команд к продуктовым зависимостям.
/// Вызвать в setup() ДО runtime.setCommandHandler.
void wireCommandRouting(MenuBridge* menu,
                        idryer::cloud::LinkIntegrationsManager* integrations,
                        RmtOutputAdapter* output);

/// Колбэк для runtime.setCommandHandler / local.setCommandSink.
void handleCommand(const char* command, JsonObjectConst data);

/// Вызывать каждую итерацию loop() — следит за deadline-таймером `drying`.
/// При истечении применяет Off на RmtOutputAdapter.
void commandRoutingTick();

} // namespace iheaterlink
