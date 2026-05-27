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

/// Колбэк смены сессии от auto-heat (Bambu или Moonraker): вызывается ТОЛЬКО
/// при изменении целевой температуры или флага heating (Drying/Idle).
/// Используется в main.cpp для синхронизации device().status.mode[]
/// (открытие/закрытие сессии сушки → portal status, sessionNum,
/// push-уведомления, история).
using SessionCallback = void(*)(float targetTempC, bool heating);
void wireBambuSession(SessionCallback cb);
void wireMoonrakerSession(SessionCallback cb);

/// Включить/выключить логирование решений о нагреве (теги HEATER).
void setLogDecisions(bool enabled);

/// Колбэк, регистрируемый в LinkIntegrationsManager::setVirtualChamberCallback.
void onVirtualChamberUpdate(void* ctx, const idryer::cloud::VirtualChamberData& data);

/// Колбэк, регистрируемый в LinkIntegrationsManager::setBambuPrinterStatusCallback.
void onBambuPrinterStatusUpdate(void* ctx, const idryer::cloud::BambuPrinterStatus& status);

} // namespace iheaterlink
