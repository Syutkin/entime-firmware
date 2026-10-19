#pragma once
#include <cstddef>
#include "Preferences.h"
using esp_err_t = int;
constexpr int ESP_OK = 0;
using nvs_handle_t = unsigned;
constexpr int NVS_READWRITE = 1;
namespace host_project {
inline bool nvs_open_fails = false;
inline bool nvs_commit_fails = false;
inline unsigned failed_opens = 0, failed_commits = 0, open_handles = 0;
inline nvs_handle_t next_handle = 0;
inline std::unordered_map<nvs_handle_t, std::pair<std::string, std::string>> pending_strings;
}
inline esp_err_t nvs_open(const char *, int, nvs_handle_t *out) {
  if (host_project::nvs_open_fails) {
    ++host_project::failed_opens;
    return 1;
  }
  *out = ++host_project::next_handle;
  ++host_project::open_handles;
  return ESP_OK;
}
inline esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
  if (host_project::write_fails || host_project::failing_key == key) {
    ++host_project::failed_writes;
    return 1;
  }
  host_project::pending_strings[handle] = {key, value};
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t handle) {
  if (host_project::nvs_commit_fails) {
    ++host_project::failed_commits;
    return 1;
  }
  const auto found = host_project::pending_strings.find(handle);
  if (found != host_project::pending_strings.end()) {
    host_project::nvs[found->second.first] = found->second.second;
    ++host_project::successful_writes;
    host_project::pending_strings.erase(found);
  }
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) {
  host_project::pending_strings.erase(handle);
  --host_project::open_handles;
}
struct nvs_stats_t { size_t used_entries=0, free_entries=100, total_entries=100, namespace_count=1; };
inline esp_err_t nvs_get_stats(const char *, nvs_stats_t *out) { *out=nvs_stats_t{}; return 0; }
inline const char *esp_err_to_name(int err) { return err == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
