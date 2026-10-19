#include "../support/application/command_environment.h"
#include "command/command_parser.h"
#include "storage/settings.h"
#include <cmath>
#include <limits>

namespace {
void seed() {
  host_project::now_ms = 10000;
  require(settings.begin(), "settings must initialize");
  auto device = settings.getDevice(); device.name = "RaceTimer"; device.number = 7;
  require(settings.setDevice(device), "seed device must validate");
  auto wifi = settings.getWifi(); wifi.ssid = "Stored"; wifi.passwd = "stored-pass";
  require(settings.setWifi(wifi), "seed WiFi must validate");
  require(settings.save() > 0, "seed configuration must persist");
}
std::string snapshot() {
  std::string text; serializeJson(settings.toJson(), text);
  return text + "|" + settings.getWifi().passwd;
}
JsonDocument request(const std::string &json) {
  host_project::MemoryStream stream; CommandParser parser(stream, "Serial");
  stream.feed(json + '\n'); parser.update();
  require(!stream.output.empty() && stream.output.back() == '\n', "one newline-terminated response");
  require(stream.output.find('\n') == stream.output.size() - 1, "one response per request");
  JsonDocument response;
  require(!deserializeJson(response, stream.output), "response must be valid JSON");
  return response;
}
void error(const JsonDocument &response, int code) {
  expect(response["status"] == "error" && response["error_code"] == code,
         "response must report the specified protocol error");
}

struct BadRequest { const char *json; int code; };
const BadRequest kBad[] = {
  {R"({"id":17})",101}, {R"({"cmd":42,"id":17})",101},
  {R"({"cmd":"unknown","id":17})",100},
  {R"({"cmd":"save_config","id":17})",102},
  {R"({"cmd":"save_config","id":17,"data":[]})",103},
  {R"({"cmd":"save_config","id":17,"data":{"device":{"type":3}}})",103},
  {R"({"cmd":"wifi","id":17})",102},
  {R"({"cmd":"wifi","id":17,"enable":"true"})",103},
  {R"({"cmd":"sync_source","id":17})",102},
  {R"({"cmd":"sync_source","id":17,"source":2})",103},
  {R"({"cmd":"sync_source","id":17,"source":"invalid"})",103},
  {R"({"cmd":"calibrate","id":17})",102},
  {R"({"cmd":"calibrate","id":17,"offset":"invalid"})",103},
};
template <size_t Index> void rejected_request_has_no_side_effects() {
  seed(); const auto before = snapshot(); const auto nvs = host_project::nvs;
  const auto response = request(kBad[Index].json); error(response, kBad[Index].code);
  expect(response["id"] == 17, "valid parsed request ID must survive errors");
  expect(snapshot() == before && host_project::nvs == nvs &&
             wifiManager.begins == 0 && wifiManager.connects == 0 && wifiManager.stops == 0 &&
             rtc.aging_reads == 0 && rtc.aging_writes == 0 && rtc.writes == 0 &&
             host_project::ntp_calls == 0 && host_project::calibration_calls == 0 &&
             host_project::timer.starts == 0,
         "rejected command must not mutate settings or invoke hardware operations");
}
template <unsigned Id> void ping_preserves_id() {
  seed(); const char *ids[] = {"17", "0", "\"client-17\"", "false"};
  const auto response = request(std::string("{\"cmd\":\"ping\",\"id\":") + ids[Id] + "}");
  JsonDocument expected; require(!deserializeJson(expected, ids[Id]), "expected ID");
  expect(response["cmd"] == "pong" && response["status"] == "ok" &&
             response["id"] == expected.as<JsonVariantConst>(), "pong must retain ID value and type");
}
void ping_without_id() {
  seed(); auto response = request(R"({"cmd":"ping"})");
  expect(response["cmd"] == "pong" && response["id"].isNull(), "absent ID must stay absent");
}
void malformed_json_returns_parser_error() {
  seed(); const auto before = snapshot();
  error(request("{broken"), 101);
  expect(snapshot() == before, "malformed JSON must not reach a handler");
}
void load_config_hides_password() {
  seed(); const auto before = snapshot();
  auto response = request(R"({"cmd":"load_config","id":"load"})");
  expect(response["status"] == "ok" && response["id"] == "load" &&
             response["data"]["device"]["name"] == "RaceTimer" &&
             response["data"]["wifi"]["passwd"] == "", "load_config response and password policy");
  expect(snapshot() == before, "read-only command must not modify stored password");
}
void no_op_save_does_not_write() {
  seed(); const auto nvs = host_project::nvs;
  const auto writes = host_project::successful_writes;
  auto response = request(R"({"cmd":"save_config","data":{}})");
  expect(response["status"] == "ok" && response["saved_keys"] == 0 &&
             response["reboot_needed"] == false && host_project::nvs == nvs &&
             host_project::successful_writes == writes,
         "no-op save must neither write nor request reboot");
}
template <size_t Index> void save_reboot_policy() {
  seed(); const char *data[] = {
    R"({"device":{"name":"Other"}})", R"({"device":{"number":8}})",
    R"({"device":{"type":2}})", R"({"wifi":{"active":true}})",
    R"({"wifi":{"ssid":"Other"}})", R"({"wifi":{"passwd":"other-pass"}})",
    R"({"gps":{"enabled":false}})", R"({"device":{"timezone_offset_min":0}})",
    R"({"sync":{"source":2}})", R"({"sync":{"auto":false}})",
  };
  auto response = request(std::string("{\"cmd\":\"save_config\",\"data\":") + data[Index] + "}");
  expect(response["status"] == "ok" && response["saved_keys"].as<int>() > 0 &&
             response["reboot_needed"].as<bool>() == (Index < 7), "reboot policy for changed field");
  expect(host_project::timer.starts == 0, "save_config must report reboot need without scheduling one");
}

template <bool Rtc> void time_response() {
  seed(); host_project::utc_available = true; host_project::accuracy_us = 42;
  host_project::sync_status.source = Rtc ? TimeSource::RTC : TimeSource::GPS_PPS;
  auto response = request(R"({"cmd":"time","id":17})");
  expect(response["time"] == 1704067210000000LL && response["accuracy_us"] == 42 &&
             response["source"] == (Rtc ? "rtc" : "gps") &&
             response["status"] == (Rtc ? "warning" : "ok") && response["id"] == 17,
         "time response must expose UTC, source and uncertainty without altering them");
}
void no_time_returns_error() { seed(); error(request(R"({"cmd":"time"})"),201); }

template <bool Saved> void wifi_connect_uses_credentials() {
  seed(); const auto before = snapshot();
  auto response = request(Saved ? R"({"cmd":"wifi","enable":true})"
      : R"({"cmd":"wifi","enable":true,"ssid":"Explicit","passwd":"explicit-pass"})");
  expect(response["status"] == "ok" && wifiManager.begins == 1 && wifiManager.connects == 1 &&
             wifiManager.ssid == (Saved ? "Stored" : "Explicit") &&
             wifiManager.password == (Saved ? "stored-pass" : "explicit-pass") &&
             wifiManager.state == WiFiState::CONNECTING, "WiFi must receive selected credentials once");
  expect(snapshot() == before, "temporary WiFi command must not overwrite saved settings");
}
template <unsigned Failure> void wifi_failure() {
  seed();
  if constexpr (Failure == 0) {
    auto wifi = settings.getWifi(); wifi.ssid = "";
    require(settings.setWifi(wifi), "empty stored SSID is valid");
    error(request(R"({"cmd":"wifi","enable":true})"),102);
    expect(wifiManager.begins == 0 && wifiManager.connects == 0, "missing SSID must not start WiFi");
  } else if constexpr (Failure == 1) {
    wifiManager.start_ok = false;
    error(request(R"({"cmd":"wifi","enable":true})"),205);
    expect(wifiManager.connects == 1, "failed start must not be retried by the handler");
  } else {
    wifiManager.state = WiFiState::CONNECTED; wifiManager.stop_ok = false;
    error(request(R"({"cmd":"wifi","enable":false})"),206);
    expect(wifiManager.state == WiFiState::CONNECTED && wifiManager.stops == 1,
           "stop timeout must not claim disabled state");
  }
}
void wifi_disable() {
  seed(); wifiManager.state = WiFiState::CONNECTED;
  auto response = request(R"({"cmd":"wifi","enable":false})");
  expect(response["state"] == "disabled" && wifiManager.state == WiFiState::OFF &&
             wifiManager.stops == 1 && wifiManager.connects == 0, "disable must only stop WiFi");
}

template <unsigned Policy> void sync_source_updates_policy() {
  seed(); const char *names[] = {"auto", "gps", "rtc"};
  host_project::sync_status.source = Policy == 0 ? TimeSource::GPS_PPS : TimeSource::RTC;
  auto response = request(std::string("{\"cmd\":\"sync_source\",\"source\":\"") + names[Policy] + "\"}");
  expect(response["status"] == "ok" && settings.getSync().source == Policy &&
             response["reason"] == (Policy == 0 ? "auto" : Policy == 1 ? "no_gps_signal" : "ok"),
         "sync_source must report actual source separately from selected policy");
  SettingsManager reboot; require(reboot.begin(), "saved policy must reload");
  expect(reboot.getSync().source == Policy, "sync_source must persist policy");
}

template <unsigned Failure> void rtc_calibration_failure() {
  seed(); rtc.ready = Failure != 0; rtc.read_ok = Failure != 1; rtc.write_ok = Failure != 2;
  rtc.aging = 1.2f;
  error(request(R"({"cmd":"calibrate","offset":-1.5})"), Failure == 0 ? 201 : 202);
  expect(rtc.aging == 1.2f && rtc.aging_writes == (Failure == 2 ? 1U : 0U),
         "failed calibration must not update aging or write after failed precondition");
}
void rtc_calibration_success() {
  seed(); rtc.ready = true; rtc.aging = 1.2f;
  host_project::sync_status.last_offset_us = -123;
  auto response = request(R"({"cmd":"calibrate","offset":-1.5})");
  expect(response["status"] == "ok" && response["previous_offset"] == 1.2f &&
             response["new_offset"] == -1.5f && response["estimated_error_us"] == 123 &&
             rtc.aging == -1.5f && rtc.aging_reads == 1 && rtc.aging_writes == 1,
         "calibration must pass ppm to RTC once and return the applied value");
}

template <unsigned Failure> void ntp_precondition_or_failure() {
  seed(); wifiManager.state = Failure == 0 ? WiFiState::OFF : WiFiState::CONNECTED;
  rtc.ready = Failure != 1; host_project::ntp_ok = false;
  error(request(R"({"cmd":"sync_ntp"})"), Failure == 0 ? 204 : Failure == 1 ? 201 : 203);
  expect(host_project::ntp_calls == (Failure == 2 ? 1U : 0U) && rtc.writes == 0,
         "failed NTP preconditions must not invoke sync; failed sync must not write RTC");
}
void ntp_success_uses_configured_servers() {
  seed(); wifiManager.state = WiFiState::CONNECTED; rtc.ready = true;
  auto sync = settings.getSync(); sync.ntp1 = "one.test"; sync.ntp2 = "two.test"; sync.ntp3 = "three.test";
  require(settings.setSync(sync), "NTP server names must validate");
  auto response = request(R"({"cmd":"sync_ntp","id":17})");
  expect(response["status"] == "ok" && response["sync_duration_ms"] == 37 &&
             response["rtc_time"] == 1704067200 && response["ntp_servers"].size() == 3 &&
             response["ntp_servers"][2] == "three.test" && rtc.writes == 1 &&
             host_project::ntp_calls == 1 && host_project::ntp_servers[0] == "one.test" &&
             host_project::ntp_servers[1] == "two.test" && host_project::ntp_servers[2] == "three.test" &&
             host_project::ntp_timeouts[0] == 15000 && host_project::ntp_timeouts[1] == 2500 &&
             host_project::ntp_timeouts[2] == 1500, "NTP command must forward servers and timing policy");
}

template <TouchCalibrationFlowStatus Status, int Code> void touch_flow_error_mapping() {
  seed(); host_project::calibration_result.status = Status;
  error(request(R"({"cmd":"touch_calibrate","id":17})"), Code);
  expect(host_project::calibration_calls == 1, "touch command must invoke the flow once");
}
void touch_flow_success() {
  seed(); auto &flow = host_project::calibration_result;
  flow.status = TouchCalibrationFlowStatus::Ok; flow.savedKeys = 6;
  flow.calibration.valid = true; flow.calibration.data[0] = 123;
  auto response = request(R"({"cmd":"touch_calibrate"})");
  expect(response["status"] == "ok" && response["saved_keys"] == 6 &&
             response["cal_valid"] == true && response["calibration"].size() == 5 &&
             response["calibration"][0] == 123, "touch flow result must serialize without loss");
}

void reset_schedules_and_reuses_timer() {
  seed(); auto response = request(R"({"cmd":"factory_reset","id":17})");
  expect(response["status"] == "ok" && settings.getDevice().name == DEFAULT_DEVICE_NAME &&
             host_project::timer.creates == 1 && host_project::timer.starts == 1 &&
             host_project::timer.delay_us == 2000000 && host_project::timer.restarts == 0,
         "factory reset must restore defaults and schedule a delayed reboot");
  response = request(R"({"cmd":"factory_reset"})");
  expect(host_project::timer.creates == 1 && host_project::timer.starts == 2 &&
             host_project::timer.stops == 1, "repeated reset must replace deadline on the same timer");
  require(host_project::timer.callback != nullptr, "reset callback must be registered");
  host_project::timer.callback(nullptr);
  expect(host_project::timer.restarts == 1, "only the scheduled callback must request restart");
}
template <bool Creation> void failed_reset_timer_is_reported() {
  seed(); host_project::timer.create_ok = !Creation; host_project::timer.start_ok = Creation;
  auto response = request(R"({"cmd":"factory_reset"})");
  require(Creation ? host_project::timer.creates == 1 : host_project::timer.starts == 1,
          "selected timer failure must actually be exercised");
  expect(response["status"] == "error" && response["error_code"].is<int>() &&
             response["error_code"].as<int>() > 0,
         "timer failure must be reported as a protocol error, not successful reset");
  expect(!host_project::timer.active && host_project::timer.restarts == 0,
         "failed timer must not claim a pending reboot");
}

template <unsigned State> void status_mapping() {
  seed(); const WiFiState wifi[] = {WiFiState::OFF, WiFiState::CONNECTING, WiFiState::CONNECTED,
                                  WiFiState::DISCONNECTED, WiFiState::RECONNECTING, WiFiState::ERROR};
  const char *names[] = {"off", "connecting", "connected", "error", "connecting", "error"};
  wifiManager.state = wifi[State];
  auto response = request(R"({"cmd":"status","id":17})");
  expect(response["status"] == "ok" && response["id"] == 17 &&
             response["device"]["type"] == "start" && response["wifi"]["state"] == names[State] &&
             response["sync"]["state"] == "nosync" && response["system"]["uptime_s"] == 10 &&
             response["system"]["free_heap_bytes"] == 123456, "status must map internal state to protocol");
}

#define BAD(index, label) {"router_reject_" label, rejected_request_has_no_side_effects<index>}
#define REBOOT(index, label) {"router_reboot_policy_" label, save_reboot_policy<index>}
const Test kTests[] = {
  BAD(0,"missing_cmd"), BAD(1,"numeric_cmd"), BAD(2,"unknown_cmd"),
  BAD(3,"missing_config_data"), BAD(4,"array_config_data"), BAD(5,"invalid_config_value"),
  BAD(6,"missing_wifi_enable"), BAD(7,"non_boolean_wifi_enable"),
  BAD(8,"missing_source"), BAD(9,"numeric_source"), BAD(10,"unknown_source"),
  BAD(11,"missing_calibration_offset"), BAD(12,"string_calibration_offset"),
  {"router_ping_integer_id", ping_preserves_id<0>}, {"router_ping_zero_id", ping_preserves_id<1>},
  {"router_ping_string_id", ping_preserves_id<2>}, {"router_ping_boolean_id", ping_preserves_id<3>},
  {"router_ping_without_id", ping_without_id}, {"router_malformed_json", malformed_json_returns_parser_error},
  {"router_load_config_preserves_password", load_config_hides_password},
  {"router_noop_save_no_writes", no_op_save_does_not_write},
  REBOOT(0,"name"), REBOOT(1,"number"), REBOOT(2,"role"), REBOOT(3,"wifi_active"),
  REBOOT(4,"wifi_ssid"), REBOOT(5,"wifi_password"), REBOOT(6,"gps_enabled"),
  REBOOT(7,"timezone_immediate"), REBOOT(8,"source_immediate"), REBOOT(9,"auto_sync_immediate"),
  {"router_time_gps", time_response<false>}, {"router_time_rtc_warning", time_response<true>},
  {"router_time_unavailable", no_time_returns_error},
  {"router_wifi_explicit_credentials", wifi_connect_uses_credentials<false>},
  {"router_wifi_saved_credentials", wifi_connect_uses_credentials<true>},
  {"router_wifi_missing_ssid", wifi_failure<0>}, {"router_wifi_start_failure", wifi_failure<1>},
  {"router_wifi_stop_timeout", wifi_failure<2>}, {"router_wifi_disable", wifi_disable},
  {"router_sync_source_auto", sync_source_updates_policy<0>},
  {"router_sync_source_gps", sync_source_updates_policy<1>},
  {"router_sync_source_rtc", sync_source_updates_policy<2>},
  {"router_calibrate_rtc_absent", rtc_calibration_failure<0>},
  {"router_calibrate_read_failure", rtc_calibration_failure<1>},
  {"router_calibrate_write_failure", rtc_calibration_failure<2>},
  {"router_calibrate_success", rtc_calibration_success},
  {"router_ntp_no_wifi", ntp_precondition_or_failure<0>},
  {"router_ntp_no_rtc", ntp_precondition_or_failure<1>},
  {"router_ntp_timeout", ntp_precondition_or_failure<2>},
  {"router_ntp_configured_servers", ntp_success_uses_configured_servers},
  {"router_touch_not_ready", touch_flow_error_mapping<TouchCalibrationFlowStatus::TouchNotReady,201>},
  {"router_touch_wizard_failure", touch_flow_error_mapping<TouchCalibrationFlowStatus::WizardFailed,202>},
  {"router_touch_invalid_settings", touch_flow_error_mapping<TouchCalibrationFlowStatus::InvalidSettings,103>},
  {"router_touch_save_failure", touch_flow_error_mapping<TouchCalibrationFlowStatus::SaveFailed,202>},
  {"router_touch_apply_failure", touch_flow_error_mapping<TouchCalibrationFlowStatus::ApplyFailed,202>},
  {"router_touch_success", touch_flow_success},
  {"router_factory_reset_scheduled", reset_schedules_and_reuses_timer},
  {"router_reset_timer_create_failure", failed_reset_timer_is_reported<true>, "AU010"},
  {"router_reset_timer_start_failure", failed_reset_timer_is_reported<false>, "AU010"},
  {"router_status_wifi_off", status_mapping<0>}, {"router_status_wifi_connecting", status_mapping<1>},
  {"router_status_wifi_connected", status_mapping<2>}, {"router_status_wifi_disconnected", status_mapping<3>},
  {"router_status_wifi_reconnecting", status_mapping<4>}, {"router_status_wifi_error", status_mapping<5>},
};
#undef BAD
#undef REBOOT
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
