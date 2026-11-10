// Host tests for the cursor of the MQTT event stream.
#include <cstdio>
#include <vector>

#include "doser/event_cursor.h"

using namespace doser;
using event_cursor::Cursor;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static history::Event ev(uint32_t utc, uint8_t pump) {
    history::Event e;
    e.utc = utc;
    e.pump = pump;
    return e;
}

struct Sink {
    std::vector<history::Event> got;
    int fail_after = -1;  // send fails from this many sent events on (broker gone)
};

static bool send(const history::Event& e, void* ctx) {
    auto* s = static_cast<Sink*>(ctx);
    if (s->fail_after >= 0 && int(s->got.size()) >= s->fail_after) return false;
    s->got.push_back(e);
    return true;
}

constexpr uint32_t kDay = 86400;
constexpr uint32_t kD0 = 20000 * kDay;  // some UTC day

static void test_window() {
    std::printf("Window\n");
    CHECK(event_cursor::window_end({kD0 + 100, 0}, kD0 + 5000) == kD0 + 5000);
    CHECK(event_cursor::window_end({kD0 + 100, 0}, kD0 + 3 * kDay) == kD0 + kDay - 1);
    CHECK(event_cursor::window_end({kD0, 0}, kD0 + kDay) == kD0 + kDay - 1);
}

static void test_advance() {
    std::printf("Advance\n");
    // three events, two in the same second
    const history::Event day[] = {ev(kD0 + 10, 1), ev(kD0 + 20, 2), ev(kD0 + 20, 3), ev(kD0 + 30, 4)};
    Cursor c{kD0, 0};
    Sink s;
    size_t sent = 0;
    // limit 2 per round: stops in the middle of the second kD0+20
    CHECK(!event_cursor::advance(&c, kD0 + 100, day, 4, true, 2, send, &s, &sent));
    CHECK(sent == 2 && s.got.size() == 2 && c.utc == kD0 + 20 && c.n == 1);
    // next round sees the same window again and skips what was sent
    CHECK(event_cursor::advance(&c, kD0 + 100, day + 1, 3, true, 10, send, &s, &sent));
    CHECK(sent == 2 && s.got.size() == 4 && s.got[2].pump == 3 && s.got[3].pump == 4);
    CHECK(c.utc == kD0 + 101 && c.n == 0);
    // nothing new: window done without sending
    CHECK(event_cursor::advance(&c, kD0 + 200, nullptr, 0, true, 10, send, &s, &sent));
    CHECK(sent == 0 && c.utc == kD0 + 201);
}

static void test_outage() {
    std::printf("Broker outage\n");
    const history::Event day[] = {ev(kD0 + 10, 1), ev(kD0 + 11, 2), ev(kD0 + 12, 3)};
    Cursor c{kD0, 0};
    Sink s;
    s.fail_after = 1;
    size_t sent = 0;
    CHECK(!event_cursor::advance(&c, kD0 + 50, day, 3, true, 10, send, &s, &sent));
    CHECK(sent == 1 && c.utc == kD0 + 10 && c.n == 1);
    // reconnected: the rest follows, nothing twice
    s.fail_after = -1;
    CHECK(event_cursor::advance(&c, kD0 + 50, day, 3, true, 10, send, &s, &sent));
    CHECK(sent == 2 && s.got.size() == 3 && s.got[1].pump == 2 && s.got[2].pump == 3);
}

static void test_incomplete() {
    std::printf("Incomplete window\n");
    // the reader only kept the oldest two of a busy day
    const history::Event part[] = {ev(kD0 + 1, 1), ev(kD0 + 2, 2)};
    Cursor c{kD0, 0};
    Sink s;
    size_t sent = 0;
    CHECK(!event_cursor::advance(&c, kD0 + kDay - 1, part, 2, false, 10, send, &s, &sent));
    CHECK(sent == 2 && c.utc == kD0 + 2 && c.n == 1);  // not past the window: the rest comes next round
    // events before the cursor or after the window end are ignored
    const history::Event odd[] = {ev(kD0 + 1, 1), ev(kD0 + 2, 2), ev(kD0 + 5, 3), ev(kD0 + 9, 4)};
    CHECK(event_cursor::advance(&c, kD0 + 8, odd, 4, true, 10, send, &s, &sent));
    CHECK(sent == 1 && s.got.back().pump == 3 && c.utc == kD0 + 9);
}

int main() {
    test_window();
    test_advance();
    test_outage();
    test_incomplete();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
