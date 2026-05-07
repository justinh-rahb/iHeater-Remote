#pragma once

// Колбэки от LinkIntegrationsManager (Moonraker VIRTUAL_CHAMBER, Bambu Reader)
// → применение target температуры на RmtOutputAdapter.
//
// Поведение 1-в-1 с legacy HeaterDevice::onVirtualChamberUpdate /
// onBambuPrinterStatusUpdate:
//   - moon_en/bambu_en из меню — gate автонагрева;
//   - VirtualChamber: target>0 + available + autoHeat → ON, иначе OFF;
//   - Bambu: chamberTarget > 0 (printer) → ON; иначе trayType → menu.mat_*; иначе OFF.

namespace idryer { namespace cloud {
struct VirtualChamberData;
struct BambuPrinterStatus;
}}

namespace iheaterlink {

class RmtOutputAdapter;

/// Привязать модуль авто-нагрева к выходному адаптеру. Один раз при старте.
void wireAutoHeat(RmtOutputAdapter* output);

/// Колбэк, регистрируемый в LinkIntegrationsManager::setVirtualChamberCallback.
void onVirtualChamberUpdate(const idryer::cloud::VirtualChamberData& data);

/// Колбэк, регистрируемый в LinkIntegrationsManager::setBambuPrinterStatusCallback.
void onBambuPrinterStatusUpdate(const idryer::cloud::BambuPrinterStatus& status);

} // namespace iheaterlink
