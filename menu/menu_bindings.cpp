// AUTO-GENERATED. DO NOT EDIT.
#include <string.h>
#include "menu_bindings.h"
#include "menu_eeprom_io.h"

#ifdef __cplusplus
extern "C" {
#endif

extern MenuState menu;
const MenuBinding g_bindings[] = {
  {MENU_IHEATER_LINK_ENABLED, "iheater_link_enabled", VT_BOOL, (void*)&menu.iheater_link_enabled, true, nullptr, false, SCOPE_GLOBAL, 2220, 0},
  {MENU_IHEATER_LINK_PLA_TEMP, "iheater_link_pla_temp", VT_F32, (void*)&menu.iheater_link_pla_temp, true, nullptr, false, SCOPE_GLOBAL, 2221, 0},
  {MENU_IHEATER_LINK_PETG_TEMP, "iheater_link_petg_temp", VT_F32, (void*)&menu.iheater_link_petg_temp, true, nullptr, false, SCOPE_GLOBAL, 2225, 0},
  {MENU_IHEATER_LINK_ABS_TEMP, "iheater_link_abs_temp", VT_F32, (void*)&menu.iheater_link_abs_temp, true, nullptr, false, SCOPE_GLOBAL, 2229, 0},
  {MENU_IHEATER_LINK_ASA_TEMP, "iheater_link_asa_temp", VT_F32, (void*)&menu.iheater_link_asa_temp, true, nullptr, false, SCOPE_GLOBAL, 2233, 0},
  {MENU_IHEATER_LINK_PC_TEMP, "iheater_link_pc_temp", VT_F32, (void*)&menu.iheater_link_pc_temp, true, nullptr, false, SCOPE_GLOBAL, 2237, 0},
  {MENU_IHEATER_LINK_PA_TEMP, "iheater_link_pa_temp", VT_F32, (void*)&menu.iheater_link_pa_temp, true, nullptr, false, SCOPE_GLOBAL, 2241, 0},
  {MENU_IHEATER_LINK_UNKNOWN_TEMP, "iheater_link_unknown_temp", VT_F32, (void*)&menu.iheater_link_unknown_temp, true, nullptr, false, SCOPE_GLOBAL, 2245, 0},
  {MENU_HA_ENABLED, "ha_enabled", VT_BOOL, (void*)&menu.ha_enabled, true, nullptr, false, SCOPE_GLOBAL, 2249, 0},
  {MENU_UNITS_COUNT, "units_count", VT_U8, (void*)&menu.units_count, true, nullptr, false, SCOPE_GLOBAL, 2250, 0},
  {MENU_LANGUAGE, "language", VT_U8, (void*)&menu.language, true, nullptr, false, SCOPE_GLOBAL, 2251, 0},
};
const uint16_t g_bindings_count = sizeof(g_bindings)/sizeof(g_bindings[0]);

// Global config change hook
ConfigChangeHookFn g_config_change_hook = nullptr;

void menu_set_config_change_hook(ConfigChangeHookFn hook) {
  g_config_change_hook = hook;
}

const MenuBinding* menu_find_bind(const char* bind) {
  if (!bind) return nullptr;
  for (uint16_t i=0;i<g_bindings_count;i++)
    if (strcmp(g_bindings[i].bind, bind) == 0) return &g_bindings[i];
  return nullptr;
}

static inline void store_value(const MenuBinding& b, float v, uint8_t idx) {
  switch (b.vtype) {
    case VT_F32:  if (b.scope==SCOPE_GLOBAL) *(float*)b.ptr    = v; else ((float*)b.ptr)[idx]    = v; break;
    case VT_U16:  if (b.scope==SCOPE_GLOBAL) *(uint16_t*)b.ptr = (uint16_t)v; else ((uint16_t*)b.ptr)[idx] = (uint16_t)v; break;
    case VT_U8:   if (b.scope==SCOPE_GLOBAL) *(uint8_t*)b.ptr  = (uint8_t)v;  else ((uint8_t*)b.ptr)[idx]  = (uint8_t)v;  break;
    case VT_I32:  if (b.scope==SCOPE_GLOBAL) *(int32_t*)b.ptr  = (int32_t)v;  else ((int32_t*)b.ptr)[idx]  = (int32_t)v;  break;
    case VT_BOOL: if (b.scope==SCOPE_GLOBAL) *(bool*)b.ptr     = (bool)(v!=0.0f); else ((bool*)b.ptr)[idx]     = (bool)(v!=0.0f); break;
    case VT_U32:  if (b.scope==SCOPE_GLOBAL) *(uint32_t*)b.ptr = (uint32_t)v; else ((uint32_t*)b.ptr)[idx] = (uint32_t)v; break;
  }
}

static inline void read_value(const MenuBinding& b, void* out_value, uint8_t idx) {
  switch (b.vtype) {
    case VT_F32:  *(float*)out_value    = (b.scope==SCOPE_GLOBAL)? *(float*)b.ptr    : ((float*)b.ptr)[idx]; break;
    case VT_U16:  *(uint16_t*)out_value = (b.scope==SCOPE_GLOBAL)? *(uint16_t*)b.ptr : ((uint16_t*)b.ptr)[idx]; break;
    case VT_U8:   *(uint8_t*)out_value  = (b.scope==SCOPE_GLOBAL)? *(uint8_t*)b.ptr  : ((uint8_t*)b.ptr)[idx]; break;
    case VT_I32:  *(int32_t*)out_value  = (b.scope==SCOPE_GLOBAL)? *(int32_t*)b.ptr  : ((int32_t*)b.ptr)[idx]; break;
    case VT_BOOL: *(bool*)out_value     = (b.scope==SCOPE_GLOBAL)? *(bool*)b.ptr     : ((bool*)b.ptr)[idx]; break;
    case VT_U32:  *(uint32_t*)out_value = (b.scope==SCOPE_GLOBAL)? *(uint32_t*)b.ptr : ((uint32_t*)b.ptr)[idx]; break;
  }
}

static inline uint16_t calc_eeprom_offset(const MenuBinding& b, uint8_t idx){
  if (!b.persist) return 0;
  if (b.scope == SCOPE_GLOBAL) return b.ee_base;
  return (uint16_t)(b.ee_base + (uint16_t)idx * b.ee_stride);
}

bool menu_read_by_bind(const char* bind, void* out_value) {
  const MenuBinding* b = menu_find_bind(bind);
  if (!b || !out_value) return false;
  uint8_t idx = 0;
  if (b->scope == SCOPE_PER_CONTROLLER) idx = menu_get_active_controller();
  read_value(*b, out_value, idx);
  return true;
}

bool menu_apply_by_bind(const char* bind, float v) {
  const MenuBinding* b = menu_find_bind(bind);
  if (!b) return false;
  uint8_t idx = 0;
  if (b->scope == SCOPE_PER_CONTROLLER) idx = menu_get_active_controller();
  store_value(*b, v, idx);
  if (b->persist) {
    uint16_t ee = calc_eeprom_offset(*b, idx);
    switch (b->vtype) {
      case VT_F32:  ee_store_field<float>(   ee, (b->scope==SCOPE_GLOBAL)? *(float*)b->ptr    : ((float*)b->ptr)[idx]); break;
      case VT_U16:  ee_store_field<uint16_t>(ee, (b->scope==SCOPE_GLOBAL)? *(uint16_t*)b->ptr : ((uint16_t*)b->ptr)[idx]); break;
      case VT_U8:   ee_store_field<uint8_t>( ee, (b->scope==SCOPE_GLOBAL)? *(uint8_t*)b->ptr  : ((uint8_t*)b->ptr)[idx]); break;
      case VT_I32:  ee_store_field<int32_t>( ee, (b->scope==SCOPE_GLOBAL)? *(int32_t*)b->ptr  : ((int32_t*)b->ptr)[idx]); break;
      case VT_BOOL: ee_store_field<bool>(    ee, (b->scope==SCOPE_GLOBAL)? *(bool*)b->ptr     : ((bool*)b->ptr)[idx]); break;
      case VT_U32:  ee_store_field<uint32_t>(ee, (b->scope==SCOPE_GLOBAL)? *(uint32_t*)b->ptr : ((uint32_t*)b->ptr)[idx]); break;
    }
  }
  if (b->on_change) b->on_change((void*)b->ptr);
  if (g_config_change_hook) g_config_change_hook(b->id, idx, b->bind);
  return true;
}
