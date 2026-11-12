#pragma once
// What a signed-in browser may reach before the first setup is completed
// (spec 2). Until then the device only lets the setup itself through: the
// wizard and what its steps need (sign-in, network, time, language, system
// information), the maintenance page and firmware updates.
// Everything else - pumps, schedules, history, backup, MQTT, engineering,
// the web UI and its WebSocket - answers 409 "setup_required" (pages: a
// redirect to /setup). Pure logic, tested on the host.

#include <cstring>

namespace lgal::setup {

inline bool open_during_setup(const char* uri) {
    static constexpr const char* kOpen[] = {
        "/setup",          "/api/v1/setup", "/login",     "/api/v1/auth/", "/api/v1/net/",
        "/api/v1/system/", "/api/v1/ota",   "/recovery",  "/i18n.js",
    };
    if (!uri) return false;
    for (const char* p : kOpen) {
        const size_t n = std::strlen(p);
        // the prefix must end at a path or query boundary: "/setup" opens "/setup?x", not "/setupx"
        if (std::strncmp(uri, p, n) == 0 && (p[n - 1] == '/' || uri[n] == '\0' || uri[n] == '/' || uri[n] == '?'))
            return true;
    }
    return false;
}

}  // namespace lgal::setup
