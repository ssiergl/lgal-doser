#pragma once
// Versioned NVS blobs for trivially copyable structs (used by the stores).
// A blob with a different size or version is ignored (defaults apply).

#include <cstdint>
#include <type_traits>

#include "nvs.h"

namespace doser::nvs_blob {

template <typename T>
struct Blob {
    uint16_t version;
    T value;
};

template <typename T>
bool load(const char* ns, const char* key, uint16_t version, T* out) {
    static_assert(std::is_trivially_copyable_v<T>);
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    Blob<T> b{};
    size_t len = sizeof(b);
    const bool ok = nvs_get_blob(h, key, &b, &len) == ESP_OK && len == sizeof(b) && b.version == version;
    nvs_close(h);
    if (ok) *out = b.value;
    return ok;
}

template <typename T>
esp_err_t save(const char* ns, const char* key, uint16_t version, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    Blob<T> b{version, v};
    err = nvs_set_blob(h, key, &b, sizeof(b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

}  // namespace doser::nvs_blob
