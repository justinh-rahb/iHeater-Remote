#pragma once

#include "ControllerOutputCommand.h"

namespace iheaterlink {

class IControllerOutputAdapter {
public:
    virtual ~IControllerOutputAdapter() = default;

    virtual void begin() = 0;
    virtual void loop() = 0;
    virtual void apply(const ControllerOutputCommand& command) = 0;
    virtual ControllerOutputCommand getLastCommand() const = 0;
};

} // namespace iheaterlink
