#include "doser/stepgen.h"

#include "doser/pins.h"
#include "driver/gptimer.h"
#include "driver/mcpwm_prelude.h"
#include "driver/pulse_cnt.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "hal/mcpwm_ll.h"
#include "sdkconfig.h"

// Without these options step clocks stall during flash writes
// (NVS, OTA, log) or the ISR crashes. After changing
// sdkconfig.defaults: "rm sdkconfig" and rebuild.
#if !CONFIG_RMT_TX_ISR_CACHE_SAFE || !CONFIG_PCNT_ISR_IRAM_SAFE || !CONFIG_PCNT_CTRL_FUNC_IN_IRAM || \
    !CONFIG_GPTIMER_ISR_CACHE_SAFE || !CONFIG_MCPWM_CTRL_FUNC_IN_IRAM
#error "IRAM-Optionen für RMT/PCNT/GPTimer/MCPWM fehlen – sdkconfig löschen und neu bauen (siehe sdkconfig.defaults)"
#endif

namespace doser::step {

namespace {
constexpr const char* TAG = "stepgen";

// ---------------------------------------------------------------------------
// RMT (pumps 1-4)
// ---------------------------------------------------------------------------
class RmtChannel final : public Channel {
public:
    esp_err_t init(gpio_num_t pin) {
        rmt_tx_channel_config_t cfg{};
        cfg.gpio_num = pin;
        cfg.clk_src = RMT_CLK_SRC_DEFAULT;
        cfg.resolution_hz = 1000000;  // 1 tick = 1 µs (matches Ramp)
        cfg.mem_block_symbols = 48;   // 4 channels share 192 symbols
        cfg.trans_queue_depth = 1;
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&cfg, &ch_), TAG, "rmt channel");

        rmt_simple_encoder_config_t enc{};
        enc.callback = &RmtChannel::encode;
        enc.arg = this;
        enc.min_chunk_size = 2;
        ESP_RETURN_ON_ERROR(rmt_new_simple_encoder(&enc, &enc_), TAG, "rmt encoder");

        rmt_tx_event_callbacks_t cbs{};
        cbs.on_trans_done = &RmtChannel::on_done;
        ESP_RETURN_ON_ERROR(rmt_tx_register_event_callbacks(ch_, &cbs, this), TAG, "rmt cb");
        return rmt_enable(ch_);
    }

    esp_err_t start(const RampParams& move) override {
        if (busy_) return ESP_ERR_INVALID_STATE;
        ramp_.start(move);
        pending_low_ = 0;
        if (ramp_.done()) return ESP_OK;
        busy_ = true;
        rmt_transmit_config_t tc{};
        tc.loop_count = 0;
        tc.flags.eot_level = 0;
        esp_err_t err = rmt_transmit(ch_, enc_, &payload_, sizeof(payload_), &tc);
        if (err != ESP_OK) {
            ramp_.finish();
            busy_ = false;
        }
        return err;
    }

    void stop(bool immediate) override {
        if (!busy_) return;
        if (!immediate) {
            ramp_.request_stop();
            return;
        }
        rmt_disable(ch_);  // aborts the running transmission
        ramp_.finish();
        busy_ = false;
        rmt_enable(ch_);
    }

    bool busy() const override { return busy_; }
    uint32_t position() const override { return ramp_.position(); }
    uint32_t target() const override { return ramp_.target(); }
    uint32_t velocity() const override { return busy_ ? ramp_.velocity() : 0; }
    const char* kind() const override { return "rmt"; }

private:
    static rmt_symbol_word_t sym(uint32_t d0, uint32_t l0, uint32_t d1, uint32_t l1) {
        rmt_symbol_word_t s;
        s.duration0 = d0;
        s.level0 = l0;
        s.duration1 = d1;
        s.level1 = l1;
        return s;
    }

