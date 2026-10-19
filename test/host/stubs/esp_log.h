#pragma once

#include <cstdlib>

// Keep log arguments type-checked without producing firmware logs in tests.
template <typename... Args>
inline void host_log(const char *, const char *, Args...) {}

#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGD(...) host_log(__VA_ARGS__)
#define ESP_LOGV(...) host_log(__VA_ARGS__)
