#include "lgal/logbuf.h"

#include <cstdarg>
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace lgal::logbuf {

namespace {
char* s_buf = nullptr;
size_t s_cap = 0;
size_t s_head = 0;  // next write position
bool s_wrapped = false;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t s_orig = nullptr;

void append(const char* data, size_t len) {
    taskENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < len; ++i) {
        s_buf[s_head++] = data[i];
        if (s_head == s_cap) {
            s_head = 0;
            s_wrapped = true;
        }
    }
    taskEXIT_CRITICAL(&s_mux);
}

int hook(const char* fmt, va_list args) {
    char line[192];  // keep small: runs on the stack of the logging task
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0 && s_buf) append(line, static_cast<size_t>(n) < sizeof(line) ? n : sizeof(line) - 1);
    return s_orig ? s_orig(fmt, args) : n;
}
}  // namespace

void init(size_t capacity) {
    if (s_buf) return;
    s_buf = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_buf) s_buf = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT));
    if (!s_buf) return;
    s_cap = capacity;
    s_orig = esp_log_set_vprintf(hook);
}

std::string snapshot() {
    std::string out;
    if (!s_buf) return out;
    out.reserve(s_cap);
    taskENTER_CRITICAL(&s_mux);
    size_t head = s_head;
    bool wrapped = s_wrapped;
    taskEXIT_CRITICAL(&s_mux);
    // Copy outside the critical section; concurrent writes can at most
    // corrupt the oldest line.
    if (wrapped) out.append(s_buf + head, s_cap - head);
    out.append(s_buf, head);
    return out;
}

}  // namespace lgal::logbuf
