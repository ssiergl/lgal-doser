// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lgal/events.h"

#include "lgal/auth.h"

namespace lgal::events {

namespace {
Hook s_hook = nullptr;  // set once at start, before the servers run
}  // namespace

void set_hook(Hook h) { s_hook = h; }

void note(const char* code, By by, int32_t value) {
    if (s_hook) s_hook(code, by, value);
}

void note(const char* code, httpd_req_t* req, int32_t value) {
    auth::Principal p;
    note(code, req && auth::principal(req, &p) && p.key_slot >= 0 ? By::ApiKey : By::Browser, value);
}

}  // namespace lgal::events
