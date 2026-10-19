#include "settings.h"
#include "esp_log.h"
#include "nvs.h"
#include <ArduinoJson.h>
#include <cctype>
#include <cmath>

static const char *TAG = "Settings";

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

bool SettingsManager::putStringIfChanged_(const char *key, const String &v,
                                          const char *def, bool secret) {
  String cur = prefs_.getString(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return false;
  }

  prefs_.putString(key, v);

  if (secret) {
    SETTINGS_LOG_NVS_SECRET(key);
  } else {
    SETTINGS_LOG_NVS_FMT(key, "\"%s\"", v.c_str());
  }
  return true;
}

bool SettingsManager::putBoolIfChanged_(const char *key, bool v, bool def) {
  bool cur = prefs_.getBool(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return false;
  }

  prefs_.putBool(key, v);
  SETTINGS_LOG_NVS_FMT(key, "%s", v ? "true" : "false");
  return true;
}

bool SettingsManager::putUCharIfChanged_(const char *key, uint8_t v,
                                         uint8_t def) {
  uint8_t cur = prefs_.getUChar(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return false;
  }

  prefs_.putUChar(key, v);
  SETTINGS_LOG_NVS_FMT(key, "%u", v);
  return true;
}

bool SettingsManager::putShortIfChanged_(const char *key, int16_t v,
                                         int16_t def) {
  int16_t cur = prefs_.getShort(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return false;
  }

  prefs_.putShort(key, v);
  SETTINGS_LOG_NVS_FMT(key, "%d", v);
  return true;
}

bool SettingsManager::putUShortIfChanged_(const char *key, uint16_t v,
                                          uint16_t def) {
  uint16_t cur = prefs_.getUShort(key, def);
  if (cur == v) {
    SETTINGS_LOG_NVS_SKIP(key);
    return false;
  }

  prefs_.putUShort(key, v);
  SETTINGS_LOG_NVS_FMT(key, "%u", (unsigned)v);
  return true;
}

bool SettingsManager::putFloatIfChanged_(const char *key, float v, float def,
                                         float eps) {
  float cur = prefs_.getFloat(key, def);
  if (std::fabs(cur - v) <= eps) {
    SETTINGS_LOG_NVS_SKIP(key);
    return false;
  }

  prefs_.putFloat(key, v);
  SETTINGS_LOG_NVS_FMT(key, "%.4f", v);
  return true;
}

// ============================================================================
// Сохранение/загрузка отдельных групп
// ============================================================================

size_t SettingsManager::saveDevice() {
  size_t count = 0;
  if (putStringIfChanged_("device.name", settings_.device.name,
                          DEFAULT_DEVICE_NAME))
    count++;
  if (putUCharIfChanged_("device.number", settings_.device.number,
                         DEFAULT_DEVICE_NUMBER))
    count++;
  if (putUCharIfChanged_("device.type", settings_.device.type,
                         DEFAULT_DEVICE_TYPE))
    count++;
  if (putShortIfChanged_("device.tz_min",
                         settings_.device.timezone_offset_min,
                         DEFAULT_DEVICE_TIMEZONE_OFFSET_MIN))
    count++;
  return count;
}

size_t SettingsManager::saveSync() {
  size_t count = 0;
  if (putBoolIfChanged_("sync.auto", settings_.sync.auto_sync,
                        DEFAULT_SYNC_AUTO))
    count++;
  if (putUCharIfChanged_("sync.source", settings_.sync.source,
                         DEFAULT_SYNC_SOURCE))
    count++;
  if (putStringIfChanged_("sync.ntp1", settings_.sync.ntp1, DEFAULT_SYNC_NTP1))
    count++;
  if (putStringIfChanged_("sync.ntp2", settings_.sync.ntp2, DEFAULT_SYNC_NTP2))
    count++;
  if (putStringIfChanged_("sync.ntp3", settings_.sync.ntp3, DEFAULT_SYNC_NTP3))
    count++;
  return count;
}

size_t SettingsManager::saveWifi() {
  size_t count = 0;
  if (putBoolIfChanged_("wifi.active", settings_.wifi.active,
                        DEFAULT_WIFI_ACTIVE))
    count++;
  if (putStringIfChanged_("wifi.ssid", settings_.wifi.ssid, DEFAULT_WIFI_SSID))
    count++;
  // пароль не логируем значением
  if (putStringIfChanged_("wifi.passwd", settings_.wifi.passwd,
                          DEFAULT_WIFI_PASSWD, true))
    count++;
  return count;
}

size_t SettingsManager::saveGps() {
  size_t count = 0;
  if (putBoolIfChanged_("gps.enabled", settings_.gps.enabled,
                        DEFAULT_GPS_ENABLED))
    count++;
  return count;
}

