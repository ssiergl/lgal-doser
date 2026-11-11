#pragma once
// Keeps the hardware random number generator on true random numbers.
//
// The ESP32-S3 RNG is only guaranteed to be truly random while the radio
// runs or the SAR ADC noise source is on (ESP-IDF "Random Number
// Generation"). In LAN operation the radio is off, yet session tokens, API
// keys, password salts and TLS keys need real entropy. So the SAR ADC source
// is switched on at start and only switched off while Wi-Fi uses the radio
// (the two must not run together). The firmware uses no ADC otherwise.

namespace lgal::entropy {

// First thing in app_main: noise source on
void init();
// Before esp_wifi_init()/esp_wifi_start(): radio(true); after esp_wifi_stop()
// (or when the radio stays off after init): radio(false)
void radio(bool on);

}  // namespace lgal::entropy
