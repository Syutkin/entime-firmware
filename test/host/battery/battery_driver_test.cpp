#include "../support/application/environment.h"
#include "../../../src/hal/ina226/ina226.h"
#include "config.h"
#include <cmath>
#include <limits>
#include <vector>

namespace {
struct Notification { InaBatteryLevel level; int percent; bool charging; };
std::vector<Notification> notifications;
void changed(InaBatteryLevel level,int percent,bool charging) {
  notifications.push_back({level,percent,charging});
}
void begin(float voltage=7.5f,float current=.05f) {
  host_driver::ina.voltage=voltage; host_driver::ina.current=current;
  ina226.setBatteryStateChangedCallback(changed);
  require(ina226.begin(),"INA226 must initialize");
}
void sample(float voltage,float current=.05f) {
  host_driver::ina.voltage=voltage; host_driver::ina.current=current;
  host::fire_gpio(INA226_ALERT_PIN); ina226.update();
  expect(host::interrupt_depth==0,"DATA_READY consumption must restore interrupts");
}
void uninitialized_is_inert() {
  ina226.update();
  expect(!ina226.isReady() && !ina226.hasValidSample() &&
             ina226.batteryLevel()==InaBatteryLevel::NoData && ina226.batteryPercent()==-1 &&
             host_driver::ina.voltage_reads==0,"uninitialized monitor must not read or publish data");
}
template <unsigned Stage> void begin_failure() {
  auto &m=host_driver::ina;
  if constexpr(Stage==0) m.found=false;
  else if constexpr(Stage==1) m.calibration_error=1;
  else {
    const char *operations[]={"average","bus_time","shunt_time","continuous",
                              "average","bus_time","shunt_time","continuous",
                              "polarity","latch","alert"};
    m.fail_operation=operations[Stage-2]; m.fail_occurrence=Stage>=6 && Stage<=9?2:1;
  }
  expect(!ina226.begin() && !ina226.isReady() && ina226.lastError()[0]!='\0' &&
             host::gpio_handlers[INA226_ALERT_PIN]==nullptr,
         "initialization failure must be reported before arming data-ready interrupt");
}
void begin_can_retry_and_is_idempotent() {
  host_driver::ina.found=false;
  expect(!ina226.begin(),"first absent-chip begin must fail");
  host_driver::ina.found=true; begin(); const auto reads=host_driver::ina.voltage_reads;
  expect(ina226.begin() && host_driver::ina.begins==2 && host_driver::ina.voltage_reads==reads &&
             host_driver::ina.max_current==.5f && host_driver::ina.shunt==.1f &&
             host_driver::ina.bootstrap_timeout==250 &&
             host::gpio_edges[INA226_ALERT_PIN]==FALLING,
         "successful begin must be idempotent after a retry and use configured calibration");
}
void bootstrap_timeout_then_recovery() {
  host_driver::ina.bootstrap_ready=false; begin();
  expect(ina226.isReady() && !ina226.hasValidSample() && ina226.batteryPercent()==-1,
         "bootstrap timeout may keep monitor ready without inventing a sample");
  sample(7.5f);
  expect(ina226.hasValidSample() && ina226.batteryPercent()==50,
         "first DATA_READY must recover from missing bootstrap sample");
}
void update_requires_irq_and_conversion_ready() {
  begin(); const auto reads=host_driver::ina.voltage_reads;
  host_driver::ina.voltage=8.4f; ina226.update();
  expect(host_driver::ina.voltage_reads==reads && ina226.getBusVoltage()==7.5f,
         "polling without DATA_READY must not consume a new sample");
  host_driver::ina.conversion_ready=false; sample(8.4f);
  expect(host_driver::ina.voltage_reads==reads,"spurious IRQ before conversion must not read");
  host_driver::ina.conversion_ready=true; sample(8.4f);
  expect(host_driver::ina.voltage_reads==reads+1 && ina226.getBusVoltage()==8.4f,
         "ready conversion after spurious IRQ must be read once");
}
void repeated_irq_is_coalesced() {
  begin(); const auto reads=host_driver::ina.voltage_reads;
  for(unsigned i=0;i<5;++i) host::fire_gpio(INA226_ALERT_PIN);
  ina226.update(); ina226.update();
  expect(host_driver::ina.voltage_reads==reads+1,"pending DATA_READY is one latest measurement, not five samples");
}

template <unsigned Error> void invalid_sample_and_recovery() {
  begin(); auto &m=host_driver::ina;
  const float nan=std::numeric_limits<float>::quiet_NaN();
  const float inf=std::numeric_limits<float>::infinity();
  if constexpr(Error==0) m.read_error=1;
  else if constexpr(Error==1) m.voltage=nan;
  else if constexpr(Error==2) m.current=nan;
  else if constexpr(Error==3) m.power=nan;
  else if constexpr(Error==4) m.voltage=inf;
  else if constexpr(Error==5) m.current=-inf;
  else m.power=inf;
  host::fire_gpio(INA226_ALERT_PIN); ina226.update();
  expect(!ina226.hasValidSample() && ina226.batteryLevel()==InaBatteryLevel::NoData &&
             ina226.batteryPercent()==-1 && !ina226.isCharging() &&
             std::isnan(ina226.getBusVoltage()) && std::isnan(ina226.getCurrent()) &&
             std::isnan(ina226.getPower()) && ina226.lastError()[0]!='\0',
         "I2C/nonfinite sample must invalidate all published measurements");
  m.read_error=0; m.power=.375f; sample(7.5f,.05f);
  expect(ina226.hasValidSample() && ina226.batteryLevel()==InaBatteryLevel::Mid &&
             ina226.batteryPercent()==50 && ina226.lastError()[0]=='\0',
         "valid sample must recover measurements and clear stale error");
}

template <int MilliVolt,int Percent> void percent_curve() {
  begin(MilliVolt/1000.0f);
  expect(ina226.batteryPercent()==Percent,"independent expected battery percentage and clamp");
}
template <int MilliVolt,InaBatteryLevel Level> void initial_level() {
  begin(MilliVolt/1000.0f);
  expect(ina226.batteryLevel()==Level,"initial sample must classify the nominal voltage threshold");
}

struct Boundary {
  float initial, edge;
  InaBatteryLevel below, exact, above;
};
// Express the physical thresholds independently of the driver's switch branches.
const Boundary kBoundaries[]={
  {6.4f,INA226_BAT_CRITICAL_MAX_V+.1f,InaBatteryLevel::Critical,InaBatteryLevel::Empty,InaBatteryLevel::Empty},
  {6.7f,INA226_BAT_EMPTY_MAX_V+.1f,InaBatteryLevel::Empty,InaBatteryLevel::Low,InaBatteryLevel::Low},
  {7.0f,INA226_BAT_LOW_MAX_V+.1f,InaBatteryLevel::Low,InaBatteryLevel::Mid,InaBatteryLevel::Mid},
  {7.5f,INA226_BAT_MID_MAX_V+.1f,InaBatteryLevel::Mid,InaBatteryLevel::Full,InaBatteryLevel::Full},
  {6.7f,INA226_BAT_CRITICAL_MAX_V-.1f,InaBatteryLevel::Critical,InaBatteryLevel::Empty,InaBatteryLevel::Empty},
  {7.0f,INA226_BAT_EMPTY_MAX_V-.1f,InaBatteryLevel::Empty,InaBatteryLevel::Low,InaBatteryLevel::Low},
  {7.5f,INA226_BAT_LOW_MAX_V-.1f,InaBatteryLevel::Low,InaBatteryLevel::Mid,InaBatteryLevel::Mid},
  {8.3f,INA226_BAT_MID_MAX_V-.1f,InaBatteryLevel::Mid,InaBatteryLevel::Full,InaBatteryLevel::Full},
};
template <size_t Index,int Side> void hysteresis_boundary() {
  const auto &b=kBoundaries[Index]; begin(b.initial);
  const float voltage=Side==0?b.edge:std::nextafter(b.edge,Side<0?-INFINITY:INFINITY);
  sample(voltage);
  expect(ina226.batteryLevel()==(Side<0?b.below:Side>0?b.above:b.exact),
         "hysteresis must distinguish the threshold and adjacent representable voltages");
}
void noise_does_not_flap_level() {
  begin(6.75f);
  for(unsigned i=0;i<100;++i) {
    sample(i%2?6.79f:6.81f);
    expect(ina226.batteryLevel()==InaBatteryLevel::Empty,"noise inside hysteresis band must not flap level");
  }
}
template <int MicroAmp,bool Charging> void charging_threshold() {
  begin(7.5f,MicroAmp/1000000.0f);
  expect(ina226.isCharging()==Charging,"charging threshold must include exactly negative 20 mA");
}
void discharging_callbacks_are_deduplicated() {
  begin(); const auto count=notifications.size();
  for(unsigned i=0;i<5;++i) sample(7.5f);
  expect(notifications.size()==count,"identical discharging samples must not repeat state callbacks");
  sample(7.6f);
  expect(notifications.size()==count+1 && notifications.back().percent==56,
         "percentage change without level change must still notify");
}
void charging_samples_drive_animation() {
  begin(); const auto count=notifications.size();
  sample(7.5f,-.02f); sample(7.5f,-.02f); sample(7.5f,-.02f);
  expect(notifications.size()==count+3 && notifications.back().charging,
         "each charging sample must notify to advance the documented battery animation");
  sample(7.5f,.05f); const auto stopped=notifications.size(); sample(7.5f,.05f);
  expect(notifications.size()==stopped && !notifications.back().charging,
         "charging stop must notify once and restore deduplication");
}
void callback_can_be_removed() {
  begin(); const auto count=notifications.size(); ina226.setBatteryStateChangedCallback(nullptr);
  sample(8.4f,-.05f);
  expect(notifications.size()==count && ina226.isCharging(),
         "removing callback must preserve measurement updates without invoking the old callback");
}

#define HYST(index,label) \
  {"battery_hysteresis_" label "_below",hysteresis_boundary<index,-1>}, \
  {"battery_hysteresis_" label "_exact",hysteresis_boundary<index,0>}, \
  {"battery_hysteresis_" label "_above",hysteresis_boundary<index,1>}
const Test kTests[]={
  {"battery_uninitialized",uninitialized_is_inert},
  {"battery_begin_absent",begin_failure<0>}, {"battery_begin_calibration_error",begin_failure<1>},
  {"battery_begin_bootstrap_average_error",begin_failure<2>},
  {"battery_begin_bootstrap_bus_error",begin_failure<3>},
  {"battery_begin_bootstrap_shunt_error",begin_failure<4>},
  {"battery_begin_bootstrap_mode_error",begin_failure<5>},
  {"battery_begin_cadence_average_error",begin_failure<6>},
  {"battery_begin_cadence_bus_error",begin_failure<7>},
  {"battery_begin_cadence_shunt_error",begin_failure<8>},
  {"battery_begin_cadence_mode_error",begin_failure<9>},
  {"battery_begin_polarity_error",begin_failure<10>},
  {"battery_begin_latch_error",begin_failure<11>},
  {"battery_begin_alert_error",begin_failure<12>},
  {"battery_begin_retry_idempotence",begin_can_retry_and_is_idempotent},
  {"battery_bootstrap_timeout_recovery",bootstrap_timeout_then_recovery},
  {"battery_irq_and_conversion_gate",update_requires_irq_and_conversion_ready},
  {"battery_coalesces_pending_irq",repeated_irq_is_coalesced},
  {"battery_read_error_recovery",invalid_sample_and_recovery<0>},
  {"battery_nan_voltage_recovery",invalid_sample_and_recovery<1>},
  {"battery_nan_current_recovery",invalid_sample_and_recovery<2>},
  {"battery_nan_power_recovery",invalid_sample_and_recovery<3>},
  {"battery_infinite_voltage_recovery",invalid_sample_and_recovery<4>},
  {"battery_infinite_current_recovery",invalid_sample_and_recovery<5>},
  {"battery_infinite_power_recovery",invalid_sample_and_recovery<6>},
  {"battery_percent_below_min",percent_curve<5000,0>},
  {"battery_percent_minimum",percent_curve<6600,0>},
  {"battery_percent_10",percent_curve<6780,10>},
  {"battery_percent_25",percent_curve<7050,25>},
  {"battery_percent_50",percent_curve<7500,50>},
  {"battery_percent_75",percent_curve<7950,75>},
  {"battery_percent_90",percent_curve<8220,90>},
  {"battery_percent_maximum",percent_curve<8400,100>},
  {"battery_percent_above_max",percent_curve<10000,100>},
  {"battery_initial_critical",initial_level<6599,InaBatteryLevel::Critical>},
  {"battery_initial_empty_boundary",initial_level<6600,InaBatteryLevel::Empty>},
  {"battery_initial_empty",initial_level<6799,InaBatteryLevel::Empty>},
  {"battery_initial_low_boundary",initial_level<6800,InaBatteryLevel::Low>},
  {"battery_initial_low",initial_level<7299,InaBatteryLevel::Low>},
  {"battery_initial_mid_boundary",initial_level<7300,InaBatteryLevel::Mid>},
  {"battery_initial_mid",initial_level<7999,InaBatteryLevel::Mid>},
  {"battery_initial_full_boundary",initial_level<8000,InaBatteryLevel::Full>},
  {"battery_initial_full",initial_level<8400,InaBatteryLevel::Full>},
  HYST(0,"critical_rising"), HYST(1,"empty_rising"), HYST(2,"low_rising"), HYST(3,"mid_rising"),
  HYST(4,"empty_falling"), HYST(5,"low_falling"), HYST(6,"mid_falling"), HYST(7,"full_falling"),
  {"battery_noise_no_level_flapping",noise_does_not_flap_level},
  {"battery_charging_before_threshold",charging_threshold<-19999,false>},
  {"battery_charging_exact_threshold",charging_threshold<-20000,true>},
  {"battery_charging_after_threshold",charging_threshold<-20001,true>},
  {"battery_callbacks_discharge_dedup",discharging_callbacks_are_deduplicated},
  {"battery_callbacks_charge_animation",charging_samples_drive_animation},
  {"battery_callbacks_remove",callback_can_be_removed},
};
#undef HYST
}
int main(int argc,char **argv) { return run_tests(argc,argv,kTests); }
