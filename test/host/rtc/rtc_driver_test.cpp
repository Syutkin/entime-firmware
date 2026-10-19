#include "../support/application/environment.h"
#include "../../../src/hal/rtc/rtc.h"
#include "config.h"
#include <cmath>
#include <limits>

namespace {
void begin() { host_project::precise_us=12345678; require(rtc.begin(),"RTC must initialize"); }
void absent_safe_defaults() {
  float value=99;
  expect(!rtc.isReady() && rtc.lostPower() && !rtc.timeValid(MIN_VALID_UNIX_SEC) &&
             rtc.unixTime()==0 && !rtc.getAgingOffsetPpm(value) &&
             !rtc.setAgingOffsetPpm(1,value) && value==99, "uninitialized RTC must be unavailable");
  rtc.setTime(1704067200);
  expect(host_driver::ds3231.writes==0 && rtc.lastSyncUs()==0,
         "uninitialized time write must not change hardware or sync timestamp");
}
void begin_failure_and_retry() {
  host_driver::ds3231.found=false;
  expect(!rtc.begin() && !rtc.isReady(),"absent DS3231 must fail initialization");
  host_driver::ds3231.found=true; begin(); rtc.begin();
  expect(host_driver::ds3231.begins==2 && host_driver::ds3231.sqw_setups==1,
         "failed begin may retry; successful begin must be idempotent");
}
void explicit_wire_and_sqw() {
  TwoWire other; require(rtc.begin(other),"alternate I2C bus must initialize");
  float ppm=99; other.register_value=10;
  expect(rtc.getAgingOffsetPpm(ppm) && std::fabs(ppm-1.0f)<.0001f &&
             host_driver::ds3231.wire==&other && other.requests==1 && Wire.requests==0 &&
             host::gpio_modes[RTC_SQW_PIN]==INPUT_PULLUP,
         "RTC and aging access must use the injected I2C bus and configure SQW input");
}
template <bool Lost, int64_t Seconds, bool Valid> void trusted_time() {
  host_driver::ds3231.lost_power=Lost; host_driver::ds3231.seconds=Seconds; begin();
  expect(rtc.timeValid(MIN_VALID_UNIX_SEC)==Valid, "RTC trust combines readiness, OSF and minimum date");
}
template <unsigned Overload> void time_write_updates_timestamp() {
  begin(); host_driver::ds3231.lost_power=true;
  if constexpr(Overload==0) rtc.setTime(uint32_t(1709164800));
  else if constexpr(Overload==1) rtc.setTime(DateTime(uint32_t(1709164800)));
  else rtc.setTime(2024,2,29,0,0,0);
  expect(rtc.unixTime()==1709164800 && rtc.lastSyncUs()==12345678 &&
             !rtc.lostPower() && host_driver::ds3231.writes==1,
         "all time setters must write the leap-day UTC and record monotonic write time");
  host_project::precise_us+=1234; rtc.setTime(uint32_t(1709164801));
  expect(rtc.lastSyncUs()==12346912 && rtc.unixTime()==1709164801,
         "later explicit correction must update both RTC value and sync timestamp");
}
template <int Raw> void read_signed_aging() {
  begin(); Wire.register_value=static_cast<uint8_t>(Raw); float value=99;
  expect(rtc.getAgingOffsetPpm(value) && std::fabs(value-Raw*.1f)<.0001f &&
             Wire.address==0x68 && Wire.written==std::vector<uint8_t>{0x10} && !Wire.last_stop,
         "aging read must preserve sign and use register 0x10 with repeated start");
}
template <int MilliPpm,int ExpectedRaw> void aging_round_and_clamp() {
  begin(); float applied=99;
  expect(rtc.setAgingOffsetPpm(MilliPpm/1000.0f,applied) &&
             Wire.written==std::vector<uint8_t>{0x10,static_cast<uint8_t>(ExpectedRaw)} &&
             std::fabs(applied-ExpectedRaw*.1f)<.0001f && Wire.last_stop,
         "aging ppm must round to signed 0.1 ppm steps and clamp to int8 bounds");
  expect(rtc.lastSyncUs()==0,"aging calibration must not masquerade as a time correction");
}
template <unsigned Failure> void aging_i2c_failure() {
  begin(); float value=99;
  if constexpr(Failure==0) Wire.end_error=2;
  else if constexpr(Failure==1) Wire.returned_bytes=0;
  else Wire.end_error=3;
  const bool ok=Failure==2 ? rtc.setAgingOffsetPpm(1,value) : rtc.getAgingOffsetPpm(value);
  expect(!ok && value==99,"failed I2C operation must return false without publishing a result");
  if constexpr(Failure==0) expect(Wire.requests==0,"NACK must stop the read before requestFrom");
}
void temperature_readback() {
  begin(); host_driver::ds3231.temperature=-12.25f;
  expect(rtc.getTemperature()==-12.25f,"RTC temperature must preserve fractional negative values");
}
const Test kTests[] = {
  {"rtc_driver_uninitialized",absent_safe_defaults}, {"rtc_driver_begin_retry",begin_failure_and_retry},
  {"rtc_driver_injected_bus",explicit_wire_and_sqw},
  {"rtc_driver_trust_valid",trusted_time<false,1704067200,true>},
  {"rtc_driver_trust_lost_power",trusted_time<true,1704067200,false>},
  {"rtc_driver_trust_minimum_exact",trusted_time<false,1577836800,true>},
  {"rtc_driver_trust_minimum_before",trusted_time<false,1577836799,false>},
  {"rtc_driver_set_unix",time_write_updates_timestamp<0>},
  {"rtc_driver_set_datetime",time_write_updates_timestamp<1>},
  {"rtc_driver_set_components",time_write_updates_timestamp<2>},
  {"rtc_driver_read_aging_min",read_signed_aging<-128>},
  {"rtc_driver_read_aging_negative",read_signed_aging<-1>},
  {"rtc_driver_read_aging_zero",read_signed_aging<0>},
  {"rtc_driver_read_aging_max",read_signed_aging<127>},
  {"rtc_driver_aging_zero",aging_round_and_clamp<0,0>},
  {"rtc_driver_aging_below_half",aging_round_and_clamp<49,0>},
  {"rtc_driver_aging_positive_half",aging_round_and_clamp<50,1>},
  {"rtc_driver_aging_negative_half",aging_round_and_clamp<-50,-1>},
  {"rtc_driver_aging_above_half",aging_round_and_clamp<151,2>},
  {"rtc_driver_aging_min_exact",aging_round_and_clamp<-12800,-128>},
  {"rtc_driver_aging_max_exact",aging_round_and_clamp<12700,127>},
  {"rtc_driver_aging_clamp_low",aging_round_and_clamp<-100000,-128>},
  {"rtc_driver_aging_clamp_high",aging_round_and_clamp<100000,127>},
  {"rtc_driver_aging_read_nack",aging_i2c_failure<0>},
  {"rtc_driver_aging_short_read",aging_i2c_failure<1>},
  {"rtc_driver_aging_write_nack",aging_i2c_failure<2>},
  {"rtc_driver_temperature",temperature_readback},
};
}
int main(int argc,char **argv) { return run_tests(argc,argv,kTests); }
