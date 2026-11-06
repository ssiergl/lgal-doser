#pragma once
// Persistence of the dosing data.
//
//  * Settings (rarely changed): NVS namespace "dose"
//      "eng"     engineering parameters (global)
//      "p0".."p4" pump master data
//  * Run state (after every run): EEPROM AT24C32, two slots per pump
//    (A/B) with sequence number and CRC32. The older slot is always written,
//    a power loss while writing leaves the other slot intact.
//      0x000 + pump * 320 + slot * 160   (used up to 0x63F)
//      0xFE0..0xFFF                      self test (i2c_devices)
//    Without EEPROM (or on write error) the state goes to NVS ("s0".."s4").
//    On load the valid record with the highest sequence number wins.

#include "doser/dosing_core.h"
#include "esp_err.h"

namespace doser::dosing_store {

void load_engineering(dose::Engineering* e);
esp_err_t save_engineering(const dose::Engineering& e);

void load_settings(int pump, dose::PumpSettings* p);
esp_err_t save_settings(int pump, const dose::PumpSettings& p);

// false = no valid record found (s stays unchanged)
bool load_state(int pump, dose::PumpState* s);
esp_err_t save_state(int pump, const dose::PumpState& s);

const char* state_backend();  // "eeprom" / "nvs"

}  // namespace doser::dosing_store
