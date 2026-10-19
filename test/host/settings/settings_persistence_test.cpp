#include "../support/application/environment.h"
#include "../../../src/storage/settings.cpp"

namespace {
void start(SettingsManager &manager, bool custom_name = false) {
  require(manager.begin(), "NVS must initialize");
  if (custom_name) {
    auto device = manager.getDevice();
    device.name = "RaceTimer";
    require(manager.setDevice(device), "custom device name must validate");
  }
}

template <unsigned Group> void default_name_preserves_settings() {
  SettingsManager first;
  start(first);
  if constexpr (Group == 0) {
    auto wifi = first.getWifi(); wifi.active = true; wifi.ssid = "RaceNetwork";
    require(first.setWifi(wifi), "WiFi settings must validate");
  } else if constexpr (Group == 1) {
    auto device = first.getDevice(); device.type = 2; device.number = 17;
    require(first.setDevice(device), "device settings must validate");
  } else if constexpr (Group == 2) {
    auto touch = first.getTouch(); touch.calibration.valid = true;
    touch.calibration.data[0] = 123;
    require(first.setTouch(touch), "calibration must validate");
  } else {
    auto sync = first.getSync(); sync.source = 2; sync.auto_sync = false;
    require(first.setSync(sync), "sync settings must validate");
  }
  require(first.save() > 0, "scenario must actually write non-default settings");
  SettingsManager reboot;
  start(reboot);
  if constexpr (Group == 0)
    expect(reboot.getWifi().active && reboot.getWifi().ssid == "RaceNetwork",
           "saved WiFi must survive reboot with factory device name");
  else if constexpr (Group == 1)
    expect(reboot.getDevice().type == 2 && reboot.getDevice().number == 17,
           "saved role/number must survive reboot with factory name");
  else if constexpr (Group == 2)
    expect(reboot.getTouch().calibration.valid &&
               reboot.getTouch().calibration.data[0] == 123,
           "saved touch calibration must survive reboot");
  else
    expect(reboot.getSync().source == 2 && !reboot.getSync().auto_sync,
           "saved sync policy must survive reboot with factory name");
}

template <unsigned Group> void other_default_name_settings_survive_reboot() {
  SettingsManager first; start(first);
  if constexpr (Group == 0) {
    auto gps = first.getGps(); gps.enabled = false;
    require(first.setGps(gps), "GPS disable must validate");
  } else if constexpr (Group == 1) {
    auto device = first.getDevice(); device.timezone_offset_min = -210;
    require(first.setDevice(device), "timezone must validate");
  } else {
    auto wifi = first.getWifi(); wifi.passwd = "RacePassword";
    require(first.setWifi(wifi), "WiFi password must validate");
  }
  require(first.save() > 0, "changed setting must be saved");
  require(host_project::nvs.count("device.name") == 0,
          "unchanged default name must not require a storage marker");
  const auto writes = host_project::successful_writes;
  SettingsManager reboot; start(reboot);
  if constexpr (Group == 0)
    expect(!reboot.getGps().enabled, "saved GPS disable must survive reboot");
  else if constexpr (Group == 1)
    expect(reboot.getDevice().timezone_offset_min == -210,
           "saved timezone must survive reboot");
  else
    expect(reboot.getWifi().passwd == "RacePassword",
           "saved password must survive reboot");
  expect(reboot.save() == 0 && host_project::successful_writes == writes,
         "loading and saving unchanged settings must not rewrite NVS");
}

void sparse_legacy_storage_without_device_name() {
  // Storage produced by earlier firmware: only non-default keys, no name marker.
  host_project::nvs["wifi.ssid"] = std::string("LegacyNetwork");
  host_project::nvs["device.number"] = uint8_t{255};
  host_project::nvs["touch.cal4"] = uint16_t{1234};
  SettingsManager manager; start(manager);
  expect(manager.getWifi().ssid == "LegacyNetwork" &&
             manager.getDevice().number == 255 &&
             manager.getTouch().calibration.data[4] == 1234,
         "legacy sparse settings must load without migration or name marker");
  expect(manager.getDevice().name == DEFAULT_DEVICE_NAME &&
             manager.getDevice().type == DEFAULT_DEVICE_TYPE &&
             manager.getSync().auto_sync == DEFAULT_SYNC_AUTO &&
             manager.getGps().enabled == DEFAULT_GPS_ENABLED &&
             manager.getTouch().calibration.data[0] == DEFAULT_TOUCH_CAL0,
         "missing keys must independently use their defaults");
  expect(host_project::successful_writes == 0 && manager.save() == 0,
         "reading legacy settings must not create or rewrite keys");
}

void empty_storage_uses_defaults_without_writes() {
  SettingsManager manager; start(manager);
  const auto snapshot = manager.toJson();
  auto device = manager.getDevice(); device.name = "Temporary";
  require(manager.setDevice(device), "temporary RAM setting must validate");
  expect(manager.load(), "empty NVS reload must succeed");
  expect(manager.toJson() == snapshot,
         "empty storage must restore all defaults, including changed RAM values");
  expect(manager.save() == 0 && host_project::nvs.empty() &&
             host_project::successful_writes == 0,
         "defaults must not require any NVS writes or sentinel key");
}

void custom_name_roundtrip() {
  SettingsManager first; start(first, true);
  auto wifi = first.getWifi(); wifi.ssid = "RaceNetwork";
  require(first.setWifi(wifi), "WiFi settings must validate");
  require(first.save() > 0, "first save must write settings");
  SettingsManager reboot; start(reboot);
  expect(reboot.getDevice().name == "RaceTimer" &&
             reboot.getWifi().ssid == "RaceNetwork", "custom-name roundtrip");
  expect(reboot.save() == 0, "unchanged settings must not rewrite NVS");
}

template <unsigned Type> void write_failure_is_reported() {
  SettingsManager manager; start(manager, true);
  require(manager.save() > 0, "seed a valid configuration before the failure");
  if constexpr (Type == 0) {
    auto wifi = manager.getWifi(); wifi.ssid = "Changed";
    require(manager.setWifi(wifi), "string change must validate");
    host_project::failing_key = "wifi.ssid";
  } else if constexpr (Type == 1) {
    auto gps = manager.getGps(); gps.enabled = false;
    require(manager.setGps(gps), "boolean change must validate");
    host_project::failing_key = "gps.enabled";
  } else if constexpr (Type == 2) {
    auto device = manager.getDevice(); device.number = 2;
    require(manager.setDevice(device), "uint8 change must validate");
    host_project::failing_key = "device.number";
  } else if constexpr (Type == 3) {
    auto device = manager.getDevice(); device.timezone_offset_min = -60;
    require(manager.setDevice(device), "int16 change must validate");
    host_project::failing_key = "device.tz_min";
  } else {
    auto touch = manager.getTouch(); touch.calibration.valid = true;
    touch.calibration.data[0] = 500;
    require(manager.setTouch(touch), "uint16 change must validate");
    host_project::failing_key = "touch.cal0";
  }
  const int result = manager.save();
  require(host_project::failed_writes > 0, "the selected NVS write must actually fail");
  expect(result < 0,
         "AU004: a failed NVS write must propagate as save failure");
}

void failed_clear_is_reported() {
  SettingsManager manager; start(manager, true);
  require(manager.save() > 0, "seed a saved configuration");
  host_project::clear_fails = true;
  const bool result = manager.factoryReset();
  require(host_project::failed_clears > 0, "NVS clear fault must actually be reached");
  expect(!result, "AU004: failed NVS clear must fail factory reset");
}

void factory_reset_clears_saved_values() {
  SettingsManager manager; start(manager, true);
  require(manager.save() > 0, "seed a saved configuration");
  expect(manager.factoryReset(), "healthy NVS reset must succeed");
  SettingsManager reboot; start(reboot);
  expect(reboot.getDevice().name == DEFAULT_DEVICE_NAME,
         "factory reset must survive reboot");
}

const Test kTests[] = {
  {"default_name_persists_gps_disable", other_default_name_settings_survive_reboot<0>},
  {"default_name_persists_timezone", other_default_name_settings_survive_reboot<1>},
  {"default_name_persists_wifi_password", other_default_name_settings_survive_reboot<2>},
  {"settings_sparse_legacy_storage_without_name", sparse_legacy_storage_without_device_name},
  {"settings_empty_storage_defaults_without_writes", empty_storage_uses_defaults_without_writes},
  {"default_name_persists_wifi", default_name_preserves_settings<0>},
  {"default_name_persists_device_role", default_name_preserves_settings<1>},
  {"default_name_persists_touch_calibration", default_name_preserves_settings<2>},
  {"default_name_persists_sync_policy", default_name_preserves_settings<3>},
  {"settings_custom_name_roundtrip", custom_name_roundtrip},
  {"nvs_string_write_failure", write_failure_is_reported<0>, "AU004"},
  {"nvs_bool_write_failure", write_failure_is_reported<1>, "AU004"},
  {"nvs_uint8_write_failure", write_failure_is_reported<2>, "AU004"},
  {"nvs_int16_write_failure", write_failure_is_reported<3>, "AU004"},
  {"nvs_uint16_write_failure", write_failure_is_reported<4>, "AU004"},
  {"nvs_factory_reset_clear_failure", failed_clear_is_reported, "AU004"},
  {"nvs_factory_reset_roundtrip", factory_reset_clears_saved_values},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
