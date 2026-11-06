#include "doser/dosing_store.h"

#include <cinttypes>
#include <cstdio>
#include <type_traits>

#include "doser/i2c_devices.h"
#include "esp_log.h"
#include "nvs.h"

namespace doser::dosing_store {

namespace {
constexpr const char* TAG = "dose_store";
constexpr const char* kNs = "dose";
constexpr uint16_t kVersion = 1;
constexpr uint16_t kEepromBase = 0x000;
constexpr uint16_t kSlotSize = sizeof(dose::Record);

struct Slots {
    uint32_t seq = 0;  // last written sequence number
    int next = 0;      // next EEPROM slot
};
Slots s_slots[5];

template <typename T>
struct Blob {
    uint16_t version;
    T value;
};

template <typename T>
bool nvs_load(const char* key, T* out) {
    static_assert(std::is_trivially_copyable_v<T>);
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
    Blob<T> b{};
    size_t len = sizeof(b);
    const bool ok = nvs_get_blob(h, key, &b, &len) == ESP_OK && len == sizeof(b) && b.version == kVersion;
    nvs_close(h);
    if (ok) *out = b.value;
    return ok;
}

template <typename T>
esp_err_t nvs_save(const char* key, const T& v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    Blob<T> b{kVersion, v};
    err = nvs_set_blob(h, key, &b, sizeof(b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

uint16_t slot_addr(int pump, int slot) { return kEepromBase + (pump * 2 + slot) * kSlotSize; }

void key(char* buf, size_t len, char prefix, int pump) { snprintf(buf, len, "%c%d", prefix, pump); }
}  // namespace

void load_engineering(dose::Engineering* e) {
    dose::Engineering v;
    if (nvs_load("eng", &v) && dose::validate(v, 600) == nullptr) *e = v;
}

esp_err_t save_engineering(const dose::Engineering& e) { return nvs_save("eng", e); }

void load_settings(int pump, dose::PumpSettings* p) {
    char k[4];
    key(k, sizeof(k), 'p', pump);
    if (nvs_load(k, p)) p->name[sizeof(p->name) - 1] = 0;
}

esp_err_t save_settings(int pump, const dose::PumpSettings& p) {
    char k[4];
    key(k, sizeof(k), 'p', pump);
    return nvs_save(k, p);
}

bool load_state(int pump, dose::PumpState* s) {
    dose::Record r[3]{};  // EEPROM A, EEPROM B, NVS
    bool have[3] = {false, false, false};
    if (eeprom::present()) {
        for (int i = 0; i < 2; ++i)
            have[i] = eeprom::read(slot_addr(pump, i), &r[i], sizeof(r[i])) == ESP_OK && dose::valid(r[i]);
    }
    char k[4];
    key(k, sizeof(k), 's', pump);
    have[2] = nvs_load(k, &r[2]) && dose::valid(r[2]);

    int best = -1;
    for (int i = 0; i < 3; ++i) {
        if (!have[i]) continue;
        if (best < 0 || static_cast<int32_t>(r[i].seq - r[best].seq) > 0) best = i;
    }
    // Next EEPROM slot = the older or invalid one
    if (have[0] && have[1]) s_slots[pump].next = dose::newest(r[0], r[1]) == 0 ? 1 : 0;
    else s_slots[pump].next = have[0] ? 1 : 0;
    if (best < 0) return false;
    s_slots[pump].seq = r[best].seq;
    *s = r[best].state;
    ESP_LOGI(TAG, "Pump %d: state #%" PRIu32 " from %s", pump + 1, r[best].seq, best == 2 ? "NVS" : "EEPROM");
    return true;
}

esp_err_t save_state(int pump, const dose::PumpState& s) {
    Slots& sl = s_slots[pump];
    dose::Record r;
    dose::seal(r, sl.seq + 1, s);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (eeprom::present()) {
        err = eeprom::write(slot_addr(pump, sl.next), &r, sizeof(r));
        if (err == ESP_OK) {
            // Read back: only a confirmed record counts
            dose::Record check;
            err = eeprom::read(slot_addr(pump, sl.next), &check, sizeof(check));
            if (err == ESP_OK && !dose::valid(check)) err = ESP_ERR_INVALID_CRC;
        }
        if (err != ESP_OK) ESP_LOGW(TAG, "Pump %d: EEPROM write failed (%s), using NVS", pump + 1, esp_err_to_name(err));
    }
    if (err != ESP_OK) {
        char k[4];
        key(k, sizeof(k), 's', pump);
        err = nvs_save(k, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Pump %d: state not saved (%s)", pump + 1, esp_err_to_name(err));
            return err;
        }
    } else {
        sl.next ^= 1;
    }
    sl.seq = r.seq;
    return ESP_OK;
}

const char* state_backend() { return eeprom::present() ? "eeprom" : "nvs"; }

}  // namespace doser::dosing_store
