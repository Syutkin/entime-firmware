#ifndef HAL_TOUCH_H
#define HAL_TOUCH_H

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "storage/settings.h"

enum class TouchEventType : uint8_t {
  Press = 0,
  Move = 1,
  Release = 2,
};

struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
  uint32_t ts_ms = 0;
};

struct TouchEvent {
  TouchEventType type = TouchEventType::Press;
  TouchPoint point;
};

class Touch {
public:
  void begin(TFT_eSPI &tft, const TouchSettings &settings, uint8_t rotation);
  void update();

  bool pollEvent(TouchEvent &out);
  bool isPressed() const;

  void setRotation(uint8_t rotation);
  void applySettings(const TouchSettings &settings);
  bool applyCalibration(const TouchCalibration &cal);
  bool calibrate(TouchCalibration &outCalibration);

  bool isReady() const { return _initialized; }
  bool isCalibrated() const { return _calibrated; }

private:
  enum class State : uint8_t {
    Idle = 0,
    PressCandidate = 1,
    Pressed = 2,
    ReleaseCandidate = 3,
  };

  static constexpr uint8_t QUEUE_CAPACITY = 8;
  static constexpr uint32_t POLL_INTERVAL_MS = 16;   // ~60 Hz
  static constexpr uint32_t DEBOUNCE_DOWN_MS = 20;
  static constexpr uint32_t DEBOUNCE_UP_MS = 30;
  static constexpr uint16_t MOVE_THRESHOLD_PX = 3;

  TFT_eSPI *_tft = nullptr;

  bool _initialized = false;
  bool _enabled = false;
  bool _calibrated = false;
  TouchCalibration _calibration;
  uint8_t _rotation = 0;

  State _state = State::Idle;
  uint32_t _lastPollMs = 0;
  uint32_t _stateTsMs = 0;

  TouchPoint _candidatePoint;
  TouchPoint _lastStablePoint;
  TouchPoint _lastRawPoint;

  TouchEvent _queue[QUEUE_CAPACITY];
  uint8_t _queueHead = 0;
  uint8_t _queueTail = 0;
  uint8_t _queueCount = 0;

  bool readRaw_(TouchPoint &point);
  void logPress_(const TouchPoint &point) const;
  void pushEvent_(TouchEventType type, const TouchPoint &point);
  bool dropOldestMove_();
  bool movedEnough_(const TouchPoint &from, const TouchPoint &to) const;
  void resetState_();
};

extern Touch touch;

#endif // HAL_TOUCH_H
