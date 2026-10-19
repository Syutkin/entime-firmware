#include "../support/application/environment.h"
#include "../../../src/hal/gps/gps.h"
#include "config.h"
#include <cstdio>
#include <vector>

namespace {
std::vector<GPSState> states;
std::vector<int> sats;
void state_changed(GPSState state) { states.push_back(state); }
void sats_changed(int8_t count) { sats.push_back(count); }
std::string nmea(const std::string &body) {
  uint8_t checksum = 0;
  for (char c : body) checksum ^= static_cast<uint8_t>(c);
  char tail[8]; std::snprintf(tail,sizeof(tail),"*%02X\r\n",checksum);
  return "$" + body + tail;
}
std::string rmc(const std::string &time = "000000.00", const char *valid = "A",
                const char *date = "010124") {
  return nmea("GPRMC," + time + "," + valid + ",4807.038,N,01131.000,E,0.0,0.0," + date + ",,,A");
}
void begin() { host_project::precise_us = 10000000; gps.begin(); }
void feed(const std::string &sentence, int64_t us = 10000000) {
  host_project::precise_us = us; host_driver::feed(sentence); gps.update();
}
int64_t utc_update() {
  int64_t us = 0; require(gps.lastUtcUpdateUs(us), "trusted UTC must have been received"); return us;
}
void disabled_does_not_read_uart() {
  host_project::precise_us = 10000000; gps.begin(false); host_driver::feed(rmc()); gps.update();
  expect(!gps.isReady() && gps.getState() == GPSState::OFF && !gps.nmeaFresh() &&
             !gps.utcFresh() && host_driver::uart_begins == 0 &&
             host_driver::levels[GPS_POWER_PIN] == LOW && !host_driver::uart.empty(),
         "disabled GPS must remain off without opening/consuming UART");
}
void enable_after_disabled_and_begin_idempotence() {
  gps.begin(false); begin(); gps.begin();
  expect(gps.isReady() && gps.getState() == GPSState::SEARCHING &&
             host_driver::uart_begins == 1 && host_driver::baud == 9600 &&
             host_driver::rx_pin == GPS_RX_PIN && host_driver::tx_pin == GPS_TX_PIN &&
             host_driver::levels[GPS_POWER_PIN] == HIGH, "GPS must initialize only once when enabled");
}
void fragmented_timestamp() {
  begin(); const auto sentence = rmc();
  feed(sentence.substr(0,1),10000000);
  expect(!gps.utcFresh(), "incomplete sentence must not provide UTC");
  feed(sentence.substr(1),10300000);
  int64_t start = 0; require(gps.lastUtcUpdateSentenceStartUs(start), "sentence start must be available");
  expect(start == 10000000 && utc_update() == 10300000 && gps.lastFixUs() == 10300000 &&
             gps.getState() == GPSState::ACTIVE && gps.nmea().getYear() == 2024,
         "UTC must retain start and completion timestamps across UART fragments");
}
template <bool Utc, int64_t Delta, bool Fresh> void freshness_boundary() {
  begin(); feed(rmc()); host_project::precise_us = 10000000 + Delta;
  expect((Utc ? gps.utcFresh() : gps.nmeaFresh()) == Fresh,
         "freshness must accept the inclusive boundary and reject past/future samples");
}
void duplicate_second_does_not_refresh_utc() {
  begin(); feed(rmc()); const auto original = utc_update();
  feed(rmc("000000.50"),11000000);
  expect(utc_update() == original && gps.nmeaFresh(), "same UTC second must not refresh UTC anchor age");
  feed(rmc("000001.00"),12000000);
  expect(utc_update() == 12000000, "next confirmed UTC second must refresh the age");
}
void invalid_fix_recovery() {
  begin(); feed(rmc()); feed(rmc("000001.00","V"),11000000);
  expect(gps.getState() == GPSState::SEARCHING && gps.lastFixUs() == 10000000,
         "invalid RMC fix must not replace last valid fix");
  feed(rmc("000002.00"),12000000);
  expect(gps.getState() == GPSState::ACTIVE && gps.lastFixUs() == 12000000 &&
             utc_update() == 12000000, "fresh valid RMC must recover fix and UTC");
}
void callbacks_only_on_changes() {
  host_project::precise_us = 10000000;
  gps.setStateCallback(state_changed); gps.setSatsCallback(sats_changed); gps.begin();
  feed(rmc());
  feed(nmea("GPGGA,000000.00,4807.038,N,01131.000,E,1,07,0.9,545.4,M,46.9,M,,"));
  const auto before_states = states.size(), before_sats = sats.size();
  for (unsigned i=0;i<5;++i) gps.update();
  expect(states.size() == before_states && sats.size() == before_sats,
         "polling unchanged GPS data must not duplicate callbacks");
  host_project::precise_us += 2500001; gps.update();
  expect(states == std::vector<GPSState>{GPSState::OFF,GPSState::SEARCHING,GPSState::ACTIVE,GPSState::SEARCHING}
             && !sats.empty() && sats.back() == -1,
         "callbacks must include initial state, acquisition and expiry");
}
void unregister_callbacks() {
  begin(); gps.setStateCallback(state_changed); gps.setSatsCallback(sats_changed);
  const auto a=states.size(), b=sats.size();
  gps.setStateCallback(nullptr); gps.setSatsCallback(nullptr); feed(rmc());
  expect(states.size()==a && sats.size()==b, "removed callbacks must not be invoked");
}
void old_date_does_not_supply_utc() {
  begin(); feed(rmc("000000.00","A","010119"));
  expect(!gps.utcFresh(), "fix with pre-2020 date must not be trusted for UTC");
}
template <bool Expiry> void bad_checksum_does_not_refresh_fix() {
  begin(); feed(rmc()); std::string broken=rmc("000001.00");
  broken[broken.size()-4] = broken[broken.size()-4] == '0' ? '1' : '0';
  feed(broken,13000000);
  expect(Expiry ? gps.getState() == GPSState::SEARCHING : gps.lastFixUs() == 10000000,
         "bad checksum must not refresh the last valid fix or keep it ACTIVE");
}
void noise_then_valid_sentence_recovers() {
  begin(); feed(std::string(180,'x')+"\r\n"); feed(rmc("000001.00"),11000000);
  expect(gps.getState()==GPSState::ACTIVE && utc_update()==11000000,
         "valid sentence after oversized noise must recover parser state");
}
std::string gsv(unsigned prn, const std::string &talker="GP", const std::string &snr="30") {
  return nmea(talker+"GSV,1,1,1,"+std::to_string(prn)+",40,100,"+snr);
}
void gsv_updates_and_separates_talkers() {
  begin(); feed(gsv(1)); feed(gsv(1,"GL"),11000000); feed(gsv(1,"GP","45"),12000000);
  GPSSatelliteInfo out[4]; const auto count=gps.satellites(out,4);
  expect(count==2 && out[0].prn==1 && out[0].snr_db==45 &&
             std::string(out[0].talker)=="GP" && std::string(out[1].talker)=="GL",
         "GSV must update a satellite in place and distinguish identical PRN across talkers");
}
template <int64_t Age, bool Fresh> void gsv_freshness() {
  begin(); feed(gsv(1)); host_project::precise_us=10000000+Age;
  GPSSatelliteInfo out[2];
  expect(gps.satellites(out,2)==(Fresh?1:0) && gps.satellites(out,2,false)==1,
         "satellite freshness must be bounded without deleting cached data");
}
void gsv_capacity_and_output_bounds() {
  begin(); for(unsigned prn=1;prn<=33;++prn) feed(gsv(prn),10000000+prn*1000);
  GPSSatelliteInfo out[33]; const auto count=gps.satellites(out,33);
  bool first=false,last=false;
  for(unsigned i=0;i<count;++i) { first|=out[i].prn==1; last|=out[i].prn==33; }
  expect(count==32 && !first && last && gps.satellites(out,1)==1 &&
             gps.satellites(nullptr,1)==0 && gps.satellites(out,0)==0,
         "bounded satellite cache must replace oldest entry and respect caller capacity");
}
void gsv_bad_checksum_ignored() {
  begin(); auto bad=gsv(1); bad[bad.size()-4]=bad[bad.size()-4]=='0'?'1':'0'; feed(bad);
  GPSSatelliteInfo out[2]; int64_t stamp=0;
  expect(gps.satellites(out,2)==0 && !gps.lastGsvUs(stamp), "bad GSV must not update satellite data");
}
void gsv_empty_snr() {
  begin(); feed(gsv(1,"GP","")); GPSSatelliteInfo out[1];
  expect(gps.satellites(out,1)==1 && out[0].snr_db==-1, "empty SNR must be represented as unknown");
}

const Test kTests[] = {
  {"gps_disabled_no_uart",disabled_does_not_read_uart},
  {"gps_enable_after_disabled_idempotent",enable_after_disabled_and_begin_idempotence},
  {"gps_fragmented_sentence_timestamps",fragmented_timestamp},
  {"gps_nmea_fresh_at_deadline",freshness_boundary<false,2500000,true>},
  {"gps_nmea_expired_after_deadline",freshness_boundary<false,2500001,false>},
  {"gps_nmea_future_timestamp_rejected",freshness_boundary<false,-1,false>},
  {"gps_utc_fresh_at_deadline",freshness_boundary<true,2500000,true>},
  {"gps_utc_expired_after_deadline",freshness_boundary<true,2500001,false>},
  {"gps_utc_future_timestamp_rejected",freshness_boundary<true,-1,false>},
  {"gps_duplicate_second_does_not_refresh_utc",duplicate_second_does_not_refresh_utc},
  {"gps_invalid_fix_and_recovery",invalid_fix_recovery},
  {"gps_callbacks_on_changes",callbacks_only_on_changes},
  {"gps_callbacks_can_be_removed",unregister_callbacks},
  {"gps_pre2020_date_not_trusted",old_date_does_not_supply_utc},
  {"gps_bad_checksum_keeps_last_valid_fix",bad_checksum_does_not_refresh_fix<false>,"AU011"},
  {"gps_bad_checksum_cannot_extend_active_state",bad_checksum_does_not_refresh_fix<true>,"AU011"},
  {"gps_noise_recovery",noise_then_valid_sentence_recovers},
  {"gps_gsv_talker_and_update",gsv_updates_and_separates_talkers},
  {"gps_gsv_fresh_at_deadline",gsv_freshness<15000000,true>},
  {"gps_gsv_expired_after_deadline",gsv_freshness<15000001,false>},
  {"gps_gsv_future_timestamp_rejected",gsv_freshness<-1,false>},
  {"gps_gsv_capacity_and_bounds",gsv_capacity_and_output_bounds},
  {"gps_gsv_bad_checksum",gsv_bad_checksum_ignored},
  {"gps_gsv_unknown_snr",gsv_empty_snr,"AU012"},
};
}
int main(int argc,char **argv) { return run_tests(argc,argv,kTests); }
