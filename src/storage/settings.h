#ifndef SETTINGS_H
#define SETTINGS_H

#include "config.h"
#include <Preferences.h>
#include <Arduino.h>
#include <ArduinoJson.h>

// ============================================================================
// Значения по умолчанию
// ============================================================================

#define DEFAULT_DEVICE_NAME BLE_DEVICE_NAME
#define DEFAULT_DEVICE_NUMBER 1
#define DEFAULT_DEVICE_TYPE 1
#define DEFAULT_DEVICE_TIMEZONE_OFFSET_MIN 180

#define DEFAULT_SYNC_AUTO true
#define DEFAULT_SYNC_SOURCE 0
#define DEFAULT_SYNC_NTP1 "ru.pool.ntp.org"
#define DEFAULT_SYNC_NTP2 "time.google.com"
#define DEFAULT_SYNC_NTP3 "time.cloudflare.com"

#define DEFAULT_WIFI_ACTIVE false
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWD ""

#define DEFAULT_GPS_ENABLED true

#define DEFAULT_TOUCH_ENABLED true
#define DEFAULT_TOUCH_CAL_VALID false
#define DEFAULT_TOUCH_CAL0 0
#define DEFAULT_TOUCH_CAL1 0
#define DEFAULT_TOUCH_CAL2 0
#define DEFAULT_TOUCH_CAL3 0
#define DEFAULT_TOUCH_CAL4 0

// ============================================================================
// Структуры данных
// ============================================================================

/**
 * @brief Настройки устройства
 */
struct DeviceSettings {
  String name = DEFAULT_DEVICE_NAME;    // 1-16 символов, ASCII
  uint8_t number = DEFAULT_DEVICE_NUMBER;    // 1-255
  uint8_t type = DEFAULT_DEVICE_TYPE;        // 1 = стартовый, 2 = финишный
  int16_t timezone_offset_min = DEFAULT_DEVICE_TIMEZONE_OFFSET_MIN; // -720 до 840
};

/**
 * @brief Настройки синхронизации времени
 */
struct SyncSettings {
  bool auto_sync = DEFAULT_SYNC_AUTO;       // автоматическая синхронизация
  uint8_t source = DEFAULT_SYNC_SOURCE;     // 0 = авто, 1 = GPS, 2 = RTC
  String ntp1 = DEFAULT_SYNC_NTP1;          // основной NTP сервер
  String ntp2 = DEFAULT_SYNC_NTP2;          // резервный NTP сервер
  String ntp3 = DEFAULT_SYNC_NTP3;          // третичный NTP сервер
};

/**
 * @brief Настройки WiFi
 */
struct WifiSettings {
  bool active = DEFAULT_WIFI_ACTIVE;        // включен ли WiFi
  String ssid = DEFAULT_WIFI_SSID;     // до 32 символов
  String passwd = DEFAULT_WIFI_PASSWD; // до 64 символов
};

/**
 * @brief Настройки GPS
 */
struct GpsSettings {
  bool enabled = DEFAULT_GPS_ENABLED;        // включать GPS при загрузке
};

/**
 * @brief Калибровка touch-панели
 */
struct TouchCalibration {
  uint16_t data[5] = {DEFAULT_TOUCH_CAL0, DEFAULT_TOUCH_CAL1, DEFAULT_TOUCH_CAL2,
                      DEFAULT_TOUCH_CAL3, DEFAULT_TOUCH_CAL4};
  bool valid = DEFAULT_TOUCH_CAL_VALID;
};

/**
 * @brief Настройки touch-панели
 */
struct TouchSettings {
  bool enabled = DEFAULT_TOUCH_ENABLED;
  TouchCalibration calibration;
};

/**
 * @brief Полная структура настроек
 */
struct Settings {
  DeviceSettings device;
  SyncSettings sync;
  WifiSettings wifi;
  GpsSettings gps;
  TouchSettings touch;
};

/**
 * @brief Менеджер настроек для работы с NVS
 *
 * Хранит настройки в ESP32 NVS через Preferences API.
 * Поддерживает загрузку, сохранение, валидацию и обновление только изменившихся настроек.
 */
class SettingsManager {
public:
  /**
   * @brief Инициализация менеджера и загрузка настроек из NVS
   * @return true если успешно, false при ошибке
   */
  bool begin();

  /**
   * @brief Загрузить все настройки из NVS
   * @return true если успешно, false при ошибке
   */
  bool load();

