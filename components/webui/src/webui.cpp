#include "webui/webui.h"

#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "lgal/http.h"
#include "lgal/sysapi.h"

extern const uint8_t webui_bin_start[] asm("_binary_webui_bin_start");
extern const uint8_t webui_bin_end[] asm("_binary_webui_bin_end");

namespace webui {

namespace {
namespace http = lgal::http;
constexpr const char* TAG = "webui";
constexpr int kMaxFiles = 64;

struct File {
    char path[96];
    char mime[40];
    const uint8_t* data;
    uint32_t len;
    char etag[19];  // "\"" + 16 hex + "\""
};

File s_files[kMaxFiles];
int s_count = 0;

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

// Reads the table of the packed blob (pack.py); false if it is malformed
bool load() {
    const uint8_t* p = webui_bin_start;
    const size_t size = size_t(webui_bin_end - webui_bin_start);
    if (size < 8 || std::memcmp(p, "LGWU", 4) != 0 || rd16(p + 4) != 1) return false;
    const int n = rd16(p + 6);
    if (n > kMaxFiles) return false;
    size_t o = 8;
    for (int i = 0; i < n; ++i) {
        File& f = s_files[i];
        if (o + 1 > size) return false;
        const size_t pl = p[o++];
        if (pl >= sizeof(f.path) || o + pl + 1 > size) return false;
        std::memcpy(f.path, p + o, pl);
        f.path[pl] = 0;
        o += pl;
        const size_t ml = p[o++];
        if (ml >= sizeof(f.mime) || o + ml + 24 > size) return false;
        std::memcpy(f.mime, p + o, ml);
        f.mime[ml] = 0;
        o += ml;
        const uint32_t off = rd32(p + o), len = rd32(p + o + 4);
        if (off > size || len > size - off) return false;
        f.data = p + off;
        f.len = len;
        f.etag[0] = '"';
        std::memcpy(f.etag + 1, p + o + 8, 16);
        f.etag[17] = '"';
        f.etag[18] = 0;
        o += 24;
    }
    s_count = n;
    return true;
}

const File* find(const char* path, size_t len) {
    for (int i = 0; i < s_count; ++i)
        if (std::strlen(s_files[i].path) == len && std::memcmp(s_files[i].path, path, len) == 0) return &s_files[i];
    return nullptr;
}

esp_err_t send_file(httpd_req_t* req, const File& f) {
    char inm[24];
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "ETag", f.etag);
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && std::strcmp(inm, f.etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, nullptr, 0);
    }
    httpd_resp_set_type(req, f.mime);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, reinterpret_cast<const char*>(f.data), f.len);
}

esp_err_t get_asset(httpd_req_t* req) {
    const char* path = req->uri + 4;  // after "/ui/"
    size_t len = std::strcspn(path, "?#");
    const File* f = find(path, len);
    if (!f || std::strcmp(f->path, "index.html") == 0)
        return http::send_error(req, "404 Not Found", "not_found", "No such file");
    return send_file(req, *f);
}

// "/" after the setup check and login (sysapi)
esp_err_t send_home(httpd_req_t* req) {
    const File* f = find("index.html", 10);
    return f ? send_file(req, *f) : http::send_error(req, "500 Internal Server Error", "webui_missing", "No web UI");
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    if (!load()) {
        ESP_LOGE(TAG, "Packed web UI is invalid");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "%d files, %u bytes", s_count, unsigned(webui_bin_end - webui_bin_start));
    http::route(server, "/ui/*", HTTP_GET, get_asset);
    lgal::sysapi::set_home_page(send_home);
    return ESP_OK;
}

}  // namespace webui
