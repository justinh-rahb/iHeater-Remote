#pragma once

#include <cloud/command_sink.h>

#include "controller/IControllerOutputAdapter.h"

namespace iheaterlink {

/**
 * @brief Применяет MQTT-команды портала локально на выходной адаптер нагревателя.
 *
 * Start (+ arg0 = temperature × 10) → ControllerOutputCommand{TargetTemperature, X}
 * Stop                              → ControllerOutputCommand{Off}
 *
 * Приоритет между источниками (портал vs Moonraker VIRTUAL_CHAMBER vs Bambu Reader)
 * разруливается принципом «последний apply() побеждает». LinkIntegrationsManager
 * в своих колбэках вызовет apply() с актуальным target из принтера,
 * и это перетрёт ручную команду с портала.
 *
 * Остальные команды (profile, config push, rfid) для iHeater не применимы
 * и молча игнорируются.
 */
class LinkCommandSink : public idryer::cloud::ICommandSink {
public:
    explicit LinkCommandSink(IControllerOutputAdapter* output)
        : output_(output) {}

    void sendCommand(const DryerUart::CommandPayload& payload, bool ackRequired) override;
    void sendProfileCommand(const DryerUart::ProfilePayload& payload, bool ackRequired) override;
    void sendConfigPushChunk(const DryerUart::ConfigChunkPayload& payload,
                             uint8_t dataLen,
                             uint8_t flags) override;

private:
    IControllerOutputAdapter* output_;
};

} // namespace iheaterlink
