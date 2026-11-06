#include "doser/tmc2209.h"

#include <cinttypes>
#include <cstring>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_private/gpio.h"
#include "esp_rom_sys.h"
#include "hal/uart_periph.h"

namespace doser {

namespace {
constexpr const char* TAG = "tmc2209";
constexpr int kRetries = 3;
}  // namespace

esp_err_t TmcBus::init(uart_port_t port, gpio_num_t pin, uint32_t baud) {
    port_ = port;
    pin_ = pin;
    byte_us_ = (10u * 1000000u + baud - 1) / baud;
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return ESP_ERR_NO_MEM;

    uart_config_t cfg{};
    cfg.baud_rate = static_cast<int>(baud);
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    ESP_RETURN_ON_ERROR(uart_driver_install(port, 256, 0, 0, nullptr, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(port, &cfg), TAG, "uart config");
    // TX and RX on the same pin: IDF connects both via the GPIO matrix
    ESP_RETURN_ON_ERROR(uart_set_pin(port, pin, pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart pin");
    gpio_input_enable(pin);
    gpio_pullup_en(pin);  // defined idle level while the pin is released
    // Move received bytes into the buffer quickly (timeout 2 characters instead of 10)
    uart_set_rx_timeout(port, 2);
    drive();
    ESP_LOGI(TAG, "UART%d an GPIO%d (Eindraht, %" PRIu32 " Bd)", port, pin, baud);
    return ESP_OK;
}

void TmcBus::drive() {
    gpio_matrix_output(pin_, UART_PERIPH_SIGNAL(port_, SOC_UART_PERIPH_SIGNAL_TX), false, false);
}

void TmcBus::release() { gpio_output_disable(pin_); }

esp_err_t TmcBus::transfer(const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
    if (!mutex_) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    ++stats_.transfers;
    esp_err_t err = ESP_OK;

    uart_flush_input(port_);
    uart_write_bytes(port_, tx, tx_len);
    // Waits until the stop bit of the last byte is out. The driver
    // only replies after SENDDELAY (24 bit times ≈ 200 µs).
    uart_wait_tx_done(port_, pdMS_TO_TICKS(20));
    if (rx_len) release();

    uint8_t echo[8];
    const TickType_t echo_to = pdMS_TO_TICKS(3 + tx_len * byte_us_ / 1000);
    int got = uart_read_bytes(port_, echo, tx_len, echo_to);
    if (got != static_cast<int>(tx_len) || std::memcmp(echo, tx, tx_len) != 0) {
        ++stats_.echo_errors;
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (rx_len) {
        const TickType_t rx_to = pdMS_TO_TICKS(4 + rx_len * byte_us_ / 1000);
        got = uart_read_bytes(port_, rx, rx_len, rx_to);
        if (got != static_cast<int>(rx_len)) {
            ++stats_.timeouts;
            err = ESP_ERR_TIMEOUT;
        }
    }
    if (rx_len) drive();
    // Pause between datagrams (driver resyncs on the sync nibble)
    esp_rom_delay_us(4 * byte_us_);
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t TmcBus::write(uint8_t addr, uint8_t reg, uint32_t value) {
    uint8_t buf[8];
    tmc::build_write(buf, addr, reg, value);
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < kRetries && err != ESP_OK; ++i) err = transfer(buf, sizeof(buf), nullptr, 0);
    return err;
}

esp_err_t TmcBus::read(uint8_t addr, uint8_t reg, uint32_t* value) {
    uint8_t req[4];
    uint8_t reply[8];
    tmc::build_read(req, addr, reg);
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < kRetries; ++i) {
        err = transfer(req, sizeof(req), reply, sizeof(reply));
        if (err == ESP_ERR_TIMEOUT) break;  // driver does not reply -> do not wait 3 times
        if (err != ESP_OK) continue;
        if (tmc::parse_reply(reply, reg, value)) return ESP_OK;
        ++stats_.crc_errors;
        err = ESP_ERR_INVALID_CRC;
    }
    return err;
}

esp_err_t Tmc2209::write(uint8_t reg, uint32_t value) {
    return bus_ ? bus_->write(addr_, reg, value) : ESP_ERR_INVALID_STATE;
}

esp_err_t Tmc2209::read(uint8_t reg, uint32_t* value) {
    return bus_ ? bus_->read(addr_, reg, value) : ESP_ERR_INVALID_STATE;
}

esp_err_t Tmc2209::probe(uint8_t* version) {
    // Set the reply delay first so that switching to receive
    // is surely done before the reply.
    ESP_RETURN_ON_ERROR(write(tmc::reg::NODECONF, uint32_t(tmc::kSendDelay) << 8), TAG, "nodeconf");
    uint32_t ioin = 0;
    esp_err_t err = read(tmc::reg::IOIN, &ioin);
    if (err != ESP_OK) return err;
    const uint8_t v = static_cast<uint8_t>(ioin >> 24);
    if (version) *version = v;
    return v == tmc::kVersion ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t Tmc2209::apply(const tmc::DriverRegs& r) {
    uint32_t before = 0;
    ESP_RETURN_ON_ERROR(read(tmc::reg::IFCNT, &before), TAG, "ifcnt");

    const struct {
        uint8_t reg;
        uint32_t val;
    } seq[] = {
        {tmc::reg::GCONF, r.gconf},
        {tmc::reg::NODECONF, uint32_t(tmc::kSendDelay) << 8},
        {tmc::reg::CHOPCONF, r.chopconf},
        {tmc::reg::PWMCONF, r.pwmconf},
        {tmc::reg::IHOLD_IRUN, r.ihold_irun},
        {tmc::reg::TPOWERDOWN, r.tpowerdown},
        {tmc::reg::TPWMTHRS, r.tpwmthrs},
        {tmc::reg::TCOOLTHRS, r.tcoolthrs},
        {tmc::reg::SGTHRS, 0},
        {tmc::reg::COOLCONF, 0},
        {tmc::reg::VACTUAL, 0},  // steps come via STEP, not from the internal clock
        {tmc::reg::GSTAT, tmc::GSTAT_RESET | tmc::GSTAT_DRV_ERR | tmc::GSTAT_UV_CP},  // clear flags
    };
    for (const auto& s : seq) ESP_RETURN_ON_ERROR(write(s.reg, s.val), TAG, "write 0x%02x", s.reg);

    uint32_t after = 0;
    ESP_RETURN_ON_ERROR(read(tmc::reg::IFCNT, &after), TAG, "ifcnt");
    const uint8_t delta = static_cast<uint8_t>(after - before);
    if (delta != sizeof(seq) / sizeof(seq[0])) {
        ESP_LOGW(TAG, "Adresse %u: %u von %u Schreibzugriffen bestätigt", addr_, delta,
                 unsigned(sizeof(seq) / sizeof(seq[0])));
        return ESP_ERR_INVALID_RESPONSE;
    }
    // spot check: read back GCONF
    uint32_t gconf = 0;
    ESP_RETURN_ON_ERROR(read(tmc::reg::GCONF, &gconf), TAG, "gconf");
    if ((gconf & 0x3FF) != (r.gconf & 0x3FF)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

}  // namespace doser
