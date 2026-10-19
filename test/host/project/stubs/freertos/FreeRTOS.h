#pragma once
#include <cstdint>
namespace host_project { inline uint32_t ticks=0; }
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline uint32_t xTaskGetTickCount() { return host_project::ticks; }
inline void vTaskDelay(uint32_t ticks) { host_project::ticks+=ticks; }
