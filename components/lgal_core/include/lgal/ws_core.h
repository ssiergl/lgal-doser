#pragma once
// WebSocket helpers without ESP dependencies (host tests: tests/host/test_ws.cpp).

#include <cstddef>
#include <cstdint>

namespace lgal::ws {

// Cross-site WebSocket hijacking protection: browsers send the session
// cookie with a WebSocket handshake from any page, so a browser handshake
// (it always carries an Origin header) is accepted only from the device's
// own pages: Origin "http://<Host>" or "https://<Host>", host compared
// case-insensitively including the port. Clients without Origin (apps,
// Home Assistant) are not browsers and pass. origin == nullptr: no header.
bool origin_allowed(const char* origin, const char* host);

// FNV-1a 32 bit, change detection of the status message
uint32_t fnv1a(const char* s, size_t len);

}  // namespace lgal::ws
