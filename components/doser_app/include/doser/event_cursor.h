#pragma once
// Position of the MQTT event stream in the history (M6, spec 1 and 9). Every
// history event is sent once, oldest first; after a broker or network outage
// or a restart the buffered events follow (at least once: a restart may
// repeat the events of the last minute). Pure logic, host-tested;
// mqtt_ha.cpp reads the history and publishes.

#include <cstddef>
#include <cstdint>

#include "doser/history_core.h"

namespace doser::event_cursor {

// Events up to and including utc are sent, n of them at utc itself (the
// history may hold several events within one second).
struct Cursor {
    uint32_t utc = 0;
    uint16_t n = 0;
};

// Last second of the window read in one go: the UTC day of the cursor
// (one history file), at most `until`.
uint32_t window_end(const Cursor& c, uint32_t until);

using SendFn = bool (*)(const history::Event& e, void* ctx);

// ev: events of the window [c->utc, end], oldest first; complete = false when
// the reader could only keep the oldest part of the window. Sends the events
// after the cursor (at most max; send returns false to stop) and moves the
// cursor along. Returns true when the window is done: then the cursor is at
// end + 1 and the caller may read the next window.
bool advance(Cursor* c, uint32_t end, const history::Event* ev, size_t count, bool complete, size_t max,
             SendFn send, void* ctx, size_t* sent);

}  // namespace doser::event_cursor