    // Runs in the RMT interrupt: one symbol per step (2 µs HIGH, rest LOW).
    // LOW phases > 32767 µs (below ~30 steps/s) are split; a
    // duration of 0 would mean end of transmission and never occurs.
    static size_t IRAM_ATTR encode(const void*, size_t, size_t, size_t symbols_free, rmt_symbol_word_t* symbols,
                                   bool* done, void* arg) {
        auto* self = static_cast<RmtChannel*>(arg);
        size_t n = 0;
        while (n < symbols_free) {
            if (self->pending_low_) {
                uint32_t a, b;
                if (self->pending_low_ <= 65534) {
                    a = self->pending_low_ / 2;
                    b = self->pending_low_ - a;
                    self->pending_low_ = 0;
                } else {
                    a = b = 16384;
                    self->pending_low_ -= 32768;
                }
                symbols[n++] = sym(a, 0, b, 0);
                continue;
            }
            const uint32_t iv = self->ramp_.next_interval_us();
            if (iv == 0) {
                *done = true;
                break;
            }
            const uint32_t low = iv - Ramp::kPulseUs;
            if (low <= 32767) {
                symbols[n++] = sym(Ramp::kPulseUs, 1, low, 0);
            } else {
                symbols[n++] = sym(Ramp::kPulseUs, 1, 16000, 0);
                self->pending_low_ = low - 16000;
            }
        }
        return n;
    }

    static bool IRAM_ATTR on_done(rmt_channel_handle_t, const rmt_tx_done_event_data_t*, void* arg) {
        static_cast<RmtChannel*>(arg)->busy_ = false;
        return false;
    }

    rmt_channel_handle_t ch_ = nullptr;
    rmt_encoder_handle_t enc_ = nullptr;
    Ramp ramp_;
    uint32_t pending_low_ = 0;
    volatile bool busy_ = false;
    uint8_t payload_ = 0;  // rmt_transmit requires payload; the ramp is the state
};

// ---------------------------------------------------------------------------
// MCPWM + PCNT (pump 5)
// ---------------------------------------------------------------------------
class McpwmChannel final : public Channel {
public:
    static constexpr int kPcntHigh = 30000;
    static constexpr uint32_t kTimerHz = 5000000;
    static constexpr uint32_t kPulseTicks = Ramp::kPulseUs * (kTimerHz / 1000000);
    static constexpr uint32_t kTickUs = 1000;
    // Only MCPWM user: group 0, first timer -> timer ID 0 (for the
    // stop in the PCNT interrupt via the LL function, which is IRAM-safe).
    static constexpr int kGroup = 0;
    static constexpr int kTimerId = 0;

    esp_err_t init(gpio_num_t pin) {
        mcpwm_timer_config_t tc{};
        tc.group_id = kGroup;
        tc.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
        tc.resolution_hz = kTimerHz;
        tc.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
        tc.period_ticks = 50000;
        tc.flags.update_period_on_empty = 1;
        ESP_RETURN_ON_ERROR(mcpwm_new_timer(&tc, &timer_), TAG, "mcpwm timer");

        mcpwm_operator_config_t oc{};
        oc.group_id = kGroup;
        ESP_RETURN_ON_ERROR(mcpwm_new_operator(&oc, &oper_), TAG, "mcpwm oper");
        ESP_RETURN_ON_ERROR(mcpwm_operator_connect_timer(oper_, timer_), TAG, "mcpwm connect");

        mcpwm_comparator_config_t cc{};
        cc.flags.update_cmp_on_tez = 1;
        ESP_RETURN_ON_ERROR(mcpwm_new_comparator(oper_, &cc, &cmp_), TAG, "mcpwm cmp");
        ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(cmp_, kPulseTicks), TAG, "mcpwm cmp val");

