#include "pps_isr.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "esp_log.h"

static const char* TAG = "PPS";

// PPS timing
static volatile int64_t  s_pps_time_us = 0;
static volatile int64_t  s_last_pps_us = 0;
static volatile bool     s_pps_valid   = false;
static volatile uint32_t s_pps_count   = 0;

// Только для диагностики
static volatile uint32_t s_last_gps_utc_sec = 0;

// Гистерезис PPS lock:
// - lock только после нескольких подряд "хороших" PPS периодов,
// - loss только по увеличенному timeout (терпим одиночный пропуск PPS).
static constexpr int64_t kMinPeriodUs = 800000;     // 0.8s
static constexpr int64_t kMaxPeriodUs = 1200000;    // 1.2s
static constexpr uint8_t kLockAfterPulses = 3;      // сколько подряд PPS нужно для lock
static constexpr int64_t kLossTimeoutUs = 2800000;  // 2.8s (~пропуск 1 PPS не роняет lock)

static bool s_locked = false;
static uint32_t s_prev_count = 0;
static int64_t s_prev_pps_us = 0;
static uint8_t s_good_pulses = 0;

static void IRAM_ATTR pps_isr_handler() {
  int64_t now = esp_timer_get_time();
  s_pps_time_us = now;
  s_last_pps_us = now;
  s_pps_valid   = true;
  const uint32_t count = s_pps_count;
  s_pps_count = count + 1U;
}

void pps_init(int gpio_pin) {
  pinMode(gpio_pin, INPUT);
  attachInterrupt(gpio_pin, pps_isr_handler, RISING);
  ESP_LOGI(TAG, "Initialized on GPIO %d (RISING edge)", gpio_pin);
}

void pps_set_gps_utc_second(uint32_t gps_utc_sec) {
  s_last_gps_utc_sec = gps_utc_sec;
  ESP_LOGV(TAG, "GPS UTC second set to %lu", (unsigned long)gps_utc_sec);
}

uint32_t pps_get_last_gps_utc_second() {
  return s_last_gps_utc_sec;
}

bool pps_is_locked() {
  int64_t now = esp_timer_get_time();

  int64_t last_pps_us = 0;
  uint32_t count = 0;
  bool valid = false;
  noInterrupts();
  last_pps_us = s_last_pps_us;
  count = s_pps_count;
  valid = s_pps_valid;
  interrupts();

  if (count != s_prev_count) {
    if (s_prev_pps_us == 0) {
      s_good_pulses = 1;
    } else {
      int64_t period = last_pps_us - s_prev_pps_us;
      if (period >= kMinPeriodUs && period <= kMaxPeriodUs) {
        if (s_good_pulses < 255) s_good_pulses++;
      } else {
        // Начинаем новую "полосу" от текущего импульса.
        s_good_pulses = 1;
      }
    }
    s_prev_pps_us = last_pps_us;
    s_prev_count = count;
  }

  bool locked = s_locked;
  if (!locked && valid && s_good_pulses >= kLockAfterPulses) {
    locked = true;
  }

  if (locked) {
    bool lost = !valid || ((now - last_pps_us) >= kLossTimeoutUs);
    if (lost) locked = false;
  }

  if (locked != s_locked) {
    if (locked) {
      ESP_LOGI(TAG, "PPS lock acquired");
    } else if (valid) {
      int64_t age_ms = (now - last_pps_us) / 1000;
      ESP_LOGW(TAG, "PPS lock lost (last PPS %lld ms ago)", (long long)age_ms);
    } else {
      ESP_LOGW(TAG, "PPS lock lost (PPS invalid)");
    }
    s_locked = locked;
  }

  return s_locked;
}

bool pps_get_raw(int64_t &pps_time_us, uint32_t &pps_count) {
  if (!s_pps_valid) return false;

  noInterrupts();
  pps_time_us = s_pps_time_us;
  pps_count   = s_pps_count;
  interrupts();

  return true;
}
