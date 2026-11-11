#pragma once
// Source of a change for the event history: a browser session counts as
// "manual" (the person at the web interface), an API key as "api".

#include "doser/dosing.h"
#include "doser/history.h"
#include "esp_http_server.h"
#include "lgal/auth.h"

namespace doser {

inline bool by_api_key(httpd_req_t* req) {
    lgal::auth::Principal p;
    return lgal::auth::principal(req, &p) && p.key_slot >= 0;
}

inline history::Source source_of(httpd_req_t* req) {
    return by_api_key(req) ? history::Source::Api : history::Source::Manual;
}

inline dosing::Trigger trigger_of(httpd_req_t* req) {
    return by_api_key(req) ? dosing::Trigger::Api : dosing::Trigger::Manual;
}

// Device event (history kind "system") made by this request
inline void note_system(httpd_req_t* req, const char* code, int32_t value = 0) {
    history::note(history::Kind::System, -1, source_of(req), code, 0, 0, value);
}

}  // namespace doser
