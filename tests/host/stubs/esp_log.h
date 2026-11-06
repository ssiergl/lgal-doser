#pragma once
#include <cstdio>
extern bool g_log_verbose;
#define ESP_LOG_HOST(l, tag, fmt, ...) \
    do { if (g_log_verbose) std::printf("    [%c %s] " fmt "\n", l, tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGI(tag, fmt, ...) ESP_LOG_HOST('I', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ESP_LOG_HOST('W', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) ESP_LOG_HOST('E', tag, fmt, ##__VA_ARGS__)
