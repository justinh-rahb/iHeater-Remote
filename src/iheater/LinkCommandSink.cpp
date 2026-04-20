#include "iheater/LinkCommandSink.h"

#include <hal/hal_types.h>

namespace iheaterlink {

void LinkCommandSink::sendCommand(const DryerUart::CommandPayload& payload, bool /*ackRequired*/)
{
    using CC = DryerUart::CommandCode;

    if (!output_) {
        HAL_LOG_WARN("LINKSINK", "No controller output, cmd=0x%02X ignored",
                     static_cast<unsigned>(payload.command));
        return;
    }

    switch (payload.command) {
    case CC::Start: {
        const float targetTempC = static_cast<float>(payload.arg0) / 10.0f;

        ControllerOutputCommand cmd{};
        cmd.mode = ControllerOutputMode::TargetTemperature;
        cmd.targetTempC = targetTempC;
        output_->apply(cmd);

        HAL_LOG_INFO("LINKSINK", "Start applied: target=%.1fC", targetTempC);
        break;
    }

    case CC::Stop: {
        ControllerOutputCommand cmd{};
        cmd.mode = ControllerOutputMode::Off;
        cmd.targetTempC = 0.0f;
        output_->apply(cmd);

        HAL_LOG_INFO("LINKSINK", "Stop applied: output = Off");
        break;
    }

    case CC::Find:
        HAL_LOG_INFO("LINKSINK", "Find: not implemented (no LED on LinkII)");
        break;

    case CC::GetConfig:
    case CC::SetConfig:
        HAL_LOG_INFO("LINKSINK", "Config cmd 0x%02X: not implemented on LinkII yet",
                     static_cast<unsigned>(payload.command));
        break;

    case CC::ReadRfid:
    case CC::WriteRfid:
        HAL_LOG_DEBUG("LINKSINK", "RFID cmd: not applicable on LinkII");
        break;

    default:
        HAL_LOG_DEBUG("LINKSINK", "Cmd 0x%02X: not applicable on LinkII",
                      static_cast<unsigned>(payload.command));
        break;
    }
}

void LinkCommandSink::sendProfileCommand(const DryerUart::ProfilePayload& /*payload*/,
                                         bool /*ackRequired*/)
{
    HAL_LOG_DEBUG("LINKSINK", "Profile cmd: not applicable on LinkII");
}

void LinkCommandSink::sendConfigPushChunk(const DryerUart::ConfigChunkPayload& /*payload*/,
                                          uint8_t /*dataLen*/,
                                          uint8_t /*flags*/)
{
    HAL_LOG_DEBUG("LINKSINK", "Config push chunk: not applicable on LinkII");
}

} // namespace iheaterlink
