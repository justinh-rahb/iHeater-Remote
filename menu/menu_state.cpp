// Auto-generated. Do not edit.
#include <string.h>
#include <EEPROM.h>
#include "menu_state.h"
#include "menu_types.h"
#include "menu_eeprom.h"
#include "menu_eeprom_io.h"

MenuState menu;

void MenuState::initDefaults(){
  this->iheater_link_enabled = true;
  this->iheater_link_pla_temp = 45.0f;
  this->iheater_link_petg_temp = 50.0f;
  this->iheater_link_abs_temp = 60.0f;
  this->iheater_link_asa_temp = 65.0f;
  this->iheater_link_pc_temp = 70.0f;
  this->iheater_link_pa_temp = 70.0f;
  this->iheater_link_unknown_temp = 50.0f;
  this->ha_enabled = false;
  this->units_count = (uint8_t)1;
  this->language = (uint8_t)1;
}

void MenuState::loadFromEEPROM(){
  uint32_t magic=0, ver=0;
  ee_read(EE_SYS_OFF_MAGIC, magic);
  ee_read(EE_SYS_OFF_VERSION, ver);
  if (magic != EE_MAGIC || ver != EE_VERSION) return;
  ee_read(2220, this->iheater_link_enabled);
  ee_read(2221, this->iheater_link_pla_temp);
  ee_read(2225, this->iheater_link_petg_temp);
  ee_read(2229, this->iheater_link_abs_temp);
  ee_read(2233, this->iheater_link_asa_temp);
  ee_read(2237, this->iheater_link_pc_temp);
  ee_read(2241, this->iheater_link_pa_temp);
  ee_read(2245, this->iheater_link_unknown_temp);
  ee_read(2249, this->ha_enabled);
  ee_read(2250, this->units_count);
  ee_read(2251, this->language);
}

void MenuState::saveToEEPROM(){
  ee_write(EE_SYS_OFF_MAGIC,   (uint32_t)EE_MAGIC);
  ee_write(EE_SYS_OFF_VERSION, (uint32_t)EE_VERSION);
  ee_store_field(2220, this->iheater_link_enabled);
  ee_store_field(2221, this->iheater_link_pla_temp);
  ee_store_field(2225, this->iheater_link_petg_temp);
  ee_store_field(2229, this->iheater_link_abs_temp);
  ee_store_field(2233, this->iheater_link_asa_temp);
  ee_store_field(2237, this->iheater_link_pc_temp);
  ee_store_field(2241, this->iheater_link_pa_temp);
  ee_store_field(2245, this->iheater_link_unknown_temp);
  ee_store_field(2249, this->ha_enabled);
  ee_store_field(2250, this->units_count);
  ee_store_field(2251, this->language);
}