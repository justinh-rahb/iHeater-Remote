// Auto-generated. Do not edit.
#pragma once
#include <stdint.h>

#define EE_MAGIC   0x4D4E5551
#define EE_VERSION 1

#ifndef NUM_UNITS
#define NUM_UNITS 3
#endif

// System area (fixed at start of EEPROM)
#define EE_SYS_OFF_MAGIC   0
#define EE_SYS_OFF_VERSION 4
#define EE_OFF_WT_HOURS    8
#define EE_OFF_WT_MINUTES  12

// ---- Scales calibration (protected, inside system area) ----
#define EE_SYS_CAL_BASE            16

// Ноль для каждой катушки: uint32_t[4]
#define EE_SYS_CAL_ZERO_BASE       (EE_SYS_CAL_BASE + 0)
#define EE_SYS_CAL_ZERO(i)         (EE_SYS_CAL_ZERO_BASE + (uint16_t)((i) * sizeof(uint32_t))) // i=0..3

// Оффсет/масштаб для каждой катушки: uint32_t[4]
#define EE_SYS_CAL_OFFSET_BASE     (EE_SYS_CAL_ZERO_BASE + 4*4)
#define EE_SYS_CAL_OFFSET(i)       (EE_SYS_CAL_OFFSET_BASE + (uint16_t)((i) * sizeof(uint32_t)))

// общий размер блока калибровки + 16 на всякий
#define EE_SYS_CAL_SIZE            (sizeof(uint32_t) * 4 * 2 + 16)

// ---- Scales temperature offsets table (uint32_t[4][6]) ----
#define EE_SCALE_TEMP_BASE              (EE_SYS_CAL_BASE + EE_SYS_CAL_SIZE)
#define EE_SCALE_TEMP_STRIDE_SPOOL      (6 * 4)   // 6 points per spool * 4 bytes each
// Accessor: spool i=0..3, point j=0..5 (e.g., temps 60..110)
#define EE_SCALE_TEMP(i,j)             (EE_SCALE_TEMP_BASE + (uint16_t)((i) * EE_SCALE_TEMP_STRIDE_SPOOL + (j) * sizeof(uint32_t)))
// Table size: 4 spools * 6 points * 4 bytes = 96 bytes
#define EE_SCALE_TEMP_SIZE             (4*6*4)

// Итоговый размер системной области: 16 (MAGIC..MINUTES) + 48 (калибровка) = 64
#define EE_SYS_AREA_SIZE           160

// ---- Error log area ----
#define ERRLOG_CAP           128
#define ERRLOG_REC_SIZE      16
#define ERRLOG_HDR_SIZE      12
#define EE_ERR_BASE          (EE_SYS_AREA_SIZE)
#define EE_ERR_OFF_HDR       (EE_ERR_BASE)
#define EE_ERR_OFF_RECS      (EE_ERR_BASE + ERRLOG_HDR_SIZE)
#define EE_ERR_AREA_SIZE     (ERRLOG_HDR_SIZE + (ERRLOG_CAP * ERRLOG_REC_SIZE))

#define EE_OFF_IHEATER_LINK_ENABLED 2220
#define EE_OFF_IHEATER_LINK_PLA_TEMP 2221
#define EE_OFF_IHEATER_LINK_PETG_TEMP 2225
#define EE_OFF_IHEATER_LINK_ABS_TEMP 2229
#define EE_OFF_IHEATER_LINK_ASA_TEMP 2233
#define EE_OFF_IHEATER_LINK_PC_TEMP 2237
#define EE_OFF_IHEATER_LINK_PA_TEMP 2241
#define EE_OFF_IHEATER_LINK_UNKNOWN_TEMP 2245
#define EE_OFF_HA_ENABLED 2249
#define EE_OFF_UNITS_COUNT 2250
#define EE_OFF_LANGUAGE 2251

#define EE_TOTAL_SIZE 2252
