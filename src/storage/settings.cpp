#include "settings.h"
#include "esp_log.h"
#include "nvs.h"
#include <ArduinoJson.h>
#include <cctype>
#include <cmath>
#include <initializer_list>

static const char *TAG = "Settings";
static constexpr const char *kSettingsNamespace = "entime";

// ============================================================================
// NVS change logging (единый формат)
// ============================================================================

#define SETTINGS_LOG_NVS_FMT(key, fmt, ...)                                    \
  ESP_LOGD(TAG, "NVS: %-28s = " fmt, key, ##__VA_ARGS__)

#define SETTINGS_LOG_NVS_SECRET(key) ESP_LOGD(TAG, "NVS: %-28s = <secret>", key)

#define SETTINGS_LOG_NVS_SKIP(key) ESP_LOGV(TAG, "NVS: %-28s (unchanged)", key)

// Глобальный экземпляр
SettingsManager settings;

// ============================================================================
// Валидация
// ============================================================================

bool SettingsManager::isValidAsciiName(const String &name) const {
  if (name.length() == 0 || name.length() > 16) {
    return false;
  }

  for (uint16_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    // важно: cast в unsigned char, иначе возможен UB на signed char
    if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

bool SettingsManager::isValidDeviceType(uint8_t type) const {
  return type == 1 || type == 2;
}

bool SettingsManager::isValidSyncSource(uint8_t source) const {
  return source == 0 || source == 1 || source == 2;
}

bool SettingsManager::isValidNtpHost(const String &host, bool allowEmpty) const {
  if (host.length() == 0) {
    return allowEmpty;
  }
  if (host.length() > 64) {
    return false;
  }

  for (uint16_t i = 0; i < host.length(); i++) {
    char c = host.charAt(i);
    if (!std::isalnum((unsigned char)c) && c != '-' && c != '.') {
      return false;
    }
  }
  return true;
}

bool SettingsManager::validateDevice(const DeviceSettings &device) const {
  if (!isValidAsciiName(device.name)) {
    ESP_LOGE(TAG, "Invalid device.name: must be 1-16 ASCII characters (a-z, "
                  "A-Z, 0-9, -, _)");
    return false;
  }

  if (device.number == 0) {
    ESP_LOGE(TAG, "Invalid device.number: must be 1-255, got %u",
             device.number);
    return false;
  }

  if (!isValidDeviceType(device.type)) {
    ESP_LOGE(TAG,
             "Invalid device.type: must be 1 (start) or 2 (finish), got %u",
             device.type);
    return false;
  }

  if (device.timezone_offset_min < -720 ||
      device.timezone_offset_min > 840) {
    ESP_LOGE(TAG,
             "Invalid device.timezone_offset_min: must be -720 to 840, got %d",
             device.timezone_offset_min);
    return false;
  }

  return true;
}

bool SettingsManager::validateSync(const SyncSettings &sync) const {
  if (!isValidSyncSource(sync.source)) {
    ESP_LOGE(
        TAG,
        "Invalid sync.source: must be 0 (auto), 1 (gps), or 2 (rtc), got %u",
        sync.source);
    return false;
  }

  if (!isValidNtpHost(sync.ntp1, false)) {
    ESP_LOGE(TAG, "Invalid sync.ntp1: must be 1-64 ASCII hostname chars (a-z, "
                  "A-Z, 0-9, -, .)");
    return false;
  }
  if (!isValidNtpHost(sync.ntp2, true)) {
    ESP_LOGE(TAG, "Invalid sync.ntp2: must be empty or 1-64 ASCII hostname "
                  "chars (a-z, A-Z, 0-9, -, .)");
    return false;
  }
  if (!isValidNtpHost(sync.ntp3, true)) {
    ESP_LOGE(TAG, "Invalid sync.ntp3: must be empty or 1-64 ASCII hostname "
                  "chars (a-z, A-Z, 0-9, -, .)");
    return false;
  }

  return true;
}

bool SettingsManager::validateWifi(const WifiSettings &wifi) const {
  if (wifi.ssid.length() > 32) {
    ESP_LOGE(TAG, "Invalid wifi.ssid: must be <= 32 characters, got %u",
             wifi.ssid.length());
    return false;
  }

  if (wifi.passwd.length() > 64) {
    ESP_LOGE(TAG, "Invalid wifi.passwd: must be <= 64 characters, got %u",
             wifi.passwd.length());
    return false;
  }

  return true;
}

bool SettingsManager::validateGps(const GpsSettings &gps) const {
  (void)gps;
  return true;
}

bool SettingsManager::validateTouch(const TouchSettings &touch) const {
  (void)touch;
  return true;
}

// ============================================================================
// Defaults
// ============================================================================

void SettingsManager::setDefaults() {
  settings_.device.name = DEFAULT_DEVICE_NAME;
  settings_.device.number = DEFAULT_DEVICE_NUMBER;
  settings_.device.type = DEFAULT_DEVICE_TYPE;
  settings_.device.timezone_offset_min = DEFAULT_DEVICE_TIMEZONE_OFFSET_MIN;

  settings_.sync.auto_sync = DEFAULT_SYNC_AUTO;
  settings_.sync.source = DEFAULT_SYNC_SOURCE;
  settings_.sync.ntp1 = DEFAULT_SYNC_NTP1;
  settings_.sync.ntp2 = DEFAULT_SYNC_NTP2;
  settings_.sync.ntp3 = DEFAULT_SYNC_NTP3;

  settings_.wifi.active = DEFAULT_WIFI_ACTIVE;
  settings_.wifi.ssid = DEFAULT_WIFI_SSID;
  settings_.wifi.passwd = DEFAULT_WIFI_PASSWD;

  settings_.gps.enabled = DEFAULT_GPS_ENABLED;

  settings_.touch.enabled = DEFAULT_TOUCH_ENABLED;
  settings_.touch.calibration.valid = DEFAULT_TOUCH_CAL_VALID;
  settings_.touch.calibration.data[0] = DEFAULT_TOUCH_CAL0;
  settings_.touch.calibration.data[1] = DEFAULT_TOUCH_CAL1;
  settings_.touch.calibration.data[2] = DEFAULT_TOUCH_CAL2;
  settings_.touch.calibration.data[3] = DEFAULT_TOUCH_CAL3;
  settings_.touch.calibration.data[4] = DEFAULT_TOUCH_CAL4;
}

// ============================================================================
// Write-if-changed helpers (уменьшают износ NVS + debug лог)
// ============================================================================

int SettingsManager::putStringIfChanged_(const char *key, const String &v,
                                         const char *def, bool secret) {
  String cur = prefs_.getString(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return 0;
  }

  // Preferences::putString не различает ошибку и успешную запись пустой строки.
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kSettingsNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_str(handle, key, v.c_str());
    if (err == ESP_OK)
      err = nvs_commit(handle);
    nvs_close(handle);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save NVS key '%s': %s", key, esp_err_to_name(err));
    return -1;
  }

  if (secret) {
    SETTINGS_LOG_NVS_SECRET(key);
  } else {
    SETTINGS_LOG_NVS_FMT(key, "\"%s\"", v.c_str());
  }
  return 1;
}

int SettingsManager::putBoolIfChanged_(const char *key, bool v, bool def) {
  bool cur = prefs_.getBool(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return 0;
  }

  if (prefs_.putBool(key, v) != sizeof(uint8_t)) {
    ESP_LOGE(TAG, "Failed to save NVS key '%s'", key);
    return -1;
  }
  SETTINGS_LOG_NVS_FMT(key, "%s", v ? "true" : "false");
  return 1;
}

int SettingsManager::putUCharIfChanged_(const char *key, uint8_t v,
                                        uint8_t def) {
  uint8_t cur = prefs_.getUChar(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return 0;
  }

  if (prefs_.putUChar(key, v) != sizeof(uint8_t)) {
    ESP_LOGE(TAG, "Failed to save NVS key '%s'", key);
    return -1;
  }
  SETTINGS_LOG_NVS_FMT(key, "%u", v);
  return 1;
}

int SettingsManager::putShortIfChanged_(const char *key, int16_t v,
                                        int16_t def) {
  int16_t cur = prefs_.getShort(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return 0;
  }

  if (prefs_.putShort(key, v) != sizeof(int16_t)) {
    ESP_LOGE(TAG, "Failed to save NVS key '%s'", key);
    return -1;
  }
  SETTINGS_LOG_NVS_FMT(key, "%d", v);
  return 1;
}

int SettingsManager::putUShortIfChanged_(const char *key, uint16_t v,
                                         uint16_t def) {
  uint16_t cur = prefs_.getUShort(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return 0;
  }

  if (prefs_.putUShort(key, v) != sizeof(uint16_t)) {
    ESP_LOGE(TAG, "Failed to save NVS key '%s'", key);
    return -1;
  }
  SETTINGS_LOG_NVS_FMT(key, "%u", (unsigned)v);
  return 1;
}

int SettingsManager::putFloatIfChanged_(const char *key, float v, float def,
                                        float eps) {
  float cur = prefs_.getFloat(key, def);
  if (std::fabs(cur - v) <= eps) {
    SETTINGS_LOG_NVS_SKIP(key);
    return 0;
  }

  if (prefs_.putFloat(key, v) != sizeof(float)) {
    ESP_LOGE(TAG, "Failed to save NVS key '%s'", key);
    return -1;
  }
  SETTINGS_LOG_NVS_FMT(key, "%.4f", v);
  return 1;
}

// ============================================================================
// Сохранение/загрузка отдельных групп
// ============================================================================

static int countSavedKeys(std::initializer_list<int> results) {
  int count = 0;
  for (int result : results) {
    if (result < 0)
      return -1;
    count += result;
  }
  return count;
}

int SettingsManager::saveDevice() {
  return countSavedKeys({
      putStringIfChanged_("device.name", settings_.device.name,
                          DEFAULT_DEVICE_NAME),
      putUCharIfChanged_("device.number", settings_.device.number,
                         DEFAULT_DEVICE_NUMBER),
      putUCharIfChanged_("device.type", settings_.device.type,
                         DEFAULT_DEVICE_TYPE),
      putShortIfChanged_("device.tz_min", settings_.device.timezone_offset_min,
                         DEFAULT_DEVICE_TIMEZONE_OFFSET_MIN),
  });
}

int SettingsManager::saveSync() {
  return countSavedKeys({
      putBoolIfChanged_("sync.auto", settings_.sync.auto_sync,
                        DEFAULT_SYNC_AUTO),
      putUCharIfChanged_("sync.source", settings_.sync.source,
                         DEFAULT_SYNC_SOURCE),
      putStringIfChanged_("sync.ntp1", settings_.sync.ntp1, DEFAULT_SYNC_NTP1),
      putStringIfChanged_("sync.ntp2", settings_.sync.ntp2, DEFAULT_SYNC_NTP2),
      putStringIfChanged_("sync.ntp3", settings_.sync.ntp3, DEFAULT_SYNC_NTP3),
  });
}

int SettingsManager::saveWifi() {
  return countSavedKeys({
      putBoolIfChanged_("wifi.active", settings_.wifi.active,
                        DEFAULT_WIFI_ACTIVE),
      putStringIfChanged_("wifi.ssid", settings_.wifi.ssid, DEFAULT_WIFI_SSID),
      // пароль не логируем значением
      putStringIfChanged_("wifi.passwd", settings_.wifi.passwd,
                          DEFAULT_WIFI_PASSWD, true),
  });
}

int SettingsManager::saveGps() {
  return countSavedKeys({
      putBoolIfChanged_("gps.enabled", settings_.gps.enabled,
                        DEFAULT_GPS_ENABLED),
  });
}

int SettingsManager::saveTouch() {
  return countSavedKeys({
      putBoolIfChanged_("touch.enabled", settings_.touch.enabled,
                        DEFAULT_TOUCH_ENABLED),
      putBoolIfChanged_("touch.cal_valid", settings_.touch.calibration.valid,
                        DEFAULT_TOUCH_CAL_VALID),
      putUShortIfChanged_("touch.cal0", settings_.touch.calibration.data[0],
                          DEFAULT_TOUCH_CAL0),
      putUShortIfChanged_("touch.cal1", settings_.touch.calibration.data[1],
                          DEFAULT_TOUCH_CAL1),
      putUShortIfChanged_("touch.cal2", settings_.touch.calibration.data[2],
                          DEFAULT_TOUCH_CAL2),
      putUShortIfChanged_("touch.cal3", settings_.touch.calibration.data[3],
                          DEFAULT_TOUCH_CAL3),
      putUShortIfChanged_("touch.cal4", settings_.touch.calibration.data[4],
                          DEFAULT_TOUCH_CAL4),
  });
}

void SettingsManager::loadDevice() {
  settings_.device.name = prefs_.getString("device.name", DEFAULT_DEVICE_NAME);

  settings_.device.number =
      prefs_.getUChar("device.number", DEFAULT_DEVICE_NUMBER);

  settings_.device.type = prefs_.getUChar("device.type", DEFAULT_DEVICE_TYPE);

  settings_.device.timezone_offset_min =
      prefs_.getShort("device.tz_min", DEFAULT_DEVICE_TIMEZONE_OFFSET_MIN);
}

void SettingsManager::loadSync() {
  settings_.sync.auto_sync = prefs_.getBool("sync.auto", DEFAULT_SYNC_AUTO);

  settings_.sync.source = prefs_.getUChar("sync.source", DEFAULT_SYNC_SOURCE);

  settings_.sync.ntp1 = prefs_.getString("sync.ntp1", DEFAULT_SYNC_NTP1);
  settings_.sync.ntp2 = prefs_.getString("sync.ntp2", DEFAULT_SYNC_NTP2);
  settings_.sync.ntp3 = prefs_.getString("sync.ntp3", DEFAULT_SYNC_NTP3);
}

void SettingsManager::loadWifi() {
  settings_.wifi.active = prefs_.getBool("wifi.active", DEFAULT_WIFI_ACTIVE);

  settings_.wifi.ssid = prefs_.getString("wifi.ssid", DEFAULT_WIFI_SSID);

  settings_.wifi.passwd = prefs_.getString("wifi.passwd", DEFAULT_WIFI_PASSWD);
}

void SettingsManager::loadGps() {
  settings_.gps.enabled = prefs_.getBool("gps.enabled", DEFAULT_GPS_ENABLED);
}

void SettingsManager::loadTouch() {
  settings_.touch.enabled = prefs_.getBool("touch.enabled", DEFAULT_TOUCH_ENABLED);
  settings_.touch.calibration.valid =
      prefs_.getBool("touch.cal_valid", DEFAULT_TOUCH_CAL_VALID);
  settings_.touch.calibration.data[0] =
      prefs_.getUShort("touch.cal0", DEFAULT_TOUCH_CAL0);
  settings_.touch.calibration.data[1] =
      prefs_.getUShort("touch.cal1", DEFAULT_TOUCH_CAL1);
  settings_.touch.calibration.data[2] =
      prefs_.getUShort("touch.cal2", DEFAULT_TOUCH_CAL2);
  settings_.touch.calibration.data[3] =
      prefs_.getUShort("touch.cal3", DEFAULT_TOUCH_CAL3);
  settings_.touch.calibration.data[4] =
      prefs_.getUShort("touch.cal4", DEFAULT_TOUCH_CAL4);
}

// ============================================================================
// Публичные методы
// ============================================================================

bool SettingsManager::begin() {
  if (initialized_) {
    ESP_LOGW(TAG, "SettingsManager already initialized");
    return true;
  }

  if (!prefs_.begin(kSettingsNamespace, false)) {
    ESP_LOGE(TAG, "Failed to open NVS namespace 'entime'");
    return false;
  }

  initialized_ = true;
  ESP_LOGI(TAG, "SettingsManager initialized");

  // Загружаем настройки (или используем значения по умолчанию)
  if (!load()) {
    ESP_LOGW(TAG, "Failed to load settings, using defaults");
    setDefaults();
  }

  return true;
}

bool SettingsManager::load() {
  if (!initialized_) {
    ESP_LOGE(TAG, "SettingsManager not initialized, call begin() first");
    return false;
  }

  // Каждый ключ независим: значения по умолчанию могут не записываться в NVS.
  loadDevice();
  loadSync();
  loadWifi();
  loadGps();
  loadTouch();
  // Валидация загруженных настроек
  if (!validateDevice(settings_.device) || !validateSync(settings_.sync) ||
      !validateWifi(settings_.wifi) || !validateGps(settings_.gps) ||
      !validateTouch(settings_.touch)) {
    ESP_LOGW(TAG, "Loaded settings failed validation, using defaults");
    setDefaults();
    return false;
  }

  ESP_LOGI(TAG, "Settings loaded successfully");
  return true;
}

int SettingsManager::save() {
  if (!initialized_) {
    ESP_LOGE(TAG, "SettingsManager not initialized, call begin() first");
    return -1;
  }

  // Валидация перед сохранением
  if (!validateDevice(settings_.device) || !validateSync(settings_.sync) ||
      !validateWifi(settings_.wifi) || !validateGps(settings_.gps) ||
      !validateTouch(settings_.touch)) {
    ESP_LOGE(TAG, "Settings validation failed, cannot save");
    return -1;
  }

  const int total = countSavedKeys({saveDevice(), saveSync(), saveWifi(),
                                    saveGps(), saveTouch()});
  if (total < 0) {
    ESP_LOGE(TAG, "Failed to save settings");
    return -1;
  }

  ESP_LOGI(TAG, "Settings saved successfully (%d keys)", total);
  return total;
}

bool SettingsManager::factoryReset() {
  if (!initialized_) {
    ESP_LOGE(TAG, "SettingsManager not initialized, call begin() first");
    return false;
  }

  if (!prefs_.clear()) {
    ESP_LOGE(TAG, "Failed to clear settings");
    return false;
  }
  setDefaults();

  if (save() < 0) {
    ESP_LOGE(TAG, "Failed to save default settings after factory reset");
    return false;
  }

  ESP_LOGI(TAG, "Factory reset completed");
  return true;
}

bool SettingsManager::setDevice(const DeviceSettings &device) {
  if (!validateDevice(device)) {
    return false;
  }
  settings_.device = device;
  return true;
}

bool SettingsManager::setSync(const SyncSettings &sync) {
  if (!validateSync(sync)) {
    return false;
  }
  settings_.sync = sync;
  return true;
}

bool SettingsManager::setWifi(const WifiSettings &wifi) {
  if (!validateWifi(wifi)) {
    return false;
  }
  settings_.wifi = wifi;
  return true;
}

bool SettingsManager::setGps(const GpsSettings &gps) {
  if (!validateGps(gps)) {
    return false;
  }
  settings_.gps = gps;
  return true;
}

bool SettingsManager::setTouch(const TouchSettings &touch) {
  if (!validateTouch(touch)) {
    return false;
  }
  settings_.touch = touch;
  return true;
}

bool SettingsManager::getStorageStats(size_t &used_bytes,
                                      size_t &total_bytes) const {
  if (!initialized_) {
    return false;
  }

  // Получаем статистику NVS раздела
  nvs_stats_t nvs_stats;
  esp_err_t err = nvs_get_stats("nvs", &nvs_stats);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get NVS stats: %s", esp_err_to_name(err));
    return false;
  }

  // nvs_stats содержит статистику по записям (entries), а не по байтам
  // Каждая запись в NVS имеет фиксированный размер: 32 байта (ключ + значение +
  // метаданные) Но размер может варьироваться в зависимости от типа данных
  const size_t nvs_entry_size =
      32; // стандартный размер одной записи NVS в байтах

  // Общий размер раздела = общее количество записей * размер записи
  total_bytes = nvs_stats.total_entries * nvs_entry_size;

  // Использованное пространство = использованные записи * размер записи
  used_bytes = nvs_stats.used_entries * nvs_entry_size;

  ESP_LOGD(TAG,
           "NVS stats: used_entries=%lu, free_entries=%lu, "
           "total_entries=%lu, namespace_count=%lu, "
           "used_bytes=%zu, total_bytes=%zu",
           (unsigned long)nvs_stats.used_entries,
           (unsigned long)nvs_stats.free_entries,
           (unsigned long)nvs_stats.total_entries,
           (unsigned long)nvs_stats.namespace_count, used_bytes, total_bytes);

  return true;
}

JsonDocument SettingsManager::toJson() const {
  JsonDocument doc;

  // Device settings
  doc["device"]["name"] = settings_.device.name;
  doc["device"]["number"] = settings_.device.number;
  doc["device"]["type"] = settings_.device.type;
  doc["device"]["timezone_offset_min"] = settings_.device.timezone_offset_min;

  // Sync settings
  doc["sync"]["auto"] = settings_.sync.auto_sync;
  doc["sync"]["source"] = settings_.sync.source;
  doc["sync"]["ntp1"] = settings_.sync.ntp1;
  doc["sync"]["ntp2"] = settings_.sync.ntp2;
  doc["sync"]["ntp3"] = settings_.sync.ntp3;

  // WiFi settings
  doc["wifi"]["active"] = settings_.wifi.active;
  doc["wifi"]["ssid"] = settings_.wifi.ssid;
  // doc["wifi"]["passwd"] = settings_.wifi.passwd;
  // Do not show wifi password
  doc["wifi"]["passwd"] = "";

  // GPS settings
  doc["gps"]["enabled"] = settings_.gps.enabled;

  // Touch settings
  doc["touch"]["enabled"] = settings_.touch.enabled;
  doc["touch"]["cal_valid"] = settings_.touch.calibration.valid;
  JsonArray touch_cal = doc["touch"]["calibration"].to<JsonArray>();
  for (uint8_t i = 0; i < 5; i++) {
    touch_cal.add(settings_.touch.calibration.data[i]);
  }

  return doc;
}

template <typename T>
static bool checkOptionalJsonType(JsonVariantConst value, const char *field) {
  if (value.isNull() || value.is<T>())
    return true;
  ESP_LOGE(TAG, "Invalid %s: unexpected JSON type or out-of-range value",
           field);
  return false;
}

template <typename T>
static bool readJsonSetting(JsonVariantConst value, T &target,
                            const char *field) {
  // Сохраняем существующую обработку отсутствующих значений и null.
  if (value.isNull())
    return true;
  if (!checkOptionalJsonType<T>(value, field))
    return false;
  target = value.as<T>();
  return true;
}

bool SettingsManager::fromJson(const JsonDocument &doc) {
  if (!checkOptionalJsonType<JsonObjectConst>(doc.as<JsonVariantConst>(),
                                              "settings"))
    return false;
  for (const char *group : {"device", "sync", "wifi", "gps", "touch"}) {
    if (!checkOptionalJsonType<JsonObjectConst>(doc[group], group))
      return false;
  }

  // Изменения применяются только после проверки всех групп.
  DeviceSettings device = settings_.device;
  SyncSettings sync = settings_.sync;
  WifiSettings wifi = settings_.wifi;
  GpsSettings gps = settings_.gps;
  TouchSettings touch = settings_.touch;

  if (!doc["device"].isNull()) {
    if (!readJsonSetting(doc["device"]["name"], device.name, "device.name") ||
        !readJsonSetting(doc["device"]["number"], device.number,
                         "device.number") ||
        !readJsonSetting(doc["device"]["type"], device.type, "device.type") ||
        !readJsonSetting(doc["device"]["timezone_offset_min"],
                         device.timezone_offset_min,
                         "device.timezone_offset_min") ||
        !validateDevice(device))
      return false;
  }

  if (!doc["sync"].isNull()) {
    if (!readJsonSetting(doc["sync"]["auto"], sync.auto_sync, "sync.auto") ||
        !readJsonSetting(doc["sync"]["source"], sync.source, "sync.source") ||
        !readJsonSetting(doc["sync"]["ntp1"], sync.ntp1, "sync.ntp1") ||
        !readJsonSetting(doc["sync"]["ntp2"], sync.ntp2, "sync.ntp2") ||
        !readJsonSetting(doc["sync"]["ntp3"], sync.ntp3, "sync.ntp3") ||
        !validateSync(sync))
      return false;
  }

  if (!doc["wifi"].isNull()) {
    if (!readJsonSetting(doc["wifi"]["active"], wifi.active, "wifi.active") ||
        !readJsonSetting(doc["wifi"]["ssid"], wifi.ssid, "wifi.ssid") ||
        !readJsonSetting(doc["wifi"]["passwd"], wifi.passwd, "wifi.passwd") ||
        !validateWifi(wifi))
      return false;
  }

  if (!doc["gps"].isNull()) {
    if (!readJsonSetting(doc["gps"]["enabled"], gps.enabled, "gps.enabled") ||
        !validateGps(gps))
      return false;
  }

  if (!doc["touch"].isNull()) {
    if (!readJsonSetting(doc["touch"]["enabled"], touch.enabled,
                         "touch.enabled") ||
        !readJsonSetting(doc["touch"]["cal_valid"], touch.calibration.valid,
                         "touch.cal_valid"))
      return false;
    if (!doc["touch"]["calibration"].isNull()) {
      if (!checkOptionalJsonType<JsonArrayConst>(doc["touch"]["calibration"],
                                                 "touch.calibration"))
        return false;
      JsonArrayConst cal_array =
          doc["touch"]["calibration"].as<JsonArrayConst>();
      if (cal_array.size() != 5)
        return false;
      for (uint8_t i = 0; i < 5; ++i) {
        if (!checkOptionalJsonType<uint16_t>(cal_array[i], "touch.calibration"))
          return false;
        touch.calibration.data[i] = cal_array[i].as<uint16_t>();
      }
    } else {
      if (!readJsonSetting(doc["touch"]["cal0"], touch.calibration.data[0],
                           "touch.cal0") ||
          !readJsonSetting(doc["touch"]["cal1"], touch.calibration.data[1],
                           "touch.cal1") ||
          !readJsonSetting(doc["touch"]["cal2"], touch.calibration.data[2],
                           "touch.cal2") ||
          !readJsonSetting(doc["touch"]["cal3"], touch.calibration.data[3],
                           "touch.cal3") ||
          !readJsonSetting(doc["touch"]["cal4"], touch.calibration.data[4],
                           "touch.cal4"))
        return false;
    }
    if (!validateTouch(touch))
      return false;
  }

  settings_.device = device;
  settings_.sync = sync;
  settings_.wifi = wifi;
  settings_.gps = gps;
  settings_.touch = touch;
  return true;
}
