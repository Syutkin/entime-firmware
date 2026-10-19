#include "ble.h"

#include "esp_log.h"

static const char *TAG = "BLE";
BLE ble;

// ============================================================================
// Callbacks
// ============================================================================

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
    uint16_t mtu = connInfo.getMTU();
    ESP_LOGI(TAG, "Client connected: %s, MTU: %d",
             connInfo.getAddress().toString().c_str(), mtu);

    ble.onConnect(connInfo, mtu);

    // Настройка connection interval для низкой latency (быстрое соединение)
    // min_interval: 6 = 7.5ms, max_interval: 6 = 7.5ms
    // latency: 0 = без пропусков, timeout: 500 = 5 секунд
    pServer->updateConnParams(connInfo.getConnHandle(), 6, 6, 0, 500);
    ESP_LOGI(
        TAG,
        "Conn params requested: min=7.5ms, max=15ms, latency=0, timeout=5s");
  }

  void onDisconnect(NimBLEServer * /*pServer*/, NimBLEConnInfo & /*connInfo*/,
                    int reason) override {
    ESP_LOGI(TAG, "Client disconnected, reason: %d", reason);

    ble.onDisconnect(reason);

    // Только старт рекламы — payload уже настроен в begin()
    ble.startAdvertising();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo & /*connInfo*/) override {
    ESP_LOGI(TAG, "MTU updated: %d", mtu);
    ble.onMtuUpdated(mtu);
  }
};

// ============================================================================
// BLE Implementation
// ============================================================================

bool BLE::registerService(IBleServicePlugin &plugin) {
  if (!_server) {
    ESP_LOGW(TAG,
             "registerService(): BLE not initialized yet (call init() first)");
    return false;
  }

  // Регистрировать нужно ДО настройки рекламы/старта
  if (_advConfigured || isAdvertising() || _pluginsInited) {
    ESP_LOGW(
        TAG,
        "registerService(): too late (advertising/plugins already initialized)");
    return false;
  }

  if (_pluginCount >= MAX_PLUGINS) {
    ESP_LOGW(TAG, "registerService(): MAX_PLUGINS reached (%u)",
             (unsigned)MAX_PLUGINS);
    return false;
  }

  _plugins[_pluginCount++] = &plugin;
  return true;
}

void BLE::initPluginsOnce() {
  if (_pluginsInited)
    return;
  if (!_server)
    return;

  for (size_t i = 0; i < _pluginCount; ++i) {
    if (_plugins[i]) {
      if (!_plugins[i]->init(_server)) {
        ESP_LOGW(TAG, "BLE plugin init failed at index %u", (unsigned)i);
      }
    }
  }

  _pluginsInited = true;
}

void BLE::pluginsConfigureAdvertising(NimBLEAdvertisementData &advData) {
  for (size_t i = 0; i < _pluginCount; ++i) {
    if (_plugins[i]) {
      _plugins[i]->configureAdvertising(advData);
    }
  }
}

void BLE::pluginsOnConnect(NimBLEConnInfo &connInfo, uint16_t mtu) {
  for (size_t i = 0; i < _pluginCount; ++i) {
    if (_plugins[i])
      _plugins[i]->onConnect(connInfo, mtu);
  }
}

void BLE::pluginsOnDisconnect(int reason) {
  for (size_t i = 0; i < _pluginCount; ++i) {
    if (_plugins[i])
      _plugins[i]->onDisconnect(reason);
  }
}

void BLE::pluginsOnMtuUpdated(uint16_t mtu) {
  for (size_t i = 0; i < _pluginCount; ++i) {
    if (_plugins[i])
      _plugins[i]->onMtuUpdated(mtu);
  }
}

void BLE::pluginsOnBleEnd() {
  for (size_t i = 0; i < _pluginCount; ++i) {
    if (_plugins[i]) {
      _plugins[i]->onBleEnd();
    }
  }
}

