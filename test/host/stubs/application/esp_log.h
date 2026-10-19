#pragma once
#include "../base/esp_log.h"
#include <sstream>
#include <string>
#include <vector>

namespace host_project {
inline std::vector<std::string> logs;

// Capture every argument at every level, including raw buffers passed to DEBUG
// logs. Checking just the INFO message could miss a second password exposure.
template <typename... Args>
void capture_log(const char *tag, const char *format, Args... args) {
  std::ostringstream message;
  message << tag << ' ' << format;
  ((message << ' ' << args), ...);
  logs.push_back(message.str());
}
} // namespace host_project

#undef ESP_LOGI
#undef ESP_LOGW
#undef ESP_LOGD
#undef ESP_LOGV
#undef ESP_LOGE
#define ESP_LOGI(...) host_project::capture_log(__VA_ARGS__)
#define ESP_LOGW(...) host_project::capture_log(__VA_ARGS__)
#define ESP_LOGD(...) host_project::capture_log(__VA_ARGS__)
#define ESP_LOGV(...) host_project::capture_log(__VA_ARGS__)
#define ESP_LOGE(...) host_project::capture_log(__VA_ARGS__)
