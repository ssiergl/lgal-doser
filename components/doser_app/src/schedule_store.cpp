#include "doser/schedule_store.h"

#include <cinttypes>
#include <cstdio>

#include "doser/i2c_devices.h"
#include "doser/nvs_blob.h"
#include "esp_log.h"

namespace doser::schedule_store {

namespace {
constexpr const char* TAG = "sched_store";
constexpr const char* kNs = "sched";
constexpr uint16_t kVersion = 1;
constexpr uint16_t kEepromBase = 0x640;  // behind the dosing records (0x000..0x63F)
constexpr uint16_t kSlotSize = 96;       // 3 EEPROM pages per record
static_assert(sizeof(sched::RunRecord) <= kSlotSize);
static_assert(kEepromBase + sched::kPumps * 2 * kSlotSize <= 0xFE0, "collides with the EEPROM self test");

struct Slots {
    uint32_t seq = 0;  // last written sequence number
    int next = 0;      // next EEPROM slot
};
Slots s_slots[sched::kPumps];

uint16_t slot_addr(int pump, int slot) { return kEepromBase + (pump * 2 + slot) * kSlotSize; }

void key(char* buf, size_t len, char prefix, int pump) { snprintf(buf, len, "%c%d", prefix, pump); }
}  // namespace

void load_config(sched::Config* c) {
    sched::Config v;
    if (nvs_blob::load(kNs, "cfg", kVersion, &v) && sched::validate(v) == nullptr) *c = v;
}

esp_err_t save_config(const sched::Config& c) { return nvs_blob::save(kNs, "cfg", kVersion, c); }

void load_locks(sched::Locks* l) {
    sched::Locks v;
    if (nvs_blob::load(kNs, "locks", kVersion, &v) && v.count <= sched::kMaxLocks) *l = v;
}

esp_err_t save_locks(const sched::Locks& l) { return nvs_blob::save(kNs, "locks", kVersion, l); }

void load_schedule(int pump, sched::PumpSchedule* s) {
    char k[4];
    key(k, sizeof(k), 'p', pump);
    sched::PumpSchedule v;
    if (nvs_blob::load(kNs, k, kVersion, &v) && v.count <= sched::kMaxPrograms) *s = v;
}

esp_err_t save_schedule(int pump, const sched::PumpSchedule& s) {
    char k[4];
    key(k, sizeof(k), 'p', pump);
    return nvs_blob::save(kNs, k, kVersion, s);
}

bool load_run(int pump, sched::RunState* s) {
    sched::RunRecord r[3]{};  // EEPROM A, EEPROM B, NVS
    bool have[3] = {false, false, false};
    if (eeprom::present()) {
        for (int i = 0; i < 2; ++i)
            have[i] = eeprom::read(slot_addr(pump, i), &r[i], sizeof(r[i])) == ESP_OK && sched::valid(r[i]);
    }
    char k[4];
    key(k, sizeof(k), 'r', pump);
    have[2] = nvs_blob::load(kNs, k, kVersion, &r[2]) && sched::valid(r[2]);

    int best = -1;
    for (int i = 0; i < 3; ++i) {
        if (!have[i]) continue;
        if (best < 0 || static_cast<int32_t>(r[i].seq - r[best].seq) > 0) best = i;
    }
    // Next EEPROM slot = the older or invalid one
    if (have[0] && have[1]) s_slots[pump].next = sched::newest(r[0], r[1]) == 0 ? 1 : 0;
    else s_slots[pump].next = have[0] ? 1 : 0;
    if (best < 0) return false;
    s_slots[pump].seq = r[best].seq;
    *s = r[best].state;
    ESP_LOGI(TAG, "Pump %d: run state #%" PRIu32 " from %s", pump + 1, r[best].seq, best == 2 ? "NVS" : "EEPROM");
    return true;
}

esp_err_t save_run(int pump, const sched::RunState& s) {
    Slots& sl = s_slots[pump];
    sched::RunRecord r;
    sched::seal(r, sl.seq + 1, s);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (eeprom::present()) {
        err = eeprom::write(slot_addr(pump, sl.next), &r, sizeof(r));
        if (err == ESP_OK) {
            // Read back: only a confirmed record counts
            sched::RunRecord check;
            err = eeprom::read(slot_addr(pump, sl.next), &check, sizeof(check));
            if (err == ESP_OK && !sched::valid(check)) err = ESP_ERR_INVALID_CRC;
        }
        if (err != ESP_OK) ESP_LOGW(TAG, "Pump %d: EEPROM write failed (%s), using NVS", pump + 1, esp_err_to_name(err));
    }
    if (err != ESP_OK) {
        char k[4];
        key(k, sizeof(k), 'r', pump);
        err = nvs_blob::save(kNs, k, kVersion, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Pump %d: run state not saved (%s)", pump + 1, esp_err_to_name(err));
            return err;
        }
    } else {
        sl.next ^= 1;
    }
    sl.seq = r.seq;
    return ESP_OK;
}

}  // namespace doser::schedule_store