        mcpwm_generator_config_t gc{};
        gc.gen_gpio_num = pin;
        ESP_RETURN_ON_ERROR(mcpwm_new_generator(oper_, &gc, &gen_), TAG, "mcpwm gen");
        // Rising edge at counter value 0 (= step), falling after 2 µs
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_timer_event(
                                gen_, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY,
                                                                   MCPWM_GEN_ACTION_HIGH)),
                            TAG, "gen tez");
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_compare_event(
                                gen_, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, cmp_, MCPWM_GEN_ACTION_LOW)),
                            TAG, "gen cmp");
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(gen_, 0, true), TAG, "gen force");
        ESP_RETURN_ON_ERROR(mcpwm_timer_enable(timer_), TAG, "timer enable");

        // PCNT counts the rising edges of the same pin (input via GPIO matrix)
        pcnt_unit_config_t uc{};
        uc.low_limit = -1;
        uc.high_limit = kPcntHigh;
        ESP_RETURN_ON_ERROR(pcnt_new_unit(&uc, &unit_), TAG, "pcnt unit");
        pcnt_chan_config_t chc{};
        chc.edge_gpio_num = pin;
        chc.level_gpio_num = -1;
        ESP_RETURN_ON_ERROR(pcnt_new_channel(unit_, &chc, &chan_), TAG, "pcnt chan");
        ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                         PCNT_CHANNEL_EDGE_ACTION_HOLD),
                            TAG, "pcnt edge");
        ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(unit_, kPcntHigh), TAG, "pcnt wp");
        pcnt_event_callbacks_t pc{};
        pc.on_reach = &McpwmChannel::on_reach;
        ESP_RETURN_ON_ERROR(pcnt_unit_register_event_callbacks(unit_, &pc, this), TAG, "pcnt cb");
        ESP_RETURN_ON_ERROR(pcnt_unit_enable(unit_), TAG, "pcnt enable");
        ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(unit_), TAG, "pcnt clear");
        ESP_RETURN_ON_ERROR(pcnt_unit_start(unit_), TAG, "pcnt start");

        // 1 kHz control clock for the ramp
        gptimer_config_t gt{};
        gt.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        gt.direction = GPTIMER_COUNT_UP;
        gt.resolution_hz = 1000000;
        ESP_RETURN_ON_ERROR(gptimer_new_timer(&gt, &tick_), TAG, "gptimer");
        gptimer_event_callbacks_t gcb{};
        gcb.on_alarm = &McpwmChannel::on_tick;
        ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(tick_, &gcb, this), TAG, "gptimer cb");
        gptimer_alarm_config_t al{};
        al.alarm_count = kTickUs;
        al.reload_count = 0;
        al.flags.auto_reload_on_alarm = 1;
        ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(tick_, &al), TAG, "gptimer alarm");
        ESP_RETURN_ON_ERROR(gptimer_enable(tick_), TAG, "gptimer enable");
        return gptimer_start(tick_);
    }

    esp_err_t start(const RampParams& move) override {
        if (busy_) return ESP_ERR_INVALID_STATE;
        ramp_.start(move);
        if (ramp_.done()) return ESP_OK;

        q_ = move.steps / kPcntHigh;
        r_ = static_cast<int>(move.steps % kPcntHigh);
        overflows_ = 0;
        last_pos_ = 0;
        if (thr_) {
            pcnt_unit_remove_watch_point(unit_, thr_);
            thr_ = 0;
        }
        if (r_) {
            ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(unit_, r_), TAG, "pcnt thr");
            thr_ = r_;
        }
        ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(unit_), TAG, "pcnt clear");

        v_ = ramp_.velocity();
        mcpwm_timer_set_period(timer_, period_for(v_));
        busy_ = true;
        mcpwm_generator_set_force_level(gen_, -1, true);  // enable output
        esp_err_t err = mcpwm_timer_start_stop(timer_, MCPWM_TIMER_START_NO_STOP);
        if (err != ESP_OK) {
            ramp_.finish();
            busy_ = false;
        }
        return err;
    }

    void stop(bool immediate) override {
        if (!busy_) return;
        if (!immediate) {
            ramp_.request_stop();
            return;
        }
        mcpwm_timer_start_stop(timer_, MCPWM_TIMER_STOP_FULL);
        ramp_.finish();
        busy_ = false;
        v_ = 0;
    }

    bool busy() const override { return busy_; }
    uint32_t position() const override { return count(); }
    uint32_t target() const override { return ramp_.target(); }
    uint32_t velocity() const override { return busy_ ? v_ : 0; }
    const char* kind() const override { return "mcpwm"; }

