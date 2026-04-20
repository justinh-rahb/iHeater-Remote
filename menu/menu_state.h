// Auto-generated. Do not edit.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "menu_ids.h"

#ifndef NUM_UNITS
#define NUM_UNITS 3
#endif

class MenuState {
public:
  bool iheater_link_enabled = true;
  float iheater_link_pla_temp = 45.0f;
  float iheater_link_petg_temp = 50.0f;
  float iheater_link_abs_temp = 60.0f;
  float iheater_link_asa_temp = 65.0f;
  float iheater_link_pc_temp = 70.0f;
  float iheater_link_pa_temp = 70.0f;
  float iheater_link_unknown_temp = 50.0f;
  bool ha_enabled = false;
  uint8_t units_count = (uint8_t)1;
  uint8_t language = (uint8_t)1;

  void initDefaults();   // выставить дефолты (из YAML)
  void loadFromEEPROM(); // подхватить, если MAGIC/VER совпали
  void saveToEEPROM();   // записать только изменённые поля
};

extern MenuState menu;