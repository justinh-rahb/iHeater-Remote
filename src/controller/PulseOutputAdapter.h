#pragma once

#include <Arduino.h>

#include "IControllerOutputAdapter.h"

namespace iheaterlink {

#ifndef IHEATER_TRIGGER_OUTPUT_PIN
#define IHEATER_TRIGGER_OUTPUT_PIN 0xFF
#endif

struct PulseOutputConfig {
    uint8_t outputPin = IHEATER_TRIGGER_OUTPUT_PIN;
    uint8_t offCode = 10;
    uint8_t minTempC = 45;
    uint8_t maxTempC = 80;
    uint8_t stepTempC = 5;
    uint32_t syncLowUs = 120000;
    uint32_t pulseHighUs = 2000;
    uint32_t pulseLowUs = 2000;
    uint32_t framePeriodUs = 1000000;
};

class PulseOutputAdapter : public IControllerOutputAdapter {
public:
    explicit PulseOutputAdapter(const PulseOutputConfig& config)
        : config_(config) {}

    void begin() override;
    void loop() override;
    void apply(const ControllerOutputCommand& command) override;
    ControllerOutputCommand getLastCommand() const override;

    uint8_t getLastPulseCode() const { return lastPulseCode_; }
    bool isEnabled() const { return config_.outputPin != 0xFF; }

private:
    enum class PulsePhase : uint8_t {
        Idle = 0,
        SyncLow = 1,
        PulseHigh = 2,
        PulseLow = 3,
    };

    uint8_t encode(const ControllerOutputCommand& command) const;
    void writeOutput(bool high);

    PulseOutputConfig config_;
    ControllerOutputCommand lastCommand_{};
    uint8_t lastPulseCode_ = 10;
    uint8_t pulsesRemaining_ = 0;
    uint32_t phaseStartedUs_ = 0;
    PulsePhase phase_ = PulsePhase::Idle;
};

} // namespace iheaterlink