size_t SettingsManager::saveTouch() {
  size_t count = 0;
  if (putBoolIfChanged_("touch.enabled", settings_.touch.enabled,
                        DEFAULT_TOUCH_ENABLED))
    count++;
  if (putBoolIfChanged_("touch.cal_valid", settings_.touch.calibration.valid,
                        DEFAULT_TOUCH_CAL_VALID))
    count++;
  if (putUShortIfChanged_("touch.cal0", settings_.touch.calibration.data[0],
                          DEFAULT_TOUCH_CAL0))
    count++;
  if (putUShortIfChanged_("touch.cal1", settings_.touch.calibration.data[1],
                          DEFAULT_TOUCH_CAL1))
    count++;
  if (putUShortIfChanged_("touch.cal2", settings_.touch.calibration.data[2],
                          DEFAULT_TOUCH_CAL2))
    count++;
  if (putUShortIfChanged_("touch.cal3", settings_.touch.calibration.data[3],
                          DEFAULT_TOUCH_CAL3))
    count++;
  if (putUShortIfChanged_("touch.cal4", settings_.touch.calibration.data[4],
                          DEFAULT_TOUCH_CAL4))
    count++;
  return count;
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

  if (!prefs_.begin("entime", false)) {
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

  // Если ключ "device.name" не существует — настроек нет
  if (!prefs_.isKey("device.name")) {
    ESP_LOGI(TAG, "No saved settings found, using defaults");
    setDefaults();
    return true;
  }

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

  size_t total = 0;
  total += saveDevice();
  total += saveSync();
  total += saveWifi();
  total += saveGps();
  total += saveTouch();

  ESP_LOGI(TAG, "Settings saved successfully (%zu keys)", total);
  return (int)total;
}

bool SettingsManager::factoryReset() {
  if (!initialized_) {
    ESP_LOGE(TAG, "SettingsManager not initialized, call begin() first");
    return false;
  }

  prefs_.clear();
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

bool SettingsManager::fromJson(const JsonDocument& doc) {
  // Создать временные структуры и обновить их данными из JSON
  DeviceSettings device = settings_.device;
  SyncSettings sync = settings_.sync;
  WifiSettings wifi = settings_.wifi;
  GpsSettings gps = settings_.gps;
  TouchSettings touch = settings_.touch;
  // Device settings
  if (!doc["device"].isNull()) {
    if (!doc["device"]["name"].isNull()) {
      device.name = doc["device"]["name"].as<String>();
    }
    if (!doc["device"]["number"].isNull()) {
      device.number = doc["device"]["number"].as<uint8_t>();
    }
    if (!doc["device"]["type"].isNull()) {
      device.type = doc["device"]["type"].as<uint8_t>();
    }
    if (!doc["device"]["timezone_offset_min"].isNull()) {
      device.timezone_offset_min =
          doc["device"]["timezone_offset_min"].as<int16_t>();
    }

    if (!validateDevice(device)) {
      return false;
    }
  }

  // Sync settings
  if (!doc["sync"].isNull()) {
    if (!doc["sync"]["auto"].isNull()) {
      sync.auto_sync = doc["sync"]["auto"].as<bool>();
    }
    if (!doc["sync"]["source"].isNull()) {
      sync.source = doc["sync"]["source"].as<uint8_t>();
    }
    if (!doc["sync"]["ntp1"].isNull()) {
      sync.ntp1 = doc["sync"]["ntp1"].as<String>();
    }
    if (!doc["sync"]["ntp2"].isNull()) {
      sync.ntp2 = doc["sync"]["ntp2"].as<String>();
    }
    if (!doc["sync"]["ntp3"].isNull()) {
      sync.ntp3 = doc["sync"]["ntp3"].as<String>();
    }

    if (!validateSync(sync)) {
      return false;
    }
  }

  // WiFi settings
  if (!doc["wifi"].isNull()) {
    if (!doc["wifi"]["active"].isNull()) {
      wifi.active = doc["wifi"]["active"].as<bool>();
    }
    if (!doc["wifi"]["ssid"].isNull()) {
      wifi.ssid = doc["wifi"]["ssid"].as<String>();
    }
    if (!doc["wifi"]["passwd"].isNull()) {
      wifi.passwd = doc["wifi"]["passwd"].as<String>();
    }

    if (!validateWifi(wifi)) {
      return false;
    }
  }

  // GPS settings
  if (!doc["gps"].isNull()) {
    if (!doc["gps"]["enabled"].isNull()) {
      gps.enabled = doc["gps"]["enabled"].as<bool>();
    }

    if (!validateGps(gps)) {
      return false;
    }
  }

  // Touch settings
  if (!doc["touch"].isNull()) {
    if (!doc["touch"]["enabled"].isNull()) {
      touch.enabled = doc["touch"]["enabled"].as<bool>();
    }
    if (!doc["touch"]["cal_valid"].isNull()) {
      touch.calibration.valid = doc["touch"]["cal_valid"].as<bool>();
    }
    if (!doc["touch"]["calibration"].isNull()) {
      if (!doc["touch"]["calibration"].is<JsonArrayConst>()) {
        return false;
      }
      JsonArrayConst cal_array = doc["touch"]["calibration"].as<JsonArrayConst>();
      if (cal_array.size() != 5) {
        return false;
      }
      for (uint8_t i = 0; i < 5; i++) {
        touch.calibration.data[i] = cal_array[i].as<uint16_t>();
      }
    } else {
      if (!doc["touch"]["cal0"].isNull()) {
        touch.calibration.data[0] = doc["touch"]["cal0"].as<uint16_t>();
      }
      if (!doc["touch"]["cal1"].isNull()) {
        touch.calibration.data[1] = doc["touch"]["cal1"].as<uint16_t>();
      }
      if (!doc["touch"]["cal2"].isNull()) {
        touch.calibration.data[2] = doc["touch"]["cal2"].as<uint16_t>();
      }
      if (!doc["touch"]["cal3"].isNull()) {
        touch.calibration.data[3] = doc["touch"]["cal3"].as<uint16_t>();
      }
      if (!doc["touch"]["cal4"].isNull()) {
        touch.calibration.data[4] = doc["touch"]["cal4"].as<uint16_t>();
      }
    }

    if (!validateTouch(touch)) {
      return false;
    }
  }

  // Если все валидации прошли - применить изменения
  settings_.device = device;
  settings_.sync = sync;
  settings_.wifi = wifi;
  settings_.gps = gps;
  settings_.touch = touch;

  return true;
}
