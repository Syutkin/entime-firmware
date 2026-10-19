#pragma once
#include "../base/esp_timer.h"
#include "nvs.h"
using esp_timer_handle_t = void *;
struct esp_timer_create_args_t {
  void (*callback)(void *) = nullptr;
  const char *name = nullptr;
};
esp_err_t esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
bool esp_timer_is_active(esp_timer_handle_t);
esp_err_t esp_timer_stop(esp_timer_handle_t);
esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t);
