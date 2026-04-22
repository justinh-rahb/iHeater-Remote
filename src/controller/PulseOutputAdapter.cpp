#include "controller/PulseOutputAdapter.h"

namespace iheaterlink {

void PulseOutputAdapter::begin()
{
    lastPulseCode_ = encode(lastCommand_);

    if (!isEnabled()) {
        return;
    }

    pinMode(config_.outputPin, OUTPUT);
    writeOutput(false);
    phaseStartedUs_ = micros();
}

void PulseOutputAdapter::loop()
{
    if (!isEnabled()) {
        return;
    }

    const uint32_t now = micros();
    const uint32_t elapsedUs = now - phaseStartedUs_;

    switch (phase_) {
    case PulsePhase::Idle:
        if (elapsedUs >= config_.framePeriodUs) {
            writeOutput(false);
            pulsesRemaining_ = lastPulseCode_;
            phase_ = PulsePhase::SyncLow;
            phaseStartedUs_ = now;
        }
        break;

    case PulsePhase::SyncLow:
        if (elapsedUs >= config_.syncLowUs) {
            writeOutput(true);
            phase_ = PulsePhase::PulseHigh;
            phaseStartedUs_ = now;
        }
        break;

    case PulsePhase::PulseHigh:
        if (elapsedUs >= config_.pulseHighUs) {
            writeOutput(false);
            phase_ = PulsePhase::PulseLow;
            phaseStartedUs_ = now;
        }
        break;

    case PulsePhase::PulseLow:
        if (elapsedUs >= config_.pulseLowUs) {
            if (pulsesRemaining_ > 0) {
                --pulsesRemaining_;
            }

            if (pulsesRemaining_ == 0) {
                phase_ = PulsePhase::Idle;
                phaseStartedUs_ = now;
            } else {
                writeOutput(true);
                phase_ = PulsePhase::PulseHigh;
                phaseStartedUs_ = now;
            }
        }
        break;
    }
}

void PulseOutputAdapter::apply(const ControllerOutputCommand& command)
{
    lastCommand_ = command;
    lastPulseCode_ = encode(command);
}

ControllerOutputCommand PulseOutputAdapter::getLastCommand() const
{
    return lastCommand_;
}

uint8_t PulseOutputAdapter::encode(const ControllerOutputCommand& command) const
{
    if (command.mode == ControllerOutputMode::Off) {
        return config_.offCode;
    }

    int target = static_cast<int>(lroundf(command.targetTempC));
    if (target < config_.minTempC) {
        target = config_.minTempC;
    }
    if (target > config_.maxTempC) {
        target = config_.maxTempC;
    }

    const int base = config_.minTempC;
    const int step = config_.stepTempC > 0 ? config_.stepTempC : 5;
    const int quantized = base + (((target - base) + (step / 2)) / step) * step;
    return static_cast<uint8_t>(quantized);
}

void PulseOutputAdapter::writeOutput(bool high)
{
    digitalWrite(config_.outputPin, high ? HIGH : LOW);
}

} // namespace iheaterlink