private:
    static uint32_t IRAM_ATTR period_for(uint32_t v) {
        uint32_t p = v ? kTimerHz / v : 65535;
        if (p > 65535) p = 65535;  // slowest frequency ~77 steps/s
        if (p < kPulseTicks * 3) p = kPulseTicks * 3;
        return p;
    }

    uint32_t IRAM_ATTR count() const {
        int c = 0;
        pcnt_unit_get_count(unit_, &c);
        uint32_t p = overflows_ * kPcntHigh + static_cast<uint32_t>(c);
        // counter already overflowed, but the overflow interrupt has not run yet
        if (p < last_pos_) p += kPcntHigh;
        return p;
    }

    // Stops when the timer maximum is reached, i.e. before the next
    // rising edge (which only comes at counter value 0).
    void IRAM_ATTR finish_isr() {
        mcpwm_ll_timer_set_start_stop_command(MCPWM_LL_GET_HW(kGroup), kTimerId, MCPWM_TIMER_STOP_FULL);
        ramp_.finish();
        busy_ = false;
        v_ = 0;
    }

    static bool IRAM_ATTR on_reach(pcnt_unit_handle_t, const pcnt_watch_event_data_t* ev, void* arg) {
        auto* self = static_cast<McpwmChannel*>(arg);
        if (ev->watch_point_value == kPcntHigh) {
            const uint32_t o = self->overflows_ + 1;
            self->overflows_ = o;
            if (self->busy_ && self->r_ == 0 && o == self->q_) self->finish_isr();
        } else if (self->busy_ && ev->watch_point_value == self->r_ && self->overflows_ == self->q_) {
            self->finish_isr();
        }
        return false;
    }

    static bool IRAM_ATTR on_tick(gptimer_handle_t, const gptimer_alarm_event_data_t*, void* arg) {
        auto* self = static_cast<McpwmChannel*>(arg);
        if (!self->busy_) return false;
        const uint32_t pos = self->count();
        self->last_pos_ = pos;
        const uint32_t v = self->ramp_.advance(kTickUs, pos);
        if (v == 0) {  // soft stop completed
            self->finish_isr();
            return false;
        }
        if (v != self->v_) {
            self->v_ = v;
            mcpwm_timer_set_period(self->timer_, period_for(v));
        }
        return false;
    }

    mcpwm_timer_handle_t timer_ = nullptr;
    mcpwm_oper_handle_t oper_ = nullptr;
    mcpwm_cmpr_handle_t cmp_ = nullptr;
    mcpwm_gen_handle_t gen_ = nullptr;
    pcnt_unit_handle_t unit_ = nullptr;
    pcnt_channel_handle_t chan_ = nullptr;
    gptimer_handle_t tick_ = nullptr;
    Ramp ramp_;
    volatile uint32_t overflows_ = 0;
    volatile uint32_t last_pos_ = 0;
    volatile uint32_t v_ = 0;
    uint32_t q_ = 0;
    int r_ = 0;
    int thr_ = 0;
    volatile bool busy_ = false;
};

RmtChannel s_rmt[4];
McpwmChannel s_mcpwm;
Channel* s_channels[pins::kPumpCount] = {&s_rmt[0], &s_rmt[1], &s_rmt[2], &s_rmt[3], &s_mcpwm};
bool s_ready = false;
}  // namespace

esp_err_t init() {
    if (s_ready) return ESP_OK;
    for (int i = 0; i < 4; ++i) {
        ESP_RETURN_ON_ERROR(s_rmt[i].init(pins::kStep[i]), TAG, "Pumpe %d (RMT)", i + 1);
    }
    ESP_RETURN_ON_ERROR(s_mcpwm.init(pins::kStep[4]), TAG, "Pumpe 5 (MCPWM/PCNT)");
    s_ready = true;
    ESP_LOGI(TAG, "Schritt-Takte bereit: Pumpe 1-4 RMT, Pumpe 5 MCPWM+PCNT");
    return ESP_OK;
}

Channel* channel(int pump_index) {
    if (!s_ready || pump_index < 0 || pump_index >= pins::kPumpCount) return nullptr;
    return s_channels[pump_index];
}

}  // namespace doser::step
