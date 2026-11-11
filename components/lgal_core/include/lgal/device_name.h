#pragma once
// Device name and host name (pure logic, host-testable).
//
// The user gives the device a name ("Reef tank living room"). The host name
// (DHCP, mDNS "<host>.local") follows from it as a slug ("reef-tank-living-
// room"); in pro mode it can be set on its own. Without a name and without
// an own host name the factory host name "lgal-<type>-xxxx" stays.

#include <cstddef>
#include <string>

namespace lgal::devname {

inline constexpr size_t kNameMax = 32;  // bytes (UTF-8)
inline constexpr size_t kHostMax = 32;  // characters, one DNS label

// Up to kNameMax bytes, no control characters, not only spaces ("" = no name)
bool valid_name(const char* s);
// 1..kHostMax of a-z, 0-9 and "-", not starting or ending with "-"
bool valid_host(const char* s);
// Host name from a name: lower case, umlauts as ae/oe/ue/ss, accents
// dropped, every other run of characters becomes one "-"; "" if nothing is left
std::string slug(const char* name);
// Host name in use: own host name, else the slug of the name, else fallback
std::string effective(const char* name, const char* host, const std::string& fallback);

}  // namespace lgal::devname
