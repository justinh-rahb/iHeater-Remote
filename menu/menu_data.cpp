// Auto-generated. Do not edit.
#include <stddef.h>
#include "menu_ids.h"
#include "menu_types.h"
#include "menu_state.h"

#ifdef __cplusplus
extern "C" {
#endif
void start_claim(void);
#ifdef __cplusplus
}
#endif

extern MenuState menu;

const MenuItem g_menu[MENU__COUNT] = {
  [0] = {
    MENU_ROOT, { "IHEATER LINK", "IHEATER LINK" }, { nullptr, nullptr },
    MN_SUBMENU, -1, 1, 5,
    { { NULL }, { VT_F32, NULL, 0, 0, 0, NULL, false } },
    -1, 0
  },
  [1] = {
    MENU_PORTAL, { "ПОРТАЛ", "PORTAL" }, { nullptr, nullptr },
    MN_SUBMENU, 0, 2, 1,
    { { NULL }, { VT_F32, NULL, 0, 0, 0, NULL, false } },
    -1, 0
  },
  [2] = {
    MENU_START_CLAIM, { "СВЯЗАТЬ", "CLAIM" }, { nullptr, nullptr },
    MN_ACTION, 1, -1, 0,
    { { start_claim }, { VT_F32, NULL, 0, 0, 0, NULL, false } },
    -1, 0
  },
  [3] = {
    MENU_BAMBU, { "BAMBU", "BAMBU" }, { nullptr, nullptr },
    MN_SUBMENU, 0, 4, 2,
    { { NULL }, { VT_F32, NULL, 0, 0, 0, NULL, false } },
    -1, 0
  },
  [4] = {
    MENU_IHEATER_LINK_ENABLED, { "АВТО НАГРЕВ", "AUTO HEAT" }, { nullptr, nullptr },
    MN_TOGGLE, 3, -1, 0,
    { { NULL }, { VT_BOOL, (void*)&menu.iheater_link_enabled, 0, 0, 1, nullptr, false } },
    2220, 1
  },
  [5] = {
    MENU_IHEATER_LINK_MATERIALS, { "МАТЕРИАЛЫ", "MATERIALS" }, { nullptr, nullptr },
    MN_SUBMENU, 3, 6, 7,
    { { NULL }, { VT_F32, NULL, 0, 0, 0, NULL, false } },
    -1, 0
  },
  [6] = {
    MENU_IHEATER_LINK_PLA_TEMP, { "PLA", "PLA" }, { "°C", "°C" },
    MN_VALUE, 5, -1, 0,
    { { NULL }, { VT_F32, (void*)&menu.iheater_link_pla_temp, 30, 90, 1, nullptr, false } },
    2221, 4
  },
  [7] = {
    MENU_IHEATER_LINK_PETG_TEMP, { "PETG", "PETG" }, { "°C", "°C" },
    MN_VALUE, 5, -1, 0,
    { { NULL }, { VT_F32, (void*)&menu.iheater_link_petg_temp, 30, 90, 1, nullptr, false } },
    2225, 4
  },
  [8] = {
    MENU_IHEATER_LINK_ABS_TEMP, { "ABS", "ABS" }, { "°C", "°C" },
    MN_VALUE, 5, -1, 0,
    { { NULL }, { VT_F32, (void*)&menu.iheater_link_abs_temp, 30, 90, 1, nullptr, false } },
    2229, 4
  },
  [9] = {
    MENU_IHEATER_LINK_ASA_TEMP, { "ASA", "ASA" }, { "°C", "°C" },
    MN_VALUE, 5, -1, 0,
    { { NULL }, { VT_F32, (void*)&menu.iheater_link_asa_temp, 30, 90, 1, nullptr, false } },
    2233, 4
  },
  [10] = {
    MENU_IHEATER_LINK_PC_TEMP, { "PC", "PC" }, { "°C", "°C" },
    MN_VALUE, 5, -1, 0,
    { { NULL }, { VT_F32, (void*)&menu.iheater_link_pc_temp, 30, 90, 1, nullptr, false } },
    2237, 4
  },
  [11] = {
    MENU_IHEATER_LINK_PA_TEMP, { "PA / NYLON", "PA / NYLON" }, { "°C", "°C" },
    MN_VALUE, 5, -1, 0,
    { { NULL }, { VT_F32, (void*)&menu.iheater_link_pa_temp, 30, 90, 1, nullptr, false } },
    2241, 4
  },
  [12] = {
    MENU_IHEATER_LINK_UNKNOWN_TEMP, { "ДРУГОЙ МАТЕРИАЛ", "OTHER MATERIAL" }, { "°C", "°C" },
    MN_VALUE, 5, -1, 0,
    { { NULL }, { VT_F32, (void*)&menu.iheater_link_unknown_temp, 30, 90, 1, nullptr, false } },
    2245, 4
  },
  [13] = {
    MENU_HA, { "HOME ASSISTANT", "HOME ASSISTANT" }, { nullptr, nullptr },
    MN_SUBMENU, 0, 14, 1,
    { { NULL }, { VT_F32, NULL, 0, 0, 0, NULL, false } },
    -1, 0
  },
  [14] = {
    MENU_HA_ENABLED, { "ВКЛЮЧЕНО", "ENABLED" }, { nullptr, nullptr },
    MN_TOGGLE, 13, -1, 0,
    { { NULL }, { VT_BOOL, (void*)&menu.ha_enabled, 0, 0, 1, nullptr, false } },
    2249, 1
  },
  [15] = {
    MENU_UNITS_COUNT, { "КОЛ-ВО ЮНИТОВ", "UNITS" }, { nullptr, nullptr },
    MN_VALUE, 0, -1, 0,
    { { NULL }, { VT_U8, (void*)&menu.units_count, 1, 1, 1, nullptr, false } },
    2250, 1
  },
  [16] = {
    MENU_LANGUAGE, { "ЯЗЫК", "LANGUAGE" }, { nullptr, nullptr },
    MN_VALUE, 0, -1, 0,
    { { NULL }, { VT_U8, (void*)&menu.language, 0, 1, 1, nullptr, false } },
    2251, 1
  },
};
