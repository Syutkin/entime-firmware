#include "../support/application/environment.h"
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
  expect(!generate_press(tft), "disabled touch must stop generating presses");
}

void touch_enable_applies_immediately() {
  setup(false); TFT_eSPI tft;
  touch.begin(tft, settings.getTouch(), 0);
  auto response = save(R"({"data":{"touch":{"enabled":true}}})");
  require(response["status"] == "ok" && settings.getTouch().enabled,
          "enable command must be accepted");
  expect(generate_press(tft), "enabled touch must start generating presses");
}

void calibration_applies_immediately() {
  setup(); TFT_eSPI tft;
  touch.begin(tft, settings.getTouch(), 0);
  auto response = save(R"({"data":{"touch":{"calibration":[200,300,400,500,1]}}})");
  require(response["status"] == "ok", "calibration command must be accepted");
  expect(tft.calibration == std::array<uint16_t, 5>{200,300,400,500,1},
         "accepted calibration must reach the active touch driver");
}

void disabling_touch_discards_pending_gesture() {
  setup(); TFT_eSPI tft; touch.begin(tft, settings.getTouch(), 0);
  tft.pressed = true;
  for (unsigned i = 0; i < 4; ++i) { host_project::now_ms += 16; touch.update(); }
  require(touch.isPressed(), "touch must hold a queued Press before disabling");
  auto response = save(R"({"data":{"touch":{"enabled":false}}})");
  TouchEvent event;
  expect(response["status"] == "ok" && !touch.isPressed() && !touch.pollEvent(event),
         "disable must clear the active gesture and queued events immediately");
  const auto reads = tft.reads;
  expect(!generate_press(tft) && tft.reads == reads,
         "disabled touch must stop reading hardware");
}

void unrelated_and_noop_save_preserve_active_touch() {
  setup(); TFT_eSPI tft; touch.begin(tft, settings.getTouch(), 0);
  require(generate_press(tft), "touch must be pressed before unrelated save");
  const auto applied = tft.applied_calls;
  auto response = save(R"({"data":{"device":{"timezone_offset_min":0}}})");
  expect(response["status"] == "ok" && touch.isPressed() && tft.applied_calls == applied,
         "unrelated settings must not interrupt touch or reapply calibration");
  response = save(R"({"data":{}})");
  expect(response["saved_keys"] == 0 && touch.isPressed() &&
             tft.applied_calls == applied && !generate_press(tft),
         "no-op save must preserve a held gesture without another Press");
}

void clearing_calibration_validity_blocks_touch() {
  setup(); TFT_eSPI tft; touch.begin(tft, settings.getTouch(), 0);
  auto response = save(R"({"data":{"touch":{"cal_valid":false}}})");
  expect(response["status"] == "ok" && response["reboot_needed"] == false &&
             !touch.isCalibrated() && !generate_press(tft),
         "invalidated calibration must block input immediately without reboot");
}

void restoring_calibration_validity_restores_touch() {
  setup(); TFT_eSPI tft; touch.begin(tft, settings.getTouch(), 0);
  require(save(R"({"data":{"touch":{"cal_valid":false}}})")["status"] == "ok",
          "calibration must first be invalidated");
  auto response = save(R"({"data":{"touch":{"cal_valid":true}}})");
  expect(response["status"] == "ok" && touch.isCalibrated() && generate_press(tft),
         "restoring calibration validity must unblock input immediately");
}

template <bool Calibration> void failed_touch_save_does_not_apply_and_can_retry() {
  setup(); TFT_eSPI tft; touch.begin(tft, settings.getTouch(), 0);
  const auto applied = tft.applied_calls;
  const char *json = Calibration
      ? R"({"data":{"touch":{"calibration":[200,300,400,500,1]}}})"
      : R"({"data":{"touch":{"enabled":false}}})";
  host_project::write_fails = true;
  auto response = save(json);
  require(host_project::failed_writes > 0, "touch NVS write must actually fail");
  expect(response["status"] == "error" && tft.applied_calls == applied &&
             tft.calibration[0] == 100 && generate_press(tft),
         "failed save must preserve the active touch configuration");
  host_project::write_fails = false;
  response = save(json);
  expect(response["status"] == "ok", "retry after NVS recovery must succeed");
  if constexpr (Calibration)
    expect(tft.calibration == std::array<uint16_t,5>{200,300,400,500,1} &&
               tft.applied_calls == applied + 1,
           "retry must apply calibration even if settings RAM was updated earlier");
  else
    expect(!touch.isPressed() && !generate_press(tft),
           "retry must disable touch even if settings RAM was updated earlier");
}

void touch_settings_before_driver_initialization() {
  setup();
  auto response = save(R"({"data":{"touch":{"calibration":[200,300,400,500,1]}}})");
  expect(response["status"] == "ok" && !touch.isReady(),
         "saving settings must not initialize an absent touch driver");
  TFT_eSPI tft; touch.begin(tft, settings.getTouch(), 0);
  expect(tft.calibration == std::array<uint16_t,5>{200,300,400,500,1} && generate_press(tft),
         "later initialization must use the saved configuration");
}

void enabling_touch_applies_new_calibration_together() {
  setup(false); TFT_eSPI tft; touch.begin(tft, settings.getTouch(), 0);
  auto response = save(R"({"data":{"touch":{"enabled":true,"calibration":[200,300,400,500,1]}}})");
  expect(response["status"] == "ok" && response["reboot_needed"] == false &&
             tft.calibration == std::array<uint16_t,5>{200,300,400,500,1} && generate_press(tft),
         "enable and calibration must both take effect in the same command");
}

void storage_failure_returns_error() {
  setup(); host_project::write_fails = true;
  auto response = save(R"({"data":{"device":{"number":2}}})");
  require(host_project::failed_writes > 0, "NVS failure must actually occur in save_config");
  expect(response["status"] == "error" && response["error_code"] == 202,
         "save_config must report storage failure to the client");
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
         "failed reset must report error without scheduling reboot");
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
  {"save_config_disable_discards_touch_gesture", disabling_touch_discards_pending_gesture},
  {"save_config_noop_preserves_active_touch", unrelated_and_noop_save_preserve_active_touch},
  {"save_config_invalid_calibration_blocks_touch", clearing_calibration_validity_blocks_touch},
  {"save_config_restore_calibration_restores_touch", restoring_calibration_validity_restores_touch},
  {"save_config_touch_disable_failure_and_retry", failed_touch_save_does_not_apply_and_can_retry<false>},
  {"save_config_touch_calibration_failure_and_retry", failed_touch_save_does_not_apply_and_can_retry<true>},
  {"save_config_touch_before_driver_init", touch_settings_before_driver_initialization},
  {"save_config_touch_enable_with_new_calibration", enabling_touch_applies_new_calibration_together},
  {"save_config_disables_touch_immediately", touch_disable_applies_immediately},
  {"save_config_enables_touch_immediately", touch_enable_applies_immediately},
  {"save_config_applies_touch_calibration", calibration_applies_immediately},
  {"save_config_reports_nvs_failure", storage_failure_returns_error},
  {"factory_reset_failure_does_not_reboot", failed_reset_does_not_schedule_reboot},
  {"save_config_role_change_requests_reboot", role_change_requests_reboot},
  {"touch_enabled_press_control", enabled_touch_generates_press},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