bool BLE::init(const String &deviceName) {
  if (_server) {
    ESP_LOGW(TAG, "BLE already initialized");
    return true;
  }

  ESP_LOGI(TAG, "Initializing BLE as '%s'...", deviceName.c_str());
  _deviceName = deviceName;

  _connected = false;
  _clientCount = 0;
  _advConfigured = false;

  // plugins
  _pluginCount = 0;
  _pluginsInited = false;

  // bonding=true, mitm=false, sc=true (или false, если будут проблемы)
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID);

  // Инициализация BLE устройства
  NimBLEDevice::init(deviceName.c_str());
  // Установка MTU для максимального размера пакетов
  NimBLEDevice::setMTU(517);

  // Создание сервера BLE
  _server = NimBLEDevice::createServer();
  if (!_server) {
    ESP_LOGE(TAG, "Failed to create BLE server");
    return false;
  }
  _server->setCallbacks(new ServerCallbacks());

  ESP_LOGI(TAG, "BLE initialized");
  return true;
}

void BLE::setupAdvertisingOnce() {
  if (_advConfigured)
    return;

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (!adv) {
    ESP_LOGW(TAG, "Cannot configure advertising: NimBLEAdvertising is null");
    return;
  }

  // Полный сброс один раз
  adv->stop();
  adv->reset();

  // ---------- Advertisement packet ----------
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);
  pluginsConfigureAdvertising(advData);
  adv->setAdvertisementData(advData);

  // ---------- Scan Response packet ----------
  // Имя устройства уходит сюда
  NimBLEAdvertisementData scanData;
  scanData.setName(_deviceName.c_str());
  adv->setScanResponseData(scanData);

  _advConfigured = true;
  ESP_LOGI(TAG, "Advertising payload configured once (ADV + scan response)");
}

void BLE::startAdvertising() {
  if (!_server) {
    ESP_LOGW(TAG, "Cannot start advertising: server not initialized");
    return;
  }

  // Перед рекламой гарантируем, что все плагины создали свои сервисы
  initPluginsOnce();

  if (!_advConfigured) {
    setupAdvertisingOnce();
    if (!_advConfigured)
      return; // не смогли сконфигурировать
  }

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (!adv) {
    ESP_LOGW(TAG, "Cannot start advertising: NimBLEAdvertising is null");
    return;
  }

  if (adv->isAdvertising()) {
    notifyStateChanged();
    return;
  }

  adv->start();
  ESP_LOGI(TAG, "Advertising started, isAdvertising=%d", adv->isAdvertising());
  notifyStateChanged();
}

void BLE::stopAdvertising() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (adv && adv->isAdvertising()) {
    adv->stop();
    ESP_LOGI(TAG, "Advertising stopped");
    notifyStateChanged();
  }
}

void BLE::end() {
  pluginsOnBleEnd();
  NimBLEDevice::deinit(true);

  _server = nullptr;

  _connected = false;
  _advConfigured = false;
  _clientCount = 0;

  // plugins
  _pluginCount = 0;
  _pluginsInited = false;
  for (size_t i = 0; i < MAX_PLUGINS; ++i)
    _plugins[i] = nullptr;

  notifyStateChanged();
}

bool BLE::isConnected() { return _connected; }

bool BLE::isAdvertising() {
  if (!_server)
    return false;
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  return adv ? adv->isAdvertising() : false;
}

BLEState BLE::getState() {
  if (_connected)
    return BLEState::CONNECTED;
  if (isAdvertising())
    return BLEState::ADVERTISING;
  return BLEState::DISCONNECTED;
}

void BLE::setStateCallback(BLEStateCallback callback) {
  _stateCallback = callback;
  notifyStateChanged();
}

// ============================================================================
// Internal Callbacks
// ============================================================================

void BLE::onConnect(NimBLEConnInfo &connInfo, uint16_t mtu) {
  _connected = true;
  if (_clientCount < UINT16_MAX) {
    ++_clientCount;
  }

  pluginsOnConnect(connInfo, mtu);
  notifyStateChanged();
}

void BLE::onDisconnect(int reason) {
  if (_clientCount > 0) {
    --_clientCount;
  }
  _connected = _clientCount > 0;

  pluginsOnDisconnect(reason);
  notifyStateChanged();
}

void BLE::onMtuUpdated(uint16_t mtu) { pluginsOnMtuUpdated(mtu); }

void BLE::notifyStateChanged() {
  // Уведомляем об изменении состояния с текущим состоянием
  if (_stateCallback)
    _stateCallback(getState());
}
