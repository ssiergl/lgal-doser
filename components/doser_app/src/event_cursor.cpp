#include "doser/event_cursor.h"

namespace doser::event_cursor {

uint32_t window_end(const Cursor& c, uint32_t until) {
    const uint64_t day_end = (uint64_t(c.utc) / 86400 + 1) * 86400 - 1;
    return day_end < until ? static_cast<uint32_t>(day_end) : until;
}

bool advance(Cursor* c, uint32_t end, const history::Event* ev, size_t count, bool complete, size_t max,
             SendFn send, void* ctx, size_t* sent) {
    size_t done = 0;
    uint16_t skip = c->n;  // already sent at c->utc
    const uint32_t start = c->utc;
    for (size_t i = 0; i < count; ++i) {
        const history::Event& e = ev[i];
        if (e.utc < start || e.utc > end) continue;
        if (e.utc == start && skip) {
            --skip;
            continue;
        }
        if (done == max || !send(e, ctx)) {
            if (sent) *sent = done;
            return false;
        }
        ++done;
        if (e.utc == c->utc) {
            ++c->n;
        } else {
            c->utc = e.utc;
            c->n = 1;
        }
    }
    if (sent) *sent = done;
    if (!complete) return false;
    c->utc = end + 1;
    c->n = 0;
    return true;
}

}  // namespace doser::event_cursor
