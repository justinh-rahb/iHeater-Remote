#pragma once

#include "IControllerOutputAdapter.h"

namespace iheaterlink {

struct AnalogTriggerOutputConfig {
    uint8_t outputPin = 0xFF;
    float offCode = 15.0f;
    float minTempC = 30.0f;
    float maxTempC = 85.0f;
};

class AnalogTriggerOutputAdapter : public IControllerOutputAdapter {
public:
    explicit AnalogTriggerOutputAdapter(const AnalogTriggerOutputConfig& config)
        : config_(config) {}

    void begin() override;
    void loop() override;
    void apply(const ControllerOutputCommand& command) override;
    ControllerOutputCommand getLastCommand() const override;

    float getLastEncodedValue() const { return lastEncodedValue_; }

private:
    float encode(const ControllerOutputCommand& command) const;

    AnalogTriggerOutputConfig config_;
    ControllerOutputCommand lastCommand_{};
    float lastEncodedValue_ = 0.0f;
};

} // namespace iheaterlink
