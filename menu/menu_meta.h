// Auto-generated for ESP32 LINK. Do not edit.
// Contains menu metadata only (no pointers to data or callbacks).
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MENU_META_COUNT 17
#define MENU_LANG_COUNT 2

typedef enum {
    META_SUBMENU = 0,
    META_ACTION = 1,
    META_VALUE = 2,
    META_TOGGLE = 3
} MenuMetaType;

typedef enum {
    META_VT_F32 = 0,
    META_VT_U16 = 1,
    META_VT_U8 = 2,
    META_VT_I32 = 3,
    META_VT_BOOL = 4,
    META_VT_U32 = 5
} MenuMetaValueType;

typedef enum {
    META_SCOPE_GLOBAL = 0,
    META_SCOPE_PER_UNIT = 1
} MenuMetaScope;

typedef struct {
    uint16_t id;
    const char* title[MENU_LANG_COUNT];
    const char* unit[MENU_LANG_COUNT];
    MenuMetaType type;
    int16_t parent;
    int16_t first_child;
    uint16_t child_count;
    // Value/Toggle fields (ignored for submenu/action)
    MenuMetaValueType vtype;
    float min_val;
    float max_val;
    float step;
    MenuMetaScope scope;
} MenuMeta;

static const MenuMeta g_menu_meta[MENU_META_COUNT] = {
    // [0] root
    { 0, { "IHEATER LINK", "IHEATER LINK" }, { nullptr, nullptr },
      META_SUBMENU, -1, 1, 5,
      META_VT_F32, 0.0f, 0.0f, 0.0f, META_SCOPE_PER_UNIT },
    // [1] portal
    { 1, { "ПОРТАЛ", "PORTAL" }, { nullptr, nullptr },
      META_SUBMENU, 0, 2, 1,
      META_VT_F32, 0.0f, 0.0f, 0.0f, META_SCOPE_PER_UNIT },
    // [2] start_claim
    { 2, { "СВЯЗАТЬ", "CLAIM" }, { nullptr, nullptr },
      META_ACTION, 1, -1, 0,
      META_VT_F32, 0.0f, 0.0f, 0.0f, META_SCOPE_PER_UNIT },
    // [3] bambu
    { 3, { "BAMBU", "BAMBU" }, { nullptr, nullptr },
      META_SUBMENU, 0, 4, 2,
      META_VT_F32, 0.0f, 0.0f, 0.0f, META_SCOPE_PER_UNIT },
    // [4] iheater_link_enabled
    { 4, { "АВТО НАГРЕВ", "AUTO HEAT" }, { nullptr, nullptr },
      META_TOGGLE, 3, -1, 0,
      META_VT_BOOL, 0.0f, 0.0f, 1.0f, META_SCOPE_GLOBAL },
    // [5] iheater_link_materials
    { 5, { "МАТЕРИАЛЫ", "MATERIALS" }, { nullptr, nullptr },
      META_SUBMENU, 3, 6, 7,
      META_VT_F32, 0.0f, 0.0f, 0.0f, META_SCOPE_PER_UNIT },
    // [6] iheater_link_pla_temp
    { 6, { "PLA", "PLA" }, { "°C", "°C" },
      META_VALUE, 5, -1, 0,
      META_VT_F32, 30.0f, 90.0f, 1.0f, META_SCOPE_GLOBAL },
    // [7] iheater_link_petg_temp
    { 7, { "PETG", "PETG" }, { "°C", "°C" },
      META_VALUE, 5, -1, 0,
      META_VT_F32, 30.0f, 90.0f, 1.0f, META_SCOPE_GLOBAL },
    // [8] iheater_link_abs_temp
    { 8, { "ABS", "ABS" }, { "°C", "°C" },
      META_VALUE, 5, -1, 0,
      META_VT_F32, 30.0f, 90.0f, 1.0f, META_SCOPE_GLOBAL },
    // [9] iheater_link_asa_temp
    { 9, { "ASA", "ASA" }, { "°C", "°C" },
      META_VALUE, 5, -1, 0,
      META_VT_F32, 30.0f, 90.0f, 1.0f, META_SCOPE_GLOBAL },
    // [10] iheater_link_pc_temp
    { 10, { "PC", "PC" }, { "°C", "°C" },
      META_VALUE, 5, -1, 0,
      META_VT_F32, 30.0f, 90.0f, 1.0f, META_SCOPE_GLOBAL },
    // [11] iheater_link_pa_temp
    { 11, { "PA / NYLON", "PA / NYLON" }, { "°C", "°C" },
      META_VALUE, 5, -1, 0,
      META_VT_F32, 30.0f, 90.0f, 1.0f, META_SCOPE_GLOBAL },
    // [12] iheater_link_unknown_temp
    { 12, { "ДРУГОЙ МАТЕРИАЛ", "OTHER MATERIAL" }, { "°C", "°C" },
      META_VALUE, 5, -1, 0,
      META_VT_F32, 30.0f, 90.0f, 1.0f, META_SCOPE_GLOBAL },
    // [13] ha
    { 13, { "HOME ASSISTANT", "HOME ASSISTANT" }, { nullptr, nullptr },
      META_SUBMENU, 0, 14, 1,
      META_VT_F32, 0.0f, 0.0f, 0.0f, META_SCOPE_PER_UNIT },
    // [14] ha_enabled
    { 14, { "ВКЛЮЧЕНО", "ENABLED" }, { nullptr, nullptr },
      META_TOGGLE, 13, -1, 0,
      META_VT_BOOL, 0.0f, 0.0f, 1.0f, META_SCOPE_GLOBAL },
    // [15] units_count
    { 15, { "КОЛ-ВО ЮНИТОВ", "UNITS" }, { nullptr, nullptr },
      META_VALUE, 0, -1, 0,
      META_VT_U8, 1.0f, 1.0f, 1.0f, META_SCOPE_GLOBAL },
    // [16] language
    { 16, { "ЯЗЫК", "LANGUAGE" }, { nullptr, nullptr },
      META_VALUE, 0, -1, 0,
      META_VT_U8, 0.0f, 1.0f, 1.0f, META_SCOPE_GLOBAL },
};

// Get menu item by id
static inline const MenuMeta* menu_meta_get(uint16_t id) {
    if (id < MENU_META_COUNT) return &g_menu_meta[id];
    return nullptr;
}
