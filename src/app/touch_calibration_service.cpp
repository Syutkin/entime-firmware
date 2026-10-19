#include "touch_calibration_service.h"

#include "hal/tft/tft.h"
#include "hal/touch/touch.h"
#include "storage/settings.h"
#include "ui/touch_calibration_screen.h"

static bool s_uiRedrawPending = false;

const char *touchCalibrationFlowStatusText(
    TouchCalibrationFlowStatus status) {
  switch (status) {
  case TouchCalibrationFlowStatus::TouchNotReady:
    return "Touch not ready";
  case TouchCalibrationFlowStatus::WizardFailed:
    return "Wizard failed";
  case TouchCalibrationFlowStatus::InvalidSettings:
    return "Invalid settings";
  case TouchCalibrationFlowStatus::SaveFailed:
    return "Save failed";
  case TouchCalibrationFlowStatus::ApplyFailed:
    return "Apply failed";
  case TouchCalibrationFlowStatus::Ok:
  default:
    return "OK";
  }
}

TouchCalibrationFlowResult runTouchCalibrationFlow(bool interactiveUi) {
  TouchCalibrationFlowResult result;

  if (!touch.isReady()) {
    result.status = TouchCalibrationFlowStatus::TouchNotReady;
    return result;
  }

  TouchCalibration calibration;
  bool calibrated = false;
  if (interactiveUi) {
    calibrated = runTouchCalibrationScreen(display.tft(), touch, calibration);
    s_uiRedrawPending = true;
  } else {
    calibrated = touch.calibrate(calibration);
  }

  if (!calibrated) {
    result.status = TouchCalibrationFlowStatus::WizardFailed;
    return result;
  }

  const TouchSettings previousSettings = settings.getTouch();
  TouchSettings touchSettings = previousSettings;
  touchSettings.calibration = calibration;
  if (!settings.setTouch(touchSettings)) {
    result.status = TouchCalibrationFlowStatus::InvalidSettings;
    return result;
  }

  int savedKeys = settings.save();
  if (savedKeys < 0) {
    settings.setTouch(previousSettings);
    result.status = TouchCalibrationFlowStatus::SaveFailed;
    return result;
  }

  if (!touch.applyCalibration(touchSettings.calibration)) {
    result.status = TouchCalibrationFlowStatus::ApplyFailed;
    return result;
  }

  result.status = TouchCalibrationFlowStatus::Ok;
  result.savedKeys = savedKeys;
  result.calibration = touchSettings.calibration;
  return result;
}

bool consumeTouchCalibrationUiRedrawRequest() {
  bool result = s_uiRedrawPending;
  s_uiRedrawPending = false;
  return result;
}
