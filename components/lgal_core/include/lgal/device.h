#pragma once
// Device identity: names and setup password derived from the base MAC.

#include <string>

namespace lgal::device {

// Must be called once before all other functions.
void init();

const std::string& mac_str();       // "AA:BB:CC:DD:EE:FF" (base MAC)
const std::string& short_id();      // "eeff" (last 2 bytes, lower case)
const std::string& hostname();      // "lgal-doser-eeff"
const std::string& setup_ssid();    // "LG-Aquatic-Life-Setup-EEFF"
// Device password on the label (12 characters) = HMAC-SHA256(secret, "LGAL-AP" + MAC):
// setup Wi-Fi password, login before the setup and admin password recovery
// via the setup Wi-Fi.
const std::string& setup_password();
// Engineering key of the manufacturer (16 characters, label "LGAL-ENG"),
// never shown on the device; tools/ap_password.py --engineering.
const std::string& engineering_key();

}  // namespace lgal::device
