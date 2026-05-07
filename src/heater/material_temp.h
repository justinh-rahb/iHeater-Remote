#pragma once

namespace iheaterlink {

/// Сопоставить название материала из принтера (Bambu/AMS) с температурой
/// из пользовательского меню iHeater Link.
///
/// `trayType` — строка вида "PLA", "PETG", "ABS", "ASA", "PC", "PA-CF", "PAHT",
/// "Nylon" и т.п. Поведение 1-в-1 как в legacy HeaterDevice.cpp:
///   case-insensitive prefix match → menu.mat_*.
///   Неизвестный материал → 0.0f (греть «вслепую» небезопасно).
float materialTempFromMenu(const char* trayType);

} // namespace iheaterlink
