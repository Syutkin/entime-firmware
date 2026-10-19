#include "../support/application/environment.h"
#include "app/touch_calibration_service.h"
#include "hal/touch/touch.h"
#include "hal/tft/tft.h"

Display display;
namespace {
bool wizard_ok=true;
unsigned wizard_calls=0;
}
bool runTouchCalibrationScreen(TFT_eSPI &,Touch &,TouchCalibration &out) {
  ++wizard_calls;
  if(!wizard_ok) return false;
  out.valid=true; const uint16_t values[]={200,300,400,500,1};
  for(unsigned i=0;i<5;++i) out.data[i]=values[i];
  return true;
}
namespace {
void setup(bool storage=true) {
  if(storage) {
    require(settings.begin(),"storage must initialize");
    auto device=settings.getDevice(); device.name="RaceTimer";
    require(settings.setDevice(device),"seed name must validate");
    require(settings.save()>0,"seed must persist");
  }
  TouchSettings cfg; cfg.calibration.valid=true; cfg.calibration.data[0]=100;
  touch.begin(display.tft(),cfg,2);
  display.tft().use_wizard_result=true; display.tft().wizard_result={200,300,400,500,1};
}
void unavailable_touch_stops_before_wizard() {
  auto result=runTouchCalibrationFlow(true);
  expect(result.status==TouchCalibrationFlowStatus::TouchNotReady && wizard_calls==0 &&
             !consumeTouchCalibrationUiRedrawRequest(),"uninitialized touch must stop before UI/storage");
}
void wizard_failure_preserves_calibration() {
  setup(); wizard_ok=false; const auto writes=host_project::successful_writes;
  const auto applied=display.tft().applied_calls;
  auto result=runTouchCalibrationFlow(true);
  expect(result.status==TouchCalibrationFlowStatus::WizardFailed &&
             host_project::successful_writes==writes && display.tft().applied_calls==applied &&
             consumeTouchCalibrationUiRedrawRequest() && !consumeTouchCalibrationUiRedrawRequest(),
         "failed wizard must not save/apply and must request exactly one UI redraw");
}
template <bool Interactive> void calibration_save_apply_roundtrip() {
  setup(); auto result=runTouchCalibrationFlow(Interactive);
  expect(result.status==TouchCalibrationFlowStatus::Ok && result.savedKeys>0 &&
             result.calibration.valid && display.tft().calibration==std::array<uint16_t,5>{200,300,400,500,1} &&
             touch.isCalibrated(),"calibration flow must save before applying complete calibration");
  SettingsManager reboot; require(reboot.begin(),"calibration must load after reboot");
  expect(reboot.getTouch().calibration.valid && reboot.getTouch().calibration.data[0]==200 &&
             reboot.getTouch().calibration.data[4]==1,"calibration must survive reboot");
  expect(wizard_calls==(Interactive?1U:0U) && consumeTouchCalibrationUiRedrawRequest()==Interactive &&
             !consumeTouchCalibrationUiRedrawRequest(),"only interactive flow must request one UI redraw");
}
void unavailable_storage_stops_before_apply() {
  setup(false); const auto applied=display.tft().applied_calls;
  auto result=runTouchCalibrationFlow(true);
  expect(result.status==TouchCalibrationFlowStatus::SaveFailed && display.tft().applied_calls==applied,
         "unavailable NVS must stop flow before applying new calibration");
}
void failed_write_stops_before_apply() {
  setup(); host_project::write_fails=true; const auto applied=display.tft().applied_calls;
  auto result=runTouchCalibrationFlow(true);
  require(host_project::failed_writes>0,"NVS write fault must actually occur");
  expect(result.status==TouchCalibrationFlowStatus::SaveFailed && display.tft().applied_calls==applied,
         "NVS failure must not be reported as calibrated/saved success");
}
const Test kTests[] = {
  {"touch_flow_not_ready",unavailable_touch_stops_before_wizard},
  {"touch_flow_wizard_failure",wizard_failure_preserves_calibration},
  {"touch_flow_interactive_save_apply_reboot",calibration_save_apply_roundtrip<true>},
  {"touch_flow_noninteractive_save_apply_reboot",calibration_save_apply_roundtrip<false>},
  {"touch_flow_storage_unavailable",unavailable_storage_stops_before_apply},
  {"touch_flow_storage_write_failure",failed_write_stops_before_apply,"AU004"},
};
}
int main(int argc,char **argv) { return run_tests(argc,argv,kTests); }
