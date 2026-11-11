#include "lgal/entropy.h"

#include "bootloader_random.h"
#include "esp_log.h"

namespace lgal::entropy {

namespace {
constexpr const char* TAG = "entropy";
bool s_adc = false;  // SAR ADC noise source on

void adc(bool on) {
    if (on == s_adc) return;
    if (on)
        bootloader_random_enable();
    else
        bootloader_random_disable();
    s_adc = on;
    ESP_LOGD(TAG, "SAR ADC noise source %s", on ? "on" : "off");
}
}  // namespace

void init() { adc(true); }

void radio(bool on) { adc(!on); }

}  // namespace lgal::entropy
