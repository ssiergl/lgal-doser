#pragma once
// Event history (functional spec chapter 9): dosing runs, skipped slots,
// container and calibration changes and alarms as fixed 24-byte records,
// one file per UTC day ("<day>.ev") in a directory. Days older than the
// retention time are deleted as a whole. Plain stdio/dirent, so the same
// code runs on LittleFS (device) and in a temp directory (host tests).
// Not thread-safe; history.cpp serializes all access.

#include <cstddef>
#include <cstdint>

namespace doser::history {

enum class Kind : uint8_t {
    Dose = 1,     // dosing run finished (target, done, result)
    Prime,        // filling the tube finished (done, result)
    CalibRun,     // calibration run finished (target = steps, result)
    Skip,         // scheduled slot not dosed (target, reason)
    Container,    // container set or refilled (value = remaining µl, target = capacity µl)
    Calibration,  // calibration applied (value = µl per revolution × 1000, done = measured µl)
    Tube,         // tube counter set (value = hours)
    Alarm,        // alarm or warning started (code)
};
constexpr uint8_t kKindMax = static_cast<uint8_t>(Kind::Alarm);

enum class Source : uint8_t { None, Manual, Api, Schedule, Boost, Mqtt };

struct Event {
    uint32_t utc = 0;
    Kind kind = Kind::Dose;
    uint8_t pump = 0;  // 1..5, 0 = device
    Source source = Source::None;
    uint8_t code = 0;  // result, reason or alarm, see code_str()
    uint32_t target_ul = 0;
    uint32_t done_ul = 0;
    int32_t value = 0;
};

constexpr size_t kRecordSize = 24;

const char* kind_str(Kind k);
bool parse_kind(const char* s, Kind* out);
const char* source_str(Source s);
// Results, skip reasons and alarms share one table; unknown -> "other" (0)
const char* code_str(uint8_t code);
uint8_t code_of(const char* s);

void encode(const Event& e, uint8_t (&out)[kRecordSize]);
bool decode(const uint8_t* in, Event* out);  // false: empty or damaged record

inline int32_t day_of_utc(uint32_t utc) { return static_cast<int32_t>(utc / 86400); }

struct Filter {
    uint32_t from = 0;           // utc, inclusive
    uint32_t to = UINT32_MAX;    // utc, inclusive
    uint8_t pump = 0;            // 0 = all
    uint16_t kinds = 0;          // bit (1 << kind), 0 = all
    bool match(const Event& e) const {
        return e.utc >= from && e.utc <= to && (!pump || e.pump == pump) &&
               (!kinds || (kinds & (1u << static_cast<uint8_t>(e.kind))));
    }
};

class Store {
public:
    // dir without trailing slash, below 40 chars, must exist
    explicit Store(const char* dir);

    bool append(const Event& e);
    // Matching events, newest first; cb returns false to stop. Returns the
    // number of matching events passed to cb.
    size_t query(const Filter& f, bool (*cb)(const Event&, void*), void* ctx) const;
    // Delete day files older than keep_days before the day of now_utc
    int prune(uint32_t now_utc, uint16_t keep_days);
    // Number of day files and their total size
    void usage(int* files, uint64_t* bytes) const;
    // Delete everything
    int clear();

private:
    void path_of(int32_t day, char (&out)[64]) const;
    int days(int32_t* out, int max) const;  // sorted newest first
    char dir_[40];
};

}  // namespace doser::history
