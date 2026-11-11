#include "lgal/events.h"

#include "lgal/auth.h"

namespace lgal::events {

namespace {
Hook s_hook = nullptr;  // set once at start, before the servers run
}  // namespace

void set_hook(Hook h) { s_hook = h; }

void note(const char* code, By by) {
    if (s_hook) s_hook(code, by);
}

void note(const char* code, httpd_req_t* req) {
    auth::Principal p;
    note(code, req && auth::principal(req, &p) && p.key_slot >= 0 ? By::ApiKey : By::Browser);
}

}  // namespace lgal::events
