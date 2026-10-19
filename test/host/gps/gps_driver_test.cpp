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
std::string corrupt_checksum(std::string sentence) {
  sentence[sentence.size()-4] = sentence[sentence.size()-4] == '0' ? '1' : '0';
  return sentence;
}
void bad_sentence_preserves_all_trusted_timestamps() {
  begin(); feed(rmc());
  int64_t old_start = 0; require(gps.lastUtcUpdateSentenceStartUs(old_start), "UTC start must exist");
  feed(corrupt_checksum(rmc("000001.00")),11000000);
  int64_t last = 0, start = 0;
  expect(gps.lastSentenceUs(last) && last == 10000000 && gps.lastFixUs() == 10000000 &&
             utc_update() == 10000000 && gps.lastUtcUpdateSentenceStartUs(start) && start == old_start,
         "bad checksum must preserve sentence, fix, UTC and sentence-start timestamps");
  expect(gps.getState() == GPSState::ACTIVE && gps.nmeaFresh(),
         "a rejected sentence must not immediately invalidate a still-fresh prior fix");
}
void repeated_bad_sentences_expire_and_valid_fix_recovers() {
  gps.setStateCallback(state_changed);
  begin(); feed(rmc());
  const auto broken = corrupt_checksum(rmc("000001.00"));
  for (unsigned second = 1; second <= 60; ++second) {
    feed(broken,10000000 + second*1000000LL);
    expect(gps.lastFixUs() == 10000000 &&
               gps.getState() == (second <= 2 ? GPSState::ACTIVE : GPSState::SEARCHING),
           "continuous checksum errors must not extend or repeatedly toggle a prior fix");
  }
  expect(!gps.nmeaFresh() && !gps.utcFresh(), "bad traffic must not keep NMEA or UTC fresh");
  feed(rmc("000101.00"),71000000);
  expect(gps.getState() == GPSState::ACTIVE && gps.lastFixUs() == 71000000 &&
             utc_update() == 71000000 && gps.nmeaFresh() && gps.utcFresh(),
         "fresh valid RMC must recover all timestamps after checksum errors");
  expect(states == std::vector<GPSState>{GPSState::OFF,GPSState::SEARCHING,GPSState::ACTIVE,
                                       GPSState::SEARCHING,GPSState::ACTIVE},
         "callbacks must report only acquisition, expiry and recovery transitions");
}
template <unsigned Kind> void invalid_first_sentence_cannot_create_fix() {
  begin(); auto broken = rmc();
  if constexpr (Kind == 0) broken = corrupt_checksum(broken);
  else if constexpr (Kind == 1) broken = broken.substr(0,broken.find('*')) + "\r\n";
  else broken.replace(broken.size()-4,2,"ZZ");
  feed(broken);
  int64_t timestamp = 0;
  expect(gps.getState() == GPSState::SEARCHING && gps.lastFixUs() == 0 &&
             !gps.lastSentenceUs(timestamp) && !gps.lastUtcUpdateUs(timestamp) &&
             !gps.nmeaFresh() && !gps.utcFresh(),
         "bad, missing or nonhex checksum cannot create a first fix or trusted timestamp");
}
void bad_gga_does_not_refresh_fix_or_satellites() {
  begin(); feed(rmc());
  const auto gga = nmea("GPGGA,000001.00,4807.038,N,01131.000,E,1,07,0.9,545.4,M,46.9,M,,");
  const auto old_sats = gps.nmea().getNumSatellites();
  feed(corrupt_checksum(gga),13000000);
  expect(gps.lastFixUs() == 10000000 && utc_update() == 10000000 &&
             gps.getState() == GPSState::SEARCHING && gps.nmea().getNumSatellites() == old_sats,
         "bad GGA must not refresh fix, UTC or satellite count");
  feed(gga,14000000);
  expect(gps.getState() == GPSState::ACTIVE && gps.lastFixUs() == 14000000 &&
             gps.nmea().getNumSatellites() == 7,
         "valid GGA must restore fix and satellite count");
}
void crlf_completion_only_updates_once() {
  begin(); const auto sentence = rmc();
  feed(sentence.substr(0,sentence.size()-1),10000000); // Ends in CR.
  feed("\n",11000000);
  int64_t last = 0;
  expect(gps.lastSentenceUs(last) && last == 10000000 && gps.lastFixUs() == 10000000 &&
             utc_update() == 10000000,
         "the LF after a valid CR-terminated sentence must not refresh timestamps again");
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
  expect(gps.satellites(out,1)==1 && out[0].snr_db==-1 && out[0].prn==1 &&
             out[0].elevation_deg==40 && out[0].azimuth_deg==100 &&
             std::string(out[0].talker)=="GP" && out[0].last_seen_us==10000000,
         "empty SNR must retain the satellite with unknown signal strength");
}

template <unsigned Count> void gsv_multiple_unknown_snr() {
  begin();
  std::string body = "GPGSV,1,1," + std::to_string(Count);
  for (unsigned prn = 1; prn <= Count; ++prn)
    body += "," + std::to_string(prn) + ",40,100,";
  feed(nmea(body));
  GPSSatelliteInfo out[4];
  expect(gps.satellites(out,4)==Count, "all satellites with empty SNR must be retained");
  for (unsigned i = 0; i < Count; ++i)
    expect(out[i].prn==i+1 && out[i].snr_db==-1 && out[i].last_seen_us==10000000,
           "empty middle and final SNR fields must preserve ordering and freshness");
}

void gsv_mixed_snr_preserves_zero_and_unknown() {
  begin(); feed(nmea("GPGSV,1,1,3,1,40,100,0,2,30,200,35,3,20,300,"));
  GPSSatelliteInfo out[3];
  expect(gps.satellites(out,3)==3 && out[0].snr_db==0 && out[1].snr_db==35 &&
             out[2].snr_db==-1 && out[2].prn==3,
         "numeric zero must remain distinct from the unknown SNR of the final satellite");
}

