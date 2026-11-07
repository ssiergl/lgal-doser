#pragma once
// Event history on the "storage" LittleFS partition (functional spec
// chapter 9). record() never blocks: events go through a queue to a writer
// task that appends them to day files and prunes old days once per hour.

#include <cstddef>
#include <cstdint>

#include "doser/history_core.h"
#include "esp_err.h"

namespace doser::history {

constexpr uint16_t kKeepDays = 60;

// Mount the partition (formats it if it cannot be mounted) and start the writer
esp_err_t init();
bool ready();

// Queue an event. utc 0 = now; dropped without a valid clock or when the queue is full.
void record(const Event& e);

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

}  // namespace doser::history
