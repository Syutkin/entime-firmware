#ifndef BLE_H
#define BLE_H

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "config.h"

// Состояния Bluetooth
enum class BLEState : uint8_t {
  DISCONNECTED = 0, // Реклама не активна
  ADVERTISING = 1,  // Реклама активна, но нет подключения
  CONNECTED = 2,    // Есть активное подключение
};

// Callback для уведомления об изменении состояния подключения
typedef void (*BLEStateCallback)(BLEState state);

// ============================================================================
// Plugin interface
// ============================================================================

/**
 * @brief Интерфейс BLE-плагина (сервиса).
 *
 * Плагин должен:
 *  - В init(server) создать свой service/characteristics
 *  - НЕ вызывать NimBLEDevice::init(), НЕ стартовать рекламу
 *
 * BLE гарантирует, что init() всех плагинов будет вызван ДО startAdvertising().
 * При старте advertising NimBLE запускает сервер и все созданные сервисы.
 */
class IBleServicePlugin {
public:
  virtual ~IBleServicePlugin() = default;

  // Вызывается один раз перед стартом advertising.
  // Возвращает true при успешной инициализации сервиса.
  virtual bool init(NimBLEServer *server) = 0;

  // Опциональные события BLE lifecycle
  virtual void onConnect(NimBLEConnInfo & /*connInfo*/, uint16_t /*mtu*/) {}
  virtual void onDisconnect(int /*reason*/) {}
  virtual void onMtuUpdated(uint16_t /*mtu*/) {}
  virtual void onBleEnd() {}

  // Опциональный вклад в advertising payload
  virtual void configureAdvertising(NimBLEAdvertisementData & /*advData*/) {}
};

class BLE {
public:
  bool init(const String &deviceName = String(BLE_DEVICE_NAME));
  void startAdvertising();
  void stopAdvertising();
  void end();

  bool isConnected();
  bool isAdvertising();

  /**
   * @brief Регистрация BLE-плагина (сервиса).
   *
   * Важно:
   *  - вызывать после init()
   *  - и ДО startAdvertising()
   * @return true если зарегистрирован
   */
  bool registerService(IBleServicePlugin &plugin);

  /**
   * @brief Получить текущее состояние Bluetooth
   * @return BLEState Текущее состояние
   */
  BLEState getState();

  /**
   * @brief Количество активных BLE клиентов
   */
  uint16_t getClientCount() const { return _clientCount; }

  /**
   * @brief Установить callback для уведомления об изменении состояния
   * подключения
   * @param callback Функция, которая будет вызвана при
   * подключении/отключении с текущим состоянием
   */
  void setStateCallback(BLEStateCallback callback);

  void onConnect(NimBLEConnInfo &connInfo, uint16_t mtu);
  void onDisconnect(int reason);
  void onMtuUpdated(uint16_t mtu);

private:
  void notifyStateChanged();
  void setupAdvertisingOnce();

  // Plugins
  void initPluginsOnce();
  void pluginsConfigureAdvertising(NimBLEAdvertisementData &advData);
  void pluginsOnConnect(NimBLEConnInfo &connInfo, uint16_t mtu);
  void pluginsOnDisconnect(int reason);
  void pluginsOnMtuUpdated(uint16_t mtu);
  void pluginsOnBleEnd();

private:
  NimBLEServer *_server = nullptr;

  bool _connected = false;
  uint16_t _clientCount = 0;

  String _deviceName;
  // Callback для уведомления об изменении состояния
  BLEStateCallback _stateCallback = nullptr;

  bool _advConfigured = false; // Флаг для отслеживания настроенной рекламы

  // Plugin registry (фиксированный размер)
  static constexpr size_t MAX_PLUGINS = 8;
  IBleServicePlugin *_plugins[MAX_PLUGINS] = {nullptr};
  size_t _pluginCount = 0;
  bool _pluginsInited = false;
};

extern BLE ble;

#endif // BLE_H
