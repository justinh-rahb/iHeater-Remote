#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace iheaterlink {

inline void applyConfiguredWifiTxPower(const char *context) {
#if defined(IHEATER_WIFI_TX_POWER)
  const bool ok = WiFi.setTxPower((wifi_power_t)IHEATER_WIFI_TX_POWER);
  Serial.printf("[WIFI] TX power %s power=%d context=%s\n",
                ok ? "set" : "not-set", (int)IHEATER_WIFI_TX_POWER,
                context ? context : "-");
#else
  (void)context;
#endif
}

} // namespace iheaterlink
