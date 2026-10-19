#include "support.h"
#include "../../../src/storage/settings.cpp"

namespace {
SettingsManager manager;
void seed() {
  require(manager.begin(), "settings must initialize");
  auto device = manager.getDevice(); device.name = "RaceTimer"; device.number = 7;
  require(manager.setDevice(device), "seed device must validate");
  auto wifi = manager.getWifi(); wifi.ssid = "Original"; wifi.passwd = "original-pass";
  require(manager.setWifi(wifi), "seed WiFi must validate");
  require(manager.save() > 0, "seed must persist");
}
std::string snapshot() {
  std::string json; serializeJson(manager.toJson(), json);
  return json + "|" + manager.getWifi().passwd;
}
bool apply(const std::string &text) {
  JsonDocument request;
  require(!deserializeJson(request, text), "scenario must supply syntactically valid JSON");
  return manager.fromJson(request);
}

// Values are checked after deserialization: testing only typed C++ setters
// would miss coercion and overflow in the command configuration path.
const char *kInvalid[] = {
  R"({"device":{"name":""}})",
  R"({"device":{"name":"12345678901234567"}})",
  R"({"device":{"name":"bad name"}})",
  R"({"device":{"name":"Таймер"}})",
  R"({"device":{"number":0}})",
  R"({"device":{"number":256}})",
  R"({"device":{"number":-1}})",
  R"({"device":{"type":0}})",
  R"({"device":{"type":3}})",
  R"({"device":{"timezone_offset_min":-721}})",
  R"({"device":{"timezone_offset_min":841}})",
  R"({"sync":{"source":3}})",
  R"({"sync":{"ntp1":""}})",
  R"({"sync":{"ntp1":"bad/host"}})",
  R"({"touch":{"calibration":[1,2,3,4]}})",
  R"({"touch":{"calibration":[1,2,3,4,5,6]}})",
  R"({"touch":{"calibration":"invalid"}})",
};
template <size_t Index> void invalid_value_is_atomic() {
  seed(); const auto before = snapshot(); const auto nvs = host_project::nvs;
  expect(!apply(kInvalid[Index]), "invalid configuration value must be rejected");
  expect(snapshot() == before && host_project::nvs == nvs,
         "invalid request must leave all RAM groups and saved keys unchanged");
}

const char *kWrongTypes[] = {
  R"({"sync":{"auto":"false"}})",
  R"({"gps":{"enabled":0}})",
  R"({"wifi":{"active":"yes"}})",
  R"({"touch":{"enabled":[]}})",
  R"({"touch":{"cal_valid":1}})",
  R"({"device":[]})",
  R"({"wifi":false})",
  R"({"touch":{"calibration":[1,2,"bad",4,5]}})",
  R"({"device":{"timezone_offset_min":32768}})",
  R"({"sync":{"source":256}})",
};
template <size_t Index> void invalid_type_is_atomic() {
  seed(); const auto before = snapshot(); const auto nvs = host_project::nvs;
  expect(!apply(kWrongTypes[Index]), "invalid JSON type/range must be rejected before conversion");
  expect(snapshot() == before && host_project::nvs == nvs,
         "type validation failure must be atomic");
}

void partial_update_keeps_other_fields() {
  seed(); const Settings before = manager.getAll(); const auto nvs = host_project::nvs;
  require(apply(R"({"device":{"number":255},"wifi":{"active":true}})"), "partial update must validate");
  expect(manager.getDevice().number == 255 && manager.getWifi().active,
         "provided fields must be applied");
  expect(manager.getDevice().name == before.device.name &&
             manager.getDevice().type == before.device.type &&
             manager.getWifi().ssid == before.wifi.ssid &&
             manager.getWifi().passwd == before.wifi.passwd &&
             manager.getSync().ntp1 == before.sync.ntp1 &&
             manager.getGps().enabled == before.gps.enabled &&
             manager.getTouch().enabled == before.touch.enabled,
         "omitted fields and groups must be preserved");
  expect(host_project::nvs == nvs, "fromJson must not persist before explicit save");
  require(manager.save() > 0, "explicit save must persist partial update");
  SettingsManager reboot; require(reboot.begin(), "reboot load must initialize");
  expect(reboot.getDevice().number == 255 && reboot.getWifi().active &&
             reboot.getWifi().passwd == "original-pass", "partial update roundtrip");
  const auto writes = host_project::successful_writes;
  expect(manager.save() == 0 && host_project::successful_writes == writes,
         "unchanged settings must not rewrite NVS");
}

template <bool TouchFailure> void later_group_failure_rolls_back_earlier_groups() {
  seed(); const auto before = snapshot(); const auto nvs = host_project::nvs;
  const auto text = TouchFailure
      ? R"({"device":{"number":22},"sync":{"source":2},"wifi":{"ssid":"Changed"},"touch":{"calibration":[1]}})"
      : R"({"device":{"number":22},"sync":{"ntp1":"invalid/host"},"wifi":{"ssid":"Changed"}})";
  expect(!apply(text), "later group must fail validation");
  expect(snapshot() == before && host_project::nvs == nvs,
         "earlier groups must not be partially applied after later group failure");
}

template <unsigned Field, size_t Length, bool Valid> void string_length_boundary() {
  seed(); JsonDocument request; const std::string value(Length, 'a');
  if constexpr (Field == 0) request["wifi"]["ssid"] = value;
  else if constexpr (Field == 1) request["wifi"]["passwd"] = value;
  else if constexpr (Field == 2) request["sync"]["ntp1"] = value;
  else request["device"]["name"] = value;
  const auto before = snapshot();
  const bool result = manager.fromJson(request);
  expect(result == Valid, "string length boundary acceptance");
  if (!Valid) expect(snapshot() == before, "oversized string must not change configuration");
  else {
    const String &stored = Field == 0 ? manager.getWifi().ssid : Field == 1
        ? manager.getWifi().passwd : Field == 2 ? manager.getSync().ntp1 : manager.getDevice().name;
    expect(stored == value, "accepted string must retain its complete value");
  }
}

template <int Zone> void timezone_boundary() {
  seed(); JsonDocument request; request["device"]["timezone_offset_min"] = Zone;
  expect(manager.fromJson(request) && manager.getDevice().timezone_offset_min == Zone,
         "allowed timezone boundary must be preserved exactly");
}
void empty_and_unknown_update_are_noops() {
  seed(); const auto before = snapshot();
  const auto writes = host_project::successful_writes;
  expect(apply("{}") && apply(R"({"future":{"value":17}})"), "optional fields and future groups");
  expect(snapshot() == before && manager.save() == 0 && host_project::successful_writes == writes,
         "no-op update must preserve configuration without NVS writes");
}
void all_groups_roundtrip() {
  seed();
  require(apply(R"({"device":{"name":"Gate_2-A","number":255,"type":2,"timezone_offset_min":-720},"sync":{"auto":false,"source":2,"ntp1":"pool.ntp.org","ntp2":"","ntp3":""},"wifi":{"active":true,"ssid":"Race","passwd":"new-pass"},"gps":{"enabled":false},"touch":{"enabled":false,"cal_valid":true,"calibration":[100,200,300,400,1]}})"), "full valid update");
  const auto before = snapshot(); require(manager.save() > 0, "full update must persist");
  require(manager.load(), "full update must reload");
  expect(snapshot() == before && manager.getTouch().calibration.data[3] == 400,
         "all configuration groups must survive save/load");
}
void partial_nvs_uses_defaults_for_missing_fields() {
  host_project::nvs["device.name"] = std::string("Gate");
  host_project::nvs["wifi.ssid"] = std::string("Saved");
  require(manager.begin(), "partial persisted configuration must load");
  expect(manager.getDevice().name == "Gate" && manager.getWifi().ssid == "Saved" &&
             manager.getDevice().number == 1 && manager.getSync().auto_sync &&
             manager.getTouch().enabled, "missing persisted keys must use their defaults");
}
void invalid_nvs_resets_configuration() {
  seed(); host_project::nvs["device.type"] = uint8_t(3);
  expect(!manager.load(), "invalid persisted value must be reported");
  expect(manager.getDevice().name == DEFAULT_DEVICE_NAME && manager.getDevice().type == 1 &&
             manager.getWifi().ssid.empty(), "invalid persisted configuration must reset all RAM groups");
}

#define INVALID(index, label) {"settings_reject_" label, invalid_value_is_atomic<index>}
#define TYPE(index, label) {"settings_type_" label, invalid_type_is_atomic<index>, "AU009"}
const Test kTests[] = {
  INVALID(0,"empty_name"), INVALID(1,"long_name"), INVALID(2,"name_spaces"), INVALID(3,"non_ascii_name"),
  INVALID(4,"number_zero"), INVALID(5,"number_256"), INVALID(6,"negative_number"),
  INVALID(7,"role_zero"), INVALID(8,"unknown_role"), INVALID(9,"timezone_below_min"),
  INVALID(10,"timezone_above_max"), INVALID(11,"unknown_sync_source"),
  INVALID(12,"empty_primary_ntp"), INVALID(13,"invalid_ntp_host"),
  INVALID(14,"short_calibration"), INVALID(15,"long_calibration"), INVALID(16,"non_array_calibration"),
  TYPE(0,"sync_auto_string"), TYPE(1,"gps_enabled_number"), TYPE(2,"wifi_active_string"),
  TYPE(3,"touch_enabled_array"), TYPE(4,"calibration_valid_number"), TYPE(5,"device_array"),
  TYPE(6,"wifi_boolean"), TYPE(7,"calibration_string_element"),
  TYPE(8,"timezone_integer_overflow"), TYPE(9,"sync_source_integer_overflow"),
  {"settings_partial_update_preserves_omitted", partial_update_keeps_other_fields},
  {"settings_later_sync_error_rolls_back", later_group_failure_rolls_back_earlier_groups<false>},
  {"settings_later_touch_error_rolls_back", later_group_failure_rolls_back_earlier_groups<true>},
  {"settings_ssid_length_32", string_length_boundary<0,32,true>},
  {"settings_ssid_length_33", string_length_boundary<0,33,false>},
  {"settings_password_length_64", string_length_boundary<1,64,true>},
  {"settings_password_length_65", string_length_boundary<1,65,false>},
  {"settings_ntp_length_64", string_length_boundary<2,64,true>},
  {"settings_ntp_length_65", string_length_boundary<2,65,false>},
  {"settings_name_length_16", string_length_boundary<3,16,true>},
  {"settings_timezone_minimum", timezone_boundary<-720>},
  {"settings_timezone_maximum", timezone_boundary<840>},
  {"settings_empty_unknown_noop", empty_and_unknown_update_are_noops},
  {"settings_all_groups_save_load", all_groups_roundtrip},
  {"settings_partial_nvs_defaults", partial_nvs_uses_defaults_for_missing_fields},
  {"settings_invalid_nvs_resets_all", invalid_nvs_resets_configuration},
};
#undef INVALID
#undef TYPE
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
