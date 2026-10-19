#include "support.h"
#include "command/handlers.h"
#include "hal/touch/touch.h"
#include <esp_timer.h>

// No real timer or restart is executed by factory_reset on the host.
static unsigned reset_timers = 0;
esp_err_t esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *out) {
  *out = &reset_timers; return ESP_OK;
}
bool esp_timer_is_active(esp_timer_handle_t) { return false; }
esp_err_t esp_timer_stop(esp_timer_handle_t) { return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t) {
  ++reset_timers; return ESP_OK;
}
void esp_restart() { throw std::runtime_error("unexpected host restart"); }

namespace {
void setup(bool enabled = true) {
  require(settings.begin(), "settings must initialize");
  auto device = settings.getDevice(); device.name = "RaceTimer";
  require(settings.setDevice(device), "name must validate");
  auto value = settings.getTouch(); value.enabled = enabled;
  value.calibration.valid = true;
  value.calibration.data[0] = 100;
  require(settings.setTouch(value), "initial touch settings must validate");
  require(settings.save() > 0, "initial settings must persist");
}

JsonDocument save(const char *json) {
  JsonDocument request;
  require(!deserializeJson(request, json), "test request must be valid JSON");
  host_project::MemoryStream output;
  cmdSaveConfig(request, output);
  JsonDocument response;
  require(!deserializeJson(response, output.output), "handler must return JSON");
  return response;
}

bool generate_press(TFT_eSPI &tft) {
  tft.pressed = true;
  for (unsigned i = 0; i < 4; ++i) {
    host_project::now_ms += 16;
    touch.update();
  }
  TouchEvent event;
  while (touch.pollEvent(event))
    if (event.type == TouchEventType::Press) return true;
  return false;
}

void touch_disable_applies_immediately() {
  setup(); TFT_eSPI tft;
  touch.begin(tft, settings.getTouch(), 0);
  auto response = save(R"({"data":{"touch":{"enabled":false}}})");
  require(response["status"] == "ok" && !settings.getTouch().enabled,
          "disable command must be accepted");
  expect(!generate_press(tft), "AU006: disabled touch must stop generating presses");
}

void touch_enable_applies_immediately() {
  setup(false); TFT_eSPI tft;
  touch.begin(tft, settings.getTouch(), 0);
  auto response = save(R"({"data":{"touch":{"enabled":true}}})");
  require(response["status"] == "ok" && settings.getTouch().enabled,
          "enable command must be accepted");
  expect(generate_press(tft), "AU006: enabled touch must start generating presses");
}

void calibration_applies_immediately() {
  setup(); TFT_eSPI tft;
  touch.begin(tft, settings.getTouch(), 0);
  auto response = save(R"({"data":{"touch":{"calibration":[200,300,400,500,1]}}})");
  require(response["status"] == "ok", "calibration command must be accepted");
  expect(tft.calibration == std::array<uint16_t, 5>{200,300,400,500,1},
         "AU006: accepted calibration must reach the active touch driver");
}

void storage_failure_returns_error() {
  setup(); host_project::write_fails = true;
  auto response = save(R"({"data":{"device":{"number":2}}})");
  require(host_project::failed_writes > 0, "NVS failure must actually occur in save_config");
  expect(response["status"] == "error" && response["error_code"] == 202,
         "AU004: save_config must report storage failure to the client");
}

void failed_reset_does_not_schedule_reboot() {
  setup(); host_project::clear_fails = true;
  JsonDocument request;
  host_project::MemoryStream output;
  cmdFactoryReset(request, output);
  JsonDocument response;
  require(!deserializeJson(response, output.output), "reset must respond with JSON");
  require(host_project::failed_clears > 0, "NVS clear failure must actually occur");
  expect(response["status"] == "error" && reset_timers == 0,
         "AU004: failed reset must report error without scheduling reboot");
}

void role_change_requests_reboot() {
  setup();
  auto response = save(R"({"data":{"device":{"type":2}}})");
  expect(response["status"] == "ok" && response["reboot_needed"] == true,
         "role change must report that reboot is needed");
}

void enabled_touch_generates_press() {
  setup(); TFT_eSPI tft;
  touch.begin(tft, settings.getTouch(), 0);
  expect(generate_press(tft), "healthy enabled/calibrated touch must emit a press");
}

const Test kTests[] = {
  {"save_config_disables_touch_immediately", touch_disable_applies_immediately, "AU006"},
  {"save_config_enables_touch_immediately", touch_enable_applies_immediately, "AU006"},
  {"save_config_applies_touch_calibration", calibration_applies_immediately, "AU006"},
  {"save_config_reports_nvs_failure", storage_failure_returns_error, "AU004"},
  {"factory_reset_failure_does_not_reboot", failed_reset_does_not_schedule_reboot, "AU004"},
  {"save_config_role_change_requests_reboot", role_change_requests_reboot},
  {"touch_enabled_press_control", enabled_touch_generates_press},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
