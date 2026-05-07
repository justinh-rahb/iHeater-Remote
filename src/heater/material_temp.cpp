#include "heater/material_temp.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

#include <menu_state.h>

namespace iheaterlink {

float materialTempFromMenu(const char* trayType) {
    if (!trayType || trayType[0] == '\0')
        return 0.0f;

    char upper[16];
    size_t i = 0;
    for (; trayType[i] && i < sizeof(upper) - 1; i++)
        upper[i] = (char)toupper((unsigned char)trayType[i]);
    upper[i] = '\0';

    // Strip a single trailing modifier ("-CF", "-GF", "+CF", " CF", etc.)
    // so that "PA-CF" / "PETG-CF" still match the base type.
    {
        char* dash = strpbrk(upper, "-+ ");
        if (dash) *dash = '\0';
    }

    // Polyamides — specific variants first, generic PA last.
    if (strcmp(upper, "PA66") == 0)  return menu.mat_pa66;
    if (strcmp(upper, "PA12") == 0)  return menu.mat_pa12;
    if (strcmp(upper, "PA11") == 0)  return menu.mat_pa11;
    if (strcmp(upper, "PA6")  == 0)  return menu.mat_pa6;
    // Generic PA / NYLON / PAHT etc. fall back to mat_pa.
    if (strncmp(upper, "PA",   2) == 0)  return menu.mat_pa;
    if (strcmp(upper, "NYLON") == 0)     return menu.mat_pa;

    // Common consumer types.
    if (strcmp(upper, "PETG") == 0)  return menu.mat_petg;
    if (strcmp(upper, "PLA")  == 0)  return menu.mat_pla;
    if (strcmp(upper, "ABS")  == 0)  return menu.mat_abs;
    if (strcmp(upper, "ASA")  == 0)  return menu.mat_asa;

    // Engineering / specialty (chamber ≥ 45 °C, exact codes from server DB).
    if (strcmp(upper, "PEEK") == 0)  return menu.mat_peek;
    if (strcmp(upper, "PEKK") == 0)  return menu.mat_pekk;
    if (strcmp(upper, "PEI")  == 0)  return menu.mat_pei;
    if (strcmp(upper, "PES")  == 0)  return menu.mat_pes;
    if (strcmp(upper, "PPSU") == 0)  return menu.mat_ppsu;
    if (strcmp(upper, "PSU")  == 0)  return menu.mat_psu;
    if (strcmp(upper, "PPA")  == 0)  return menu.mat_ppa;
    if (strcmp(upper, "PPS")  == 0)  return menu.mat_pps;
    if (strcmp(upper, "PPE")  == 0)  return menu.mat_ppe;
    if (strcmp(upper, "PMMA") == 0)  return menu.mat_pmma;
    if (strcmp(upper, "PVDF") == 0)  return menu.mat_pvdf;
    if (strcmp(upper, "POM")  == 0)  return menu.mat_pom;
    if (strcmp(upper, "PBT")  == 0)  return menu.mat_pbt;
    if (strcmp(upper, "PP")   == 0)  return menu.mat_pp;
    if (strcmp(upper, "PS")   == 0)  return menu.mat_ps;
    if (strcmp(upper, "PC")   == 0)  return menu.mat_pc;
    if (strcmp(upper, "TPI")  == 0)  return menu.mat_tpi;

    // Unknown filament type. Likely a slicer / printer firmware sent something
    // we don't have a mapping for (e.g. "PVA", "TPU", "GFA00"). Heater stays
    // OFF. Logged as anomaly so user can see "why isn't it heating?".
    Serial.printf("[!] ANOMALY HEATER: unknown tray_type='%s' — heater OFF (add mapping or check slicer)\n",
                  trayType);
    return 0.0f;
}

} // namespace iheaterlink
