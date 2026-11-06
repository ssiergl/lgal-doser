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
// Setup/admin password (12 characters), = HMAC-SHA256(secret, "LGAL-AP" + MAC)
const std::string& setup_password();

}  // namespace lgal::device
