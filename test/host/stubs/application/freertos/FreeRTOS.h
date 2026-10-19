#pragma once
#include <cstdint>
namespace host_project { inline uint32_t ticks=0; inline int critical_depth=0; }
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE *mux) { ++*mux; ++host_project::critical_depth; }
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { --*mux; --host_project::critical_depth; }
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline uint32_t xTaskGetTickCount() { return host_project::ticks; }
inline void vTaskDelay(uint32_t ticks) { host_project::ticks+=ticks; }
