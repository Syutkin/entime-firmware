#pragma once
#include <cstddef>
using esp_err_t = int;
constexpr int ESP_OK = 0;
struct nvs_stats_t { size_t used_entries=0, free_entries=100, total_entries=100, namespace_count=1; };
inline esp_err_t nvs_get_stats(const char *, nvs_stats_t *out) { *out=nvs_stats_t{}; return 0; }
inline const char *esp_err_to_name(int) { return "ESP_OK"; }
