#pragma once

#include <Arduino.h>
#include <driver/rmt.h>

#include "IControllerOutputAdapter.h"

namespace iheaterlink
{

#ifndef IHEATER_TRIGGER_OUTPUT_PIN
#define IHEATER_TRIGGER_OUTPUT_PIN 0xFF
#endif

    struct RmtOutputConfig
    {
        uint8_t outputPin = IHEATER_TRIGGER_OUTPUT_PIN;
        rmt_channel_t channel = RMT_CHANNEL_0;
        uint8_t memBlockNum = 2;
        uint8_t offCode = 10;
        uint8_t minTempC = 45;
        uint8_t maxTempC = 80;
        uint8_t stepTempC = 1;
        uint32_t syncLowUs = 120000;
        uint32_t pulseHighUs = 2000;
        uint32_t pulseLowUs = 2000;
        uint32_t framePeriodMs = 1000;
    };

    class RmtOutputAdapter : public IControllerOutputAdapter
    {
    public:
        explicit RmtOutputAdapter(const RmtOutputConfig &config)
            : config_(config) {}

        void begin() override;
        void loop() override;
        void apply(const ControllerOutputCommand &command) override;
        ControllerOutputCommand getLastCommand() const override;

        uint8_t getLastPulseCode() const { return lastPulseCode_; }
        bool isEnabled() const { return config_.outputPin != 0xFF && initialized_; }
        void forceFrame();
        const RmtOutputConfig& getConfig() const { return config_; }

    private:
        static constexpr size_t MAX_ITEMS = 128;

        uint8_t encode(const ControllerOutputCommand &command) const;
        void rebuildFrame(uint8_t pulseCode);
        void sendFrame();

        RmtOutputConfig config_;
        ControllerOutputCommand lastCommand_{};
        uint8_t lastPulseCode_ = 10;
        uint8_t builtForCode_ = 0xFF;
        bool initialized_ = false;

        rmt_item32_t items_[MAX_ITEMS]{};
        size_t itemCount_ = 0;

        uint32_t lastFrameMs_ = 0;
    };

} // namespace iheaterlink
