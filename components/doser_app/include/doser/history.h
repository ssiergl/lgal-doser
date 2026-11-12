#pragma once
// Event history on the "storage" LittleFS partition (functional spec
// chapter 9). record() never blocks: events go through a queue to a writer
// task that appends them to day files and prunes old days once per hour.

#include <cstddef>
#include <cstdint>

#include "doser/history_core.h"
#include "doser/limits.h"
#include "esp_err.h"

namespace doser::history {

// Mount the partition (formats it if it cannot be mounted) and start the writer
esp_err_t init();
bool ready();

// Queue an event. utc 0 = now; dropped without a valid clock or when the queue is full.
void record(const Event& e);

// Event "now" of a pump (0-based) or of the device (pump < 0)
inline void note(Kind kind, int pump, Source source, const char* code, uint32_t target_ul = 0, uint32_t done_ul = 0,
                 int32_t value = 0) {
    Event e;
    e.kind = kind;
    e.pump = pump >= 0 && pump < kMaxPumps ? static_cast<uint8_t>(pump + 1) : 0;
    e.source = source;
    e.code = code_of(code);
    e.target_ul = target_ul;
    e.done_ul = done_ul;
    e.value = value;
    record(e);
}

// Run a query under the store lock (cb must not call record()).
size_t query(const Filter& f, bool (*cb)(const Event&, void*), void* ctx);

struct Usage {
    int days = 0;
    uint64_t bytes = 0;
    size_t fs_total = 0, fs_used = 0;
    uint32_t dropped = 0;  // events lost since boot (queue full or write error)
};
Usage usage();
esp_err_t clear();

// Retention (NVS "history"/"keep", default kKeepDaysDefault). A shorter
// value deletes the older days within a minute.
uint16_t keep_days();
esp_err_t set_keep_days(uint16_t days);  // ESP_ERR_INVALID_ARG outside kKeepDaysMin..Max

}  // namespace doser::history