  /**
   * @brief Сохранить все настройки в NVS
   * При ошибке часть ключей уже может быть записана; повторный save допишет остальные.
   * @return количество сохраненных ключей при успехе, -1 при ошибке
   */
  int save();

  /**
   * @brief Сброс всех настроек к значениям по умолчанию
   * @return true если успешно, false при ошибке
   */
  bool factoryReset();

  // Геттеры
  const DeviceSettings &getDevice() const { return settings_.device; }
  const SyncSettings &getSync() const { return settings_.sync; }
  const WifiSettings &getWifi() const { return settings_.wifi; }
  const GpsSettings &getGps() const { return settings_.gps; }
  const TouchSettings &getTouch() const { return settings_.touch; }
  const Settings &getAll() const { return settings_; }

  // Сеттеры с валидацией
  /**
   * @brief Установить настройки устройства с валидацией
   * @param device Настройки устройства
   * @return true если валидация прошла и настройки установлены, false при
   * ошибке
   */
  bool setDevice(const DeviceSettings &device);

  /**
   * @brief Установить настройки синхронизации с валидацией
   * @param sync Настройки синхронизации
   * @return true если валидация прошла и настройки установлены, false при
   * ошибке
   */
  bool setSync(const SyncSettings &sync);

  /**
   * @brief Установить настройки WiFi с валидацией
   * @param wifi Настройки WiFi
   * @return true если валидация прошла и настройки установлены, false при
   * ошибке
   */
  bool setWifi(const WifiSettings &wifi);

  /**
   * @brief Установить настройки GPS с валидацией
   * @param gps Настройки GPS
   * @return true если валидация прошла и настройки установлены, false при
   * ошибке
   */
  bool setGps(const GpsSettings &gps);

  /**
   * @brief Установить настройки touch с валидацией
   * @param touch Настройки touch
   * @return true если валидация прошла и настройки установлены, false при
   * ошибке
   */
  bool setTouch(const TouchSettings &touch);

  /**
   * @brief Получить статистику использования хранилища
   * @param used_bytes Количество использованных байт
   * @param total_bytes Общий размер раздела
   * @return true если успешно
   */
  bool getStorageStats(size_t &used_bytes, size_t &total_bytes) const;

  /**
   * @brief Получить JSON представление всех настроек
   * @return JsonDocument с настройками в формате JSON
   */
  JsonDocument toJson() const;

  /**
   * @brief Загрузить настройки из JSON документа
   * @param doc JsonDocument с настройками (группы и поля опциональны)
   * @return true если все присутствующие настройки успешно валидированы и установлены, false при ошибке
   */
  bool fromJson(const JsonDocument& doc);

private:
  Preferences prefs_;
  Settings settings_;
  bool initialized_ = false;

  // Запись при изменении: 0 — без изменений, 1 — записано, -1 — ошибка.
  int putStringIfChanged_(const char *key, const String &v, const char *def,
                          bool secret = false);
  int putBoolIfChanged_(const char *key, bool v, bool def);
  int putUCharIfChanged_(const char *key, uint8_t v, uint8_t def);
  int putShortIfChanged_(const char *key, int16_t v, int16_t def);
  int putUShortIfChanged_(const char *key, uint16_t v, uint16_t def);
  int putFloatIfChanged_(const char *key, float v, float def,
                         float eps = 0.0001f);

  // Валидация
  bool validateDevice(const DeviceSettings &device) const;
  bool validateSync(const SyncSettings &sync) const;
  bool validateWifi(const WifiSettings &wifi) const;
  bool validateGps(const GpsSettings &gps) const;
  bool validateTouch(const TouchSettings &touch) const;
  // Вспомогательные функции валидации
  bool isValidAsciiName(const String &name) const;
  bool isValidDeviceType(uint8_t type) const;
  bool isValidSyncSource(uint8_t source) const;
  bool isValidNtpHost(const String &host, bool allowEmpty) const;

  // Сохранение/загрузка отдельных групп
  int saveDevice();
  int saveSync();
  int saveWifi();
  int saveGps();
  int saveTouch();
  void loadDevice();
  void loadSync();
  void loadWifi();
  void loadGps();
  void loadTouch();

  // Установка значений по умолчанию
  void setDefaults();
};

// Глобальный экземпляр менеджера настроек
extern SettingsManager settings;

#endif // SETTINGS_H
