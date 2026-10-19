#include "../../../src/timing/pps_isr.cpp"
#include "../support/gpio_fixture.h"

namespace {
constexpr int kPin = 6;
constexpr int64_t kStart = 10000000;

void pulse(unsigned second) {
  fire_edge(kPin, kStart + static_cast<int64_t>(second) * 1000000);
}

void lock_pps() {
  pps_init(kPin);
  for (unsigned second = 0; second < 3; ++second) {
    pulse(second);
    pps_is_locked();
  }
  require(pps_is_locked(), "three PPS pulses should establish lock");
}

void pps_initially_has_no_signal() {
  pps_init(kPin);
  int64_t timestamp = 0;
  uint32_t count = 0;
  expect(!pps_get_raw(timestamp, count) && !pps_is_locked(),
         "PPS must start without an edge or lock");
  expect(host::gpio_modes[kPin] == INPUT && host::gpio_edges[kPin] == RISING,
         "PPS input should capture rising edges");
}

void pps_captures_timestamp_and_counts_edges() {
  pps_init(kPin);
  for (unsigned second = 0; second < 5; ++second) {
    pulse(second);
    int64_t timestamp = 0;
    uint32_t count = 0;
    expect(pps_get_raw(timestamp, count), "raw PPS must be available");
    expect(timestamp == kStart + static_cast<int64_t>(second) * 1000000 &&
               count == second + 1,
           "raw PPS must match the latest captured GPIO edge");
  }
  expect(host::interrupt_depth == 0, "raw read must restore interrupts");
}

void pps_acquires_lock_after_three_pulses() {
  pps_init(kPin);
  pulse(0);
  expect(!pps_is_locked(), "one pulse is insufficient");
  pulse(1);
  expect(!pps_is_locked(), "two pulses are insufficient");
  pulse(2);
  expect(pps_is_locked(), "third stable PPS should acquire lock");
}

void pps_polling_does_not_count_as_new_pulses() {
  pps_init(kPin);
  pulse(0);
  for (unsigned poll = 0; poll < 100; ++poll)
    expect(!pps_is_locked(), "polling one edge must not establish lock");
}

void pps_tolerates_one_missing_pulse() {
  lock_pps();
  host::clock.now_us = kStart + 4000000;
  expect(pps_is_locked(), "one missing PPS should be tolerated");
  pulse(4);
  expect(pps_is_locked(), "returning PPS should retain lock");
}

void pps_loses_lock_at_timeout() {
  lock_pps();
  host::clock.now_us = kStart + 2000000 + 2799999;
  expect(pps_is_locked(), "lock should survive just before 2.8s");
  ++host::clock.now_us;
  expect(!pps_is_locked(), "lock should expire at 2.8s");
}

void pps_reacquires_after_outage() {
  lock_pps();
  host::clock.now_us = kStart + 5000000;
  expect(!pps_is_locked(), "outage should drop lock");
  pulse(6);
  expect(!pps_is_locked(), "first return pulse should start warmup");
  pulse(7);
  expect(!pps_is_locked(), "second return pulse is insufficient");
  pulse(8);
  expect(pps_is_locked(), "stable returning PPS should relock");
}

void pps_rejects_fast_pulses_during_warmup() {
  pps_init(kPin);
  for (unsigned i = 0; i < 10; ++i) {
    fire_edge(kPin, kStart + i * 100000);
    expect(!pps_is_locked(), "100ms pulses must not qualify as PPS");
  }
}

void pps_accepts_period_jitter() {
  pps_init(kPin);
  for (int64_t offset : {0, 950000, 2000000, 2990000}) {
    fire_edge(kPin, kStart + offset);
    pps_is_locked();
  }
  expect(pps_is_locked(), "nominal PPS jitter should retain lock");
}

void pps_stays_locked_during_long_stable_run() {
  lock_pps();
  for (unsigned second = 3; second <= 3600; ++second) {
    pulse(second);
    expect(pps_is_locked(), "stable PPS should remain locked for an hour");
  }
}

template <int64_t Period, bool Locks> void pps_lock_period_boundary() {
  pps_init(kPin);
  for (unsigned step = 0; step < 3; ++step) {
    fire_edge(kPin, kStart + step * Period);
    pps_is_locked();
  }
  expect(pps_is_locked() == Locks,
         "PPS warmup must accept 0.8..1.2s periods inclusive");
}

void pps_raw_counter_wrap_keeps_edge_available() {
  pps_init(kPin);
  // Fast-forward only the hardware edge counter instead of emitting 2^32 edges.
  s_pps_count = UINT32_MAX - 1;
  for (unsigned step = 0; step < 3; ++step) {
    pulse(step);
    int64_t timestamp = 0;
    uint32_t count = 123;
    expect(pps_get_raw(timestamp, count),
           "wrapped PPS counter is still a valid sample");
    expect(count == static_cast<uint32_t>(UINT32_MAX + step) &&
               timestamp == kStart + static_cast<int64_t>(step) * 1000000,
           "PPS counter rollover must retain the actual edge timestamp");
    pps_is_locked();
  }
  expect(pps_is_locked(), "counter rollover must not prevent PPS lock");
}

const Test kTests[] = {
    {"pps_initially_has_no_signal", pps_initially_has_no_signal},
    {"pps_captures_timestamp_and_counts_edges",
     pps_captures_timestamp_and_counts_edges},
    {"pps_acquires_lock_after_three_pulses",
     pps_acquires_lock_after_three_pulses},
    {"pps_polling_does_not_count_as_new_pulses",
     pps_polling_does_not_count_as_new_pulses},
    {"pps_tolerates_one_missing_pulse", pps_tolerates_one_missing_pulse},
    {"pps_loses_lock_at_timeout", pps_loses_lock_at_timeout},
    {"pps_reacquires_after_outage", pps_reacquires_after_outage},
    {"pps_rejects_fast_pulses_during_warmup",
     pps_rejects_fast_pulses_during_warmup},
    {"pps_accepts_period_jitter", pps_accepts_period_jitter},
    {"pps_stays_locked_during_long_stable_run",
     pps_stays_locked_during_long_stable_run},
    {"pps_lock_period_at_800000us", pps_lock_period_boundary<800000, true>},
    {"pps_lock_period_below_800000us", pps_lock_period_boundary<799999, false>},
    {"pps_lock_period_at_1200000us", pps_lock_period_boundary<1200000, true>},
    {"pps_lock_period_above_1200000us",
     pps_lock_period_boundary<1200001, false>},
    {"pps_raw_counter_wrap_keeps_edge_available",
     pps_raw_counter_wrap_keeps_edge_available},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
