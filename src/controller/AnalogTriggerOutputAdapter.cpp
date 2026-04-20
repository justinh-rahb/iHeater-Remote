#include "controller/AnalogTriggerOutputAdapter.h"

namespace iheaterlink {

void AnalogTriggerOutputAdapter::begin()
{
    lastEncodedValue_ = encode(lastCommand_);
}

void AnalogTriggerOutputAdapter::loop()
{
    // Здесь появится реальный вывод на GPIO/PWM + RC-фильтр.
    // На текущем этапе адаптер фиксирует команду и кодированное значение.
}

void AnalogTriggerOutputAdapter::apply(const ControllerOutputCommand& command)
{
    lastCommand_ = command;
    lastEncodedValue_ = encode(command);
}

ControllerOutputCommand AnalogTriggerOutputAdapter::getLastCommand() const
{
    return lastCommand_;
}

float AnalogTriggerOutputAdapter::encode(const ControllerOutputCommand& command) const
{
    if (command.mode == ControllerOutputMode::Off) {
        return config_.offCode;
    }

    float value = command.targetTempC;
    if (value < config_.minTempC) value = config_.minTempC;
    if (value > config_.maxTempC) value = config_.maxTempC;
    return value;
}

} // namespace iheaterlink
