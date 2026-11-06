#pragma once
// RAM ring buffer for log output. Replaces the serial console, since the
// board is not reachable via USB in the housing (available via the system API).

#include <cstddef>
#include <string>

namespace lgal::logbuf {

// Hooks into esp_log. The buffer lives in PSRAM if present.
void init(size_t capacity = 32 * 1024);

// Copy of the current content (oldest line first).
std::string snapshot();

}  // namespace lgal::logbuf