void gsv_snr_can_become_unknown_and_recover() {
  begin(); feed(gsv(1,"GL","35"));
  feed(gsv(1,"GL",""),11000000);
  GPSSatelliteInfo out[2];
  expect(gps.satellites(out,2)==1 && out[0].snr_db==-1 && out[0].last_seen_us==11000000,
         "empty SNR must replace a previously known value and refresh the same satellite");
  feed(gsv(1,"GL","42"),12000000);
  expect(gps.satellites(out,2)==1 && out[0].snr_db==42 && out[0].last_seen_us==12000000 &&
             std::string(out[0].talker)=="GL",
         "restored SNR must update the existing satellite without duplication");
}

void gsv_last_message_can_end_with_unknown_snr() {
  begin(); feed(nmea("GPGSV,2,1,5,1,40,100,30,2,40,110,31,3,40,120,32,4,40,130,33"));
  feed(nmea("GPGSV,2,2,5,5,30,200,"),11000000);
  GPSSatelliteInfo out[5]; int64_t stamp = 0;
  expect(gps.satellites(out,5)==5 && out[4].prn==5 && out[4].snr_db==-1 &&
             out[4].last_seen_us==11000000 && out[0].last_seen_us==10000000 &&
             gps.lastGsvUs(stamp) && stamp==11000000,
         "last multipart GSV message must retain its final satellite without altering earlier entries");
}

void gsv_missing_fields_do_not_create_satellites() {
  begin();
  const char *bodies[] = {
    "GPGSV,1,1,1,", "GPGSV,1,1,1,1,", "GPGSV,1,1,1,1,40,", "GPGSV,1,1,1,1,40,100"
  };
  GPSSatelliteInfo out[1];
  for (const char *body : bodies) {
    feed(nmea(body));
    expect(gps.satellites(out,1)==0,
           "missing PRN, elevation, azimuth or SNR separator must not create a satellite");
  }
}

void gsv_optional_checksum_with_empty_snr() {
  begin(); feed("$GPGSV,1,1,1,1,40,100,\r\n");
  GPSSatelliteInfo out[1];
  expect(gps.satellites(out,1)==1 && out[0].snr_db==-1,
         "empty final SNR must also work in the supported checksum-free GSV format");
}

void gsv_empty_snr_bad_checksum_preserves_cache() {
  begin(); feed(gsv(1,"GP","35"));
  feed(corrupt_checksum(gsv(1,"GP","")),11000000);
  GPSSatelliteInfo out[1]; int64_t stamp = 0;
  expect(gps.satellites(out,1)==1 && out[0].snr_db==35 && out[0].last_seen_us==10000000 &&
             gps.lastGsvUs(stamp) && stamp==10000000,
         "bad checksum with empty SNR must preserve the prior satellite and GSV timestamp");
}

const Test kTests[] = {
  {"gps_gsv_two_unknown_snr",gsv_multiple_unknown_snr<2>},
  {"gps_gsv_four_unknown_snr",gsv_multiple_unknown_snr<4>},
  {"gps_gsv_mixed_zero_known_unknown_snr",gsv_mixed_snr_preserves_zero_and_unknown},
  {"gps_gsv_unknown_snr_updates_and_recovers",gsv_snr_can_become_unknown_and_recover},
  {"gps_gsv_multipart_last_unknown_snr",gsv_last_message_can_end_with_unknown_snr},
  {"gps_gsv_missing_fields_not_satellites",gsv_missing_fields_do_not_create_satellites},
  {"gps_gsv_optional_checksum_empty_snr",gsv_optional_checksum_with_empty_snr},
  {"gps_gsv_bad_checksum_empty_snr_preserves_cache",gsv_empty_snr_bad_checksum_preserves_cache},
  {"gps_bad_checksum_preserves_all_timestamps",bad_sentence_preserves_all_trusted_timestamps},
  {"gps_bad_checksum_stream_expires_then_recovers",repeated_bad_sentences_expire_and_valid_fix_recovers},
  {"gps_first_sentence_bad_checksum",invalid_first_sentence_cannot_create_fix<0>},
  {"gps_first_sentence_missing_checksum",invalid_first_sentence_cannot_create_fix<1>},
  {"gps_first_sentence_nonhex_checksum",invalid_first_sentence_cannot_create_fix<2>},
  {"gps_bad_gga_does_not_refresh_fix",bad_gga_does_not_refresh_fix_or_satellites},
  {"gps_crlf_completion_updates_once",crlf_completion_only_updates_once},
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
  {"gps_bad_checksum_keeps_last_valid_fix",bad_checksum_does_not_refresh_fix<false>},
  {"gps_bad_checksum_cannot_extend_active_state",bad_checksum_does_not_refresh_fix<true>},
  {"gps_noise_recovery",noise_then_valid_sentence_recovers},
  {"gps_gsv_talker_and_update",gsv_updates_and_separates_talkers},
  {"gps_gsv_fresh_at_deadline",gsv_freshness<15000000,true>},
  {"gps_gsv_expired_after_deadline",gsv_freshness<15000001,false>},
  {"gps_gsv_future_timestamp_rejected",gsv_freshness<-1,false>},
  {"gps_gsv_capacity_and_bounds",gsv_capacity_and_output_bounds},
  {"gps_gsv_bad_checksum",gsv_bad_checksum_ignored},
  {"gps_gsv_unknown_snr",gsv_empty_snr},
};
}
int main(int argc,char **argv) { return run_tests(argc,argv,kTests); }
