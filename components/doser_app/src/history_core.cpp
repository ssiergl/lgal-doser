#include "doser/history_core.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace doser::history {

namespace {
// Stored as index on flash: only ever append, never reorder.
constexpr const char* kCodes[] = {
    "other",           "success",          "aborted",            "fault",          "locked",
    "heartbeat_lost",  "max_duration",     "missed",             "daily_limit",    "delay_timeout",
    "pump_disabled",   "pump_manual_only", "driver_unavailable", "time_invalid",   "dose_too_small",
    "dose_too_large",  "pump_busy",        "collision_lock",     "container_low",  "tube_worn",
    "rtc_fault",       "set",              "refilled",           "started",        "cancelled",
    "finished",        "cleared",          "boot",               "updated",        "restored",
    "time_set",        "settings",         "channels",           "engineering",    "network",
    "mqtt",            "password",         "key_added",          "key_removed",
};
constexpr uint8_t kCodeCount = sizeof(kCodes) / sizeof(kCodes[0]);
constexpr const char* kKinds[] = {"",         "dose",     "prime",  "calib_run", "skip",      "container",
                                  "calibration", "tube",  "alarm",  "boost",     "mode",      "schedule",
                                  "settings",  "locks",    "alarm_end", "system"};
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == kKindMax + 1);
constexpr const char* kSources[] = {"", "manual", "api", "schedule", "boost", "mqtt"};
constexpr int kMaxDays = 400;

uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) c = static_cast<uint8_t>(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}  // namespace

const char* kind_str(Kind k) {
    const auto i = static_cast<uint8_t>(k);
    return i >= 1 && i <= kKindMax ? kKinds[i] : "unknown";
}

bool parse_kind(const char* s, Kind* out) {
    for (uint8_t i = 1; i <= kKindMax; ++i)
        if (s && std::strcmp(s, kKinds[i]) == 0) {
            *out = static_cast<Kind>(i);
            return true;
        }
    return false;
}

const char* source_str(Source s) {
    const auto i = static_cast<uint8_t>(s);
    return i < sizeof(kSources) / sizeof(kSources[0]) ? kSources[i] : "";
}

const char* code_str(uint8_t code) { return code < kCodeCount ? kCodes[code] : kCodes[0]; }

uint8_t code_of(const char* s) {
    if (!s) return 0;
    for (uint8_t i = 1; i < kCodeCount; ++i)
        if (std::strcmp(s, kCodes[i]) == 0) return i;
    return 0;
}

// Layout (little endian): utc u32 | kind u8 | pump u8 | source u8 | code u8 |
// target u32 | done u32 | value i32 | reserved 3 | crc8 over bytes 0..22
void encode(const Event& e, uint8_t (&out)[kRecordSize]) {
    std::memset(out, 0, sizeof(out));
    put32(out, e.utc);
    out[4] = static_cast<uint8_t>(e.kind);
    out[5] = e.pump;
    out[6] = static_cast<uint8_t>(e.source);
    out[7] = e.code;
    put32(out + 8, e.target_ul);
    put32(out + 12, e.done_ul);
    put32(out + 16, static_cast<uint32_t>(e.value));
    out[23] = crc8(out, 23);
}

bool decode(const uint8_t* in, Event* out) {
    if (crc8(in, 23) != in[23]) return false;
    const uint8_t kind = in[4];
    if (kind < 1 || kind > kKindMax || get32(in) == 0) return false;
    out->utc = get32(in);
    out->kind = static_cast<Kind>(kind);
    out->pump = in[5];
    out->source = static_cast<Source>(in[6]);
    out->code = in[7];
    out->target_ul = get32(in + 8);
    out->done_ul = get32(in + 12);
    out->value = static_cast<int32_t>(get32(in + 16));
    return true;
}

Store::Store(const char* dir) {
    std::snprintf(dir_, sizeof(dir_), "%s", dir);
}

void Store::path_of(int32_t day, char (&out)[64]) const { std::snprintf(out, sizeof(out), "%s/%ld.ev", dir_, long(day)); }

int Store::days(int32_t* out, int max) const {
    int n = 0;
    DIR* d = opendir(dir_);
    if (!d) return 0;
    while (dirent* e = readdir(d)) {
        char* end = nullptr;
        const long v = std::strtol(e->d_name, &end, 10);
        if (end == e->d_name || std::strcmp(end, ".ev") != 0 || v < 0) continue;
        if (n < max) out[n++] = static_cast<int32_t>(v);
    }
    closedir(d);
    std::sort(out, out + n, [](int32_t a, int32_t b) { return a > b; });
    return n;
}

bool Store::append(const Event& e) {
    char path[64];
    path_of(day_of_utc(e.utc), path);
    FILE* f = std::fopen(path, "ab");
    if (!f) return false;
    // a torn record from a power cut would shift all later ones: realign
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    bool ok = true;
    if (size > 0 && size % long(kRecordSize)) {
        static const uint8_t kPad[kRecordSize] = {};
        ok = std::fwrite(kPad, 1, kRecordSize - size % kRecordSize, f) == kRecordSize - size % kRecordSize;
    }
    uint8_t rec[kRecordSize];
    encode(e, rec);
    ok = ok && std::fwrite(rec, 1, sizeof(rec), f) == sizeof(rec);
    return std::fclose(f) == 0 && ok;
}

size_t Store::query(const Filter& flt, bool (*cb)(const Event&, void*), void* ctx) const {
    int32_t list[kMaxDays];
    const int n = days(list, kMaxDays);
    const int32_t first = day_of_utc(flt.from), last = day_of_utc(flt.to);
    constexpr size_t kChunk = 32;
    uint8_t buf[kChunk * kRecordSize];
    size_t hits = 0;
    for (int i = 0; i < n; ++i) {
        if (list[i] > last) continue;
        if (list[i] < first) break;
        char path[64];
        path_of(list[i], path);
        FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        // records are appended in time order: read chunks from the end
        size_t pos = size > 0 ? size_t(size) / kRecordSize : 0;
        bool stop = false;
        while (pos && !stop) {
            const size_t n_rec = pos < kChunk ? pos : kChunk;
            pos -= n_rec;
            if (std::fseek(f, long(pos * kRecordSize), SEEK_SET) != 0) break;
            const size_t got = std::fread(buf, kRecordSize, n_rec, f);
            for (size_t k = got; k-- > 0 && !stop;) {
                Event e;
                if (!decode(buf + k * kRecordSize, &e) || !flt.match(e)) continue;
                ++hits;
                stop = !cb(e, ctx);
            }
        }
        std::fclose(f);
        if (stop) break;
    }
    return hits;
}

int Store::prune(uint32_t now_utc, uint16_t keep_days) {
    int32_t list[kMaxDays];
    const int n = days(list, kMaxDays);
    const int32_t oldest = day_of_utc(now_utc) - keep_days;
    int removed = 0;
    for (int i = 0; i < n; ++i) {
        if (list[i] >= oldest) continue;
        char path[64];
        path_of(list[i], path);
        if (std::remove(path) == 0) ++removed;
    }
    return removed;
}

void Store::usage(int* files, uint64_t* bytes) const {
    int32_t list[kMaxDays];
    const int n = days(list, kMaxDays);
    uint64_t total = 0;
    for (int i = 0; i < n; ++i) {
        char path[64];
        path_of(list[i], path);
        struct stat st {};
        if (stat(path, &st) == 0) total += uint64_t(st.st_size);
    }
    *files = n;
    *bytes = total;
}

int Store::clear() { return prune(UINT32_MAX, 0); }

}  // namespace doser::history
