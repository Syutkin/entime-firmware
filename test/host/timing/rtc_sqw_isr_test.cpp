#include "../../../src/timing/rtc_sqw_isr.cpp"
#include "../support/gpio_fixture.h"

namespace {
constexpr int kPin = 7;
constexpr int64_t kStart = 10000000;

void read_edge() {
  int64_t timestamp = 0;
  uint32_t count = 0;
  require(rtc_sqw_get_raw(timestamp, count), "SQW edge must be available");
  expect(host::interrupt_depth == 0, "raw SQW read must restore interrupts");
}

void pulse(unsigned second) {
  fire_edge(kPin, kStart + static_cast<int64_t>(second) * 1000000);
  read_edge();
}

void lock_sqw() {
  rtc_sqw_begin(kPin, FALLING);
  for (unsigned second = 0; second < 4; ++second)
    pulse(second);
  require(rtc_sqw_is_locked(), "three stable SQW periods should lock");
}

void sqw_initially_has_no_signal() {
  rtc_sqw_begin(kPin, FALLING);
  int64_t timestamp = 0;
  uint32_t count = 0;
  expect(!rtc_sqw_get_raw(timestamp, count) && !rtc_sqw_is_locked(),
         "SQW should start without an edge or lock");
  expect(host::gpio_edges[kPin] == FALLING, "SQW must capture falling edges");
}

void sqw_captures_timestamp_and_counts_edges() {
  rtc_sqw_begin(kPin, FALLING);
  for (unsigned second = 0; second < 5; ++second) {
    pulse(second);
    int64_t timestamp = 0;
    uint32_t count = 0;
    expect(rtc_sqw_get_raw(timestamp, count), "SQW should have raw edge");
    expect(timestamp == kStart + static_cast<int64_t>(second) * 1000000 &&
               count == second + 1,
           "SQW raw sample must match the latest GPIO edge");
  }
}

void sqw_acquires_after_three_complete_periods() {
  rtc_sqw_begin(kPin, FALLING);
  for (unsigned second = 0; second < 3; ++second) {
    pulse(second);
    expect(!rtc_sqw_is_locked(), "SQW needs three complete periods");
  }
  pulse(3);
  expect(rtc_sqw_is_locked(), "fourth edge completes the third SQW period");
}

void sqw_polling_does_not_count_as_new_periods() {
  rtc_sqw_begin(kPin, FALLING);
  pulse(0);
  for (unsigned poll = 0; poll < 100; ++poll) {
    read_edge();
    expect(!rtc_sqw_is_locked(), "polling must not manufacture SQW periods");
  }
}

void sqw_loses_lock_at_timeout() {
  lock_sqw();
  host::clock.now_us = kStart + 3000000 + 1499999;
  expect(rtc_sqw_is_locked(), "SQW lock should survive before 1.5s");
  ++host::clock.now_us;
  expect(!rtc_sqw_is_locked(), "SQW lock should expire at 1.5s");
}

void sqw_reacquires_after_outage() {
  lock_sqw();
  host::clock.now_us = kStart + 5000000;
  expect(!rtc_sqw_is_locked(), "SQW outage must drop lock");
  for (unsigned second = 6; second < 9; ++second) {
    pulse(second);
    expect(!rtc_sqw_is_locked(), "returning SQW needs warmup");
  }
  pulse(9);
  expect(rtc_sqw_is_locked(), "SQW should relock after three good periods");
}

void sqw_rejects_fast_pulses_during_warmup() {
  rtc_sqw_begin(kPin, FALLING);
  for (unsigned i = 0; i < 10; ++i) {
    fire_edge(kPin, kStart + i * 100000);
    read_edge();
    expect(!rtc_sqw_is_locked(), "100ms pulses must not qualify as SQW");
  }
}

void sqw_accepts_period_jitter() {
  rtc_sqw_begin(kPin, FALLING);
  for (int64_t offset : {0, 950000, 2000000, 2990000}) {
    fire_edge(kPin, kStart + offset);
    read_edge();
  }
  expect(rtc_sqw_is_locked(), "nominal SQW period jitter should lock");
}

void sqw_stays_locked_during_long_stable_run() {
  lock_sqw();
  for (unsigned second = 4; second <= 3600; ++second) {
    pulse(second);
    expect(rtc_sqw_is_locked(), "stable SQW should remain locked for an hour");
  }
}

void sqw_reinitialization_clears_old_signal() {
  lock_sqw();
  rtc_sqw_begin(kPin, FALLING);
  int64_t timestamp = 0;
  uint32_t count = 0;
  expect(!rtc_sqw_get_raw(timestamp, count) && !rtc_sqw_is_locked(),
         "reinitialization must clear previous SQW timing and lock");
}

template <int64_t Period, bool Locks> void sqw_lock_period_boundary() {
  rtc_sqw_begin(kPin, FALLING);
  for (unsigned step = 0; step < 4; ++step) {
    fire_edge(kPin, kStart + step * Period);
    read_edge();
  }
  expect(rtc_sqw_is_locked() == Locks,
         "SQW warmup must accept 0.8..1.2s periods inclusive");
}

void sqw_raw_counter_recovers_after_wrap() {
  rtc_sqw_begin(kPin, FALLING);
  s_count = UINT32_MAX - 1;
  for (unsigned second = 0; second < 3; ++second)
    fire_edge(kPin, kStart + static_cast<int64_t>(second) * 1000000);
  int64_t timestamp = 0;
  uint32_t count = 0;
  expect(rtc_sqw_get_raw(timestamp, count) && count == 1 &&
             timestamp == kStart + 2000000,
         "SQW samples after wrapped zero should resume normally");
}

const Test kTests[] = {
    {"sqw_initially_has_no_signal", sqw_initially_has_no_signal},
    {"sqw_captures_timestamp_and_counts_edges",
     sqw_captures_timestamp_and_counts_edges},
    {"sqw_acquires_after_three_complete_periods",
     sqw_acquires_after_three_complete_periods},
    {"sqw_polling_does_not_count_as_new_periods",
     sqw_polling_does_not_count_as_new_periods},
    {"sqw_loses_lock_at_timeout", sqw_loses_lock_at_timeout},
    {"sqw_reacquires_after_outage", sqw_reacquires_after_outage},
    {"sqw_rejects_fast_pulses_during_warmup",
     sqw_rejects_fast_pulses_during_warmup},
    {"sqw_accepts_period_jitter", sqw_accepts_period_jitter},
    {"sqw_stays_locked_during_long_stable_run",
     sqw_stays_locked_during_long_stable_run},
    {"sqw_reinitialization_clears_old_signal",
     sqw_reinitialization_clears_old_signal},
    {"sqw_lock_period_at_800000us", sqw_lock_period_boundary<800000, true>},
    {"sqw_lock_period_below_800000us", sqw_lock_period_boundary<799999, false>},
    {"sqw_lock_period_at_1200000us", sqw_lock_period_boundary<1200000, true>},
    {"sqw_lock_period_above_1200000us",
     sqw_lock_period_boundary<1200001, false>},
    {"sqw_raw_counter_recovers_after_wrap",
     sqw_raw_counter_recovers_after_wrap},
};
} // namespace

int main(int argc, char **argv) {
  (void)TAG; // The production SQW module currently declares an unused log tag.
  return run_tests(argc, argv, kTests);
}
