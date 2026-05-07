#if defined(ESP32) || defined(ESP_PLATFORM)

#include "heater/auto_heat.h"
#include "heater/material_temp.h"
#include "controller/RmtOutputAdapter.h"
#include <string.h>

#include <hal/hal_types.h>
#include <integrations/common/link_integrations_types.h>
#include <integrations/moonraker/moonraker_client.h>   // VirtualChamberData
#include <integrations/bambu/bambu_client.h>           // BambuPrinterStatus

#include <menu_state.h>

namespace iheaterlink {

namespace {
RmtOutputAdapter* g_output = nullptr;
}

void wireAutoHeat(RmtOutputAdapter* output) {
    g_output = output;
}

// VirtualChamber из Moonraker. Активен только при moon_en=true в меню.
// Поведение: gate (autoHeat) → available → target>0 → ON, иначе OFF.
void onVirtualChamberUpdate(const idryer::cloud::VirtualChamberData& data) {
    if (!g_output) return;

    const bool autoHeat = menu.moon_en;

    ControllerOutputCommand cmd{};
    if (!autoHeat || !data.available || data.target <= 0.0f) {
        cmd.mode = ControllerOutputMode::Off;
        cmd.targetTempC = 0.0f;
    } else {
        cmd.mode = ControllerOutputMode::TargetTemperature;
        cmd.targetTempC = data.target;
    }
    g_output->apply(cmd);

    HAL_LOG_INFO("HEATER",
                 "VIRTUAL_CHAMBER: autoHeat=%d available=%d target=%.1f temp=%.1f hasSensor=%d → target=%.1f°C",
                 autoHeat ? 1 : 0,
                 data.available ? 1 : 0,
                 data.target,
                 data.temperature,
                 data.hasSensor ? 1 : 0,
                 cmd.targetTempC);
}

// Bambu Reader. Активен только при bambu_en=true в меню.
// Приоритеты:
//   1. autoHeat=false → OFF.
//   2. gcode_state не PREPARE/RUNNING → OFF (печать не идёт).
//   3. printer.chamberTarget > 0 → setpoint от принтера (X1C с датчиком).
//   4. trayType из AMS → materialTempFromMenu (menu.mat_*).
//   5. Иначе OFF.
//
// Heat-allowed states (как присылает Bambu, заглавными):
//   RUNNING — печать идёт.
//   PREPARE — подготовка (прогрев / homing).
// Все остальные (IDLE, FINISH, FAILED, PAUSE, INIT, OFFLINE, SLICING,
// UNKNOWN, пустая строка) → OFF.
static bool bambuShouldHeat(const char* gcodeState) {
    if (!gcodeState || gcodeState[0] == '\0') return false;
    return strcmp(gcodeState, "RUNNING") == 0
        || strcmp(gcodeState, "PREPARE") == 0;
}

void onBambuPrinterStatusUpdate(const idryer::cloud::BambuPrinterStatus& status) {
    if (!g_output) return;

    const bool autoHeat        = menu.bambu_en;
    const bool stateAllowsHeat = bambuShouldHeat(status.gcodeState);

    ControllerOutputCommand cmd{};
    float menuTemp = 0.0f;
    const char* source = "off";

    if (!autoHeat) {
        cmd.mode = ControllerOutputMode::Off;
        cmd.targetTempC = 0.0f;
        source = "autoHeat=off";
    } else if (!stateAllowsHeat) {
        cmd.mode = ControllerOutputMode::Off;
        cmd.targetTempC = 0.0f;
        source = "state-not-printing";
    } else if (status.chamberTarget > 0.0f) {
        cmd.mode = ControllerOutputMode::TargetTemperature;
        cmd.targetTempC = status.chamberTarget;
        source = "printer";
    } else if (status.trayType[0] != '\0') {
        menuTemp = materialTempFromMenu(status.trayType);
        if (menuTemp > 0.0f) {
            cmd.mode = ControllerOutputMode::TargetTemperature;
            cmd.targetTempC = menuTemp;
            source = "menu";
        } else {
            cmd.mode = ControllerOutputMode::Off;
            cmd.targetTempC = 0.0f;
            source = "menu=0";
        }
    } else {
        cmd.mode = ControllerOutputMode::Off;
        cmd.targetTempC = 0.0f;
        source = "no-target";
    }

    g_output->apply(cmd);

    HAL_LOG_INFO("HEATER",
                 "BAMBU status: autoHeat=%d state=%s chamberTarget=%.1f chamberTemp=%.1f tray=%s menu=%.1f → target=%.1f°C (src=%s)",
                 autoHeat ? 1 : 0,
                 status.gcodeState,
                 status.chamberTarget,
                 status.chamberTemp,
                 status.trayType,
                 menuTemp,
                 cmd.targetTempC,
                 source);
}

} // namespace iheaterlink

#endif // ESP32 || ESP_PLATFORM
