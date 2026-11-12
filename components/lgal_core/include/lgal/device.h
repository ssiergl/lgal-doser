#pragma once
// Device identity: names and setup password derived from the base MAC, plus
// the name the user gives the device and the host name that follows from it
// (lgal/device_name.h).

#include <string>

#include "esp_err.h"

namespace lgal::device {

// Must be called once before all other functions (after settings::init).
void init();

const std::string& mac_str();       // "AA:BB:CC:DD:EE:FF" (base MAC)
const std::string& short_id();      // "eeff" (last 2 bytes, lower case)
// Factory host name "lgal-doser-eeff": never changes, so it serves as MQTT
// client id and in the default MQTT base topic
const std::string& default_hostname();
std::string hostname();             // in use (DHCP, mDNS): own, from the name or default
std::string name();                 // given by the user, "" = none
std::string own_hostname();         // set in pro mode, "" = follows the name
// Saves both (checked by the caller with devname::valid_name/valid_host or
// "" for the host); the network applies the host name with net::apply_hostname()
esp_err_t set_names(const std::string& name, const std::string& host);

const std::string& setup_ssid();    // "LG-Aquatic-Life-Setup-EEFF"
// Both passwords come from the device secret in the factory partition "fctry"
// (lgal/auth_core.h, written by tools/provision.py at the first USB flash;
// neither the factory reset nor an update touches it). The firmware image
// holds no secret.
//
// Device password on the label (12 characters) = HMAC-SHA256(secret, "LGAL-AP" + MAC):
// setup Wi-Fi password, login before the setup and admin password recovery
// via the setup Wi-Fi.
const std::string& setup_password();
// Engineering key of the manufacturer (16 characters, label "LGAL-ENG"),
// never shown in the web UI; tools/ap_password.py --engineering.
const std::string& engineering_key();
// true if this start created the device secret (nothing provisioned): the
// serial console then shows both passwords once, nobody else can know them.
bool secret_created();

}  // namespace lgal::device
