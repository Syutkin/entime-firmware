#include "../support/application/environment.h"
#include "../../../src/hal/touch/touch.h"
#include <vector>

namespace {
TFT_eSPI tft;
void begin(bool enabled=true, bool calibrated=true) {
  host_project::now_ms=1000;
  TouchSettings cfg; cfg.enabled=enabled; cfg.calibration.valid=calibrated;
  cfg.calibration.data[0]=100;
  touch.begin(tft,cfg,2);
}
void sample(bool down, uint32_t delta=16) {
  tft.pressed=down; host_project::now_ms+=delta; touch.update();
}
void press() { sample(true); sample(true,20); }
void release() { sample(false); sample(false,30); }
std::vector<TouchEvent> drain() {
  std::vector<TouchEvent> events; TouchEvent e;
  while(touch.pollEvent(e)) events.push_back(e);
  return events;
}
void uninitialized_is_inert() {
  tft.pressed=true; touch.update(); TouchCalibration out; out.data[0]=999;
  expect(!touch.isReady() && !touch.isPressed() && !touch.isCalibrated() &&
             drain().empty() && !touch.calibrate(out) && out.data[0]==999 && tft.reads==0,
         "uninitialized touch must not read, publish or change calibration output");
}
template <bool Enabled,bool Calibrated> void input_preconditions() {
  begin(Enabled,Calibrated); press();
  expect(drain().empty() && tft.reads==0 && !touch.isPressed(),
         "disabled or uncalibrated input must not poll touch hardware");
}
void poll_interval() {
  begin(); sample(false,16); const auto reads=tft.reads;
  sample(true,15); expect(tft.reads==reads,"poll before 16 ms must not read hardware");
  sample(true,1); expect(tft.reads==reads+1,"poll exactly at 16 ms must read hardware");
}
template <uint32_t Elapsed,bool Accepted> void press_debounce() {
  begin(); sample(true); sample(true,Elapsed);
  const auto events=drain();
  expect(events.size()==(Accepted?1U:0U) && touch.isPressed()==Accepted,
         "press debounce must accept exactly at 20 ms");
  if(Accepted) expect(events[0].type==TouchEventType::Press &&
      events[0].point.ts_ms==host_project::now_ms,"press must retain acceptance timestamp");
}
template <uint32_t Elapsed,bool Accepted> void release_debounce() {
  begin(); press(); drain(); sample(false); sample(false,Elapsed);
  const auto events=drain();
  expect(events.size()==(Accepted?1U:0U) && touch.isPressed()!=Accepted,
         "release debounce must accept exactly at 30 ms");
  if(Accepted) expect(events[0].type==TouchEventType::Release,"release must be emitted once");
}
void short_press_bounce() {
  begin(); sample(true); sample(false);
  expect(drain().empty() && !touch.isPressed(),"short down bounce must not create a press/release pair");
}
void release_bounce_preserves_hold() {
  begin(); press(); drain(); sample(false); sample(true);
  expect(touch.isPressed() && drain().empty(),"short up bounce must not end the held press");
  release(); auto events=drain();
  expect(events.size()==1 && events[0].type==TouchEventType::Release,"stable release after bounce must occur once");
}
void held_press_not_repeated() {
  begin(); press();
  for(unsigned i=0;i<100;++i) sample(true);
  auto events=drain(); expect(events.size()==1 && events[0].type==TouchEventType::Press,
                            "holding without movement must not repeat press events");
}
template <int Dx,int Dy,bool Moved> void move_threshold() {
  begin(); press(); drain(); tft.x+=Dx; tft.y+=Dy; sample(true);
  const auto events=drain();
  expect(events.size()==(Moved?1U:0U),"movement threshold must be 3 pixels on either axis");
  if(Moved) expect(events[0].type==TouchEventType::Move && events[0].point.x==tft.x &&
                  events[0].point.y==tft.y,"move must preserve new coordinates");
}
void queue_prioritizes_press_and_release() {
  begin(); press(); for(unsigned i=1;i<=9;++i) { tft.x=100+i*3; sample(true); }
  release(); const auto events=drain();
  expect(events.size()==8 && events.front().type==TouchEventType::Press &&
             events.back().type==TouchEventType::Release && events.back().point.x==127,
         "full move queue must preserve press/release and latest release position");
  expect(events[1].point.x==106,"release must evict the oldest queued Move, preserving order");
}
void queue_without_moves_drops_oldest_pair() {
  begin(); for(unsigned i=0;i<5;++i) { tft.x=100+i*10; press(); release(); }
  const auto events=drain();
  expect(events.size()==8 && events.front().point.x==110 && events.back().point.x==140,
         "bounded queue without moves must retain the newest events");
  for(unsigned i=0;i<events.size();++i)
    expect(events[i].type==(i%2==0?TouchEventType::Press:TouchEventType::Release),
           "retained tap events must preserve FIFO order");
}
void ring_wraparound_keeps_fifo() {
  begin(); for(unsigned i=0;i<100;++i) {
    tft.x=100+i; press(); release(); auto events=drain();
    expect(events.size()==2 && events[0].point.x==100+i &&
               events[0].type==TouchEventType::Press && events[1].type==TouchEventType::Release,
           "ring head/tail wrap must not duplicate or reorder events");
  }
}
void calibration_resets_pending_events() {
  begin(); press(); TouchCalibration invalid;
  expect(!touch.applyCalibration(invalid) && !touch.isCalibrated() && !touch.isPressed() && drain().empty(),
         "invalid calibration must clear current gesture and queued events");
  TouchCalibration valid; valid.valid=true; valid.data[0]=200;
  expect(touch.applyCalibration(valid) && tft.calibration[0]==200,"new calibration must reach hardware");
  press(); expect(drain().size()==1,"input must recover after applying valid calibration");
}
void rebegin_clears_state() {
  begin(); press(); TouchSettings disabled; disabled.enabled=false;
  touch.begin(tft,disabled,0); sample(true);
  expect(drain().empty() && !touch.isPressed(),"reinitialization must discard old gesture and queue");
}
void rollover_during_gesture() {
  begin(); host_project::now_ms=UINT32_MAX-20; press(); release();
  const auto events=drain();
  expect(events.size()==2 && events[0].type==TouchEventType::Press &&
             events[1].type==TouchEventType::Release && !touch.isPressed(),
         "millis rollover must not break press/release debounce");
}
template <bool Success> void wizard_result() {
  begin(); const auto applied=tft.applied_calls;
  tft.use_wizard_result=true;
  if(Success) tft.wizard_result={200,300,400,500,1};
  TouchCalibration out; out.data[0]=999;
  expect(touch.calibrate(out)==Success && tft.calibration_calls==1 && tft.applied_calls==applied,
         "calibrate must return wizard output without applying it prematurely");
  if(Success) expect(out.valid && out.data[0]==200 && out.data[4]==1,"valid wizard data must be copied");
  else expect(!out.valid && out.data[0]==999,"all-zero wizard result must not overwrite output");
}

const Test kTests[] = {
  {"touch_driver_uninitialized",uninitialized_is_inert},
  {"touch_driver_disabled",input_preconditions<false,true>},
  {"touch_driver_uncalibrated",input_preconditions<true,false>},
  {"touch_driver_poll_interval",poll_interval},
  {"touch_driver_down_before_boundary",press_debounce<19,false>},
  {"touch_driver_down_exact_boundary",press_debounce<20,true>},
  {"touch_driver_down_after_boundary",press_debounce<21,true>},
  {"touch_driver_up_before_boundary",release_debounce<29,false>},
  {"touch_driver_up_exact_boundary",release_debounce<30,true>},
  {"touch_driver_up_after_boundary",release_debounce<31,true>},
  {"touch_driver_down_bounce",short_press_bounce},
  {"touch_driver_up_bounce",release_bounce_preserves_hold},
  {"touch_driver_hold_no_repeat",held_press_not_repeated},
  {"touch_driver_move_under_threshold",move_threshold<2,2,false>},
  {"touch_driver_move_x_positive",move_threshold<3,0,true>},
  {"touch_driver_move_x_negative",move_threshold<-3,0,true>},
  {"touch_driver_move_y_positive",move_threshold<0,3,true>},
  {"touch_driver_move_y_negative",move_threshold<0,-3,true>},
  {"touch_driver_queue_preserves_release",queue_prioritizes_press_and_release},
  {"touch_driver_queue_without_moves",queue_without_moves_drops_oldest_pair},
  {"touch_driver_ring_wraparound",ring_wraparound_keeps_fifo},
  {"touch_driver_apply_calibration_resets",calibration_resets_pending_events},
  {"touch_driver_rebegin_resets",rebegin_clears_state},
  {"touch_driver_millis_rollover",rollover_during_gesture},
  {"touch_driver_wizard_success",wizard_result<true>},
  {"touch_driver_wizard_empty",wizard_result<false>},
};
}
int main(int argc,char **argv) { return run_tests(argc,argv,kTests); }
