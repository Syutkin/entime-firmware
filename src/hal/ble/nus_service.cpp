#include "nus_service.h"

#include <algorithm>

#include "esp_log.h"

static const char *TAG = "NusService";
static constexpr const char *NUS_SERVICE_UUID =
    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char *NUS_RX_CHARACTERISTIC =
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // Client -> ESP (Write)
static constexpr const char *NUS_TX_CHARACTERISTIC =
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // ESP -> Client (Notify)
static constexpr size_t NUS_RX_BUFFER_SIZE = 32768;

BleNusService nusService;

class NusRxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pCharacteristic,
               NimBLEConnInfo & /*connInfo*/) override {
    std::string value = pCharacteristic->getValue();
    if (!value.empty()) {
      nusService.onReceive(reinterpret_cast<const uint8_t *>(value.data()),
                           value.size());
    }
  }
};

class NusTxCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic * /*pCharacteristic*/,
                   NimBLEConnInfo & /*connInfo*/, uint16_t subValue) override {
    bool notify = (subValue & 0x01) != 0;
    ESP_LOGI(TAG, "Notify %s", notify ? "enabled" : "disabled");
    nusService.onNotifyStateChanged(notify);
  }
};

static NusRxCallbacks s_nusRxCallbacks;
static NusTxCallbacks s_nusTxCallbacks;

bool BleNusService::init(NimBLEServer *server) {
  if (_rxStream) {
    vStreamBufferDelete(_rxStream);
    _rxStream = nullptr;
  }

  _connected = false;
  _notifyEnabled = false;
  _mtu = 23;
  _peekedByte = -1;
  _hasPeeked = false;

  _rxStream = xStreamBufferCreate(NUS_RX_BUFFER_SIZE, 1);
  if (!_rxStream) {
    ESP_LOGE(TAG, "Failed to create RX StreamBuffer (%u bytes)",
             (unsigned)NUS_RX_BUFFER_SIZE);
    return false;
  }
  ESP_LOGI(TAG, "RX StreamBuffer created: %u bytes",
           (unsigned)NUS_RX_BUFFER_SIZE);

  _service = server->createService(NUS_SERVICE_UUID);
  if (!_service) {
    ESP_LOGE(TAG, "Failed to create NUS service");
    onBleEnd();
    return false;
  }

  _rxCharacteristic =
      _service->createCharacteristic(NUS_RX_CHARACTERISTIC,
                                     NIMBLE_PROPERTY::WRITE |
                                         NIMBLE_PROPERTY::WRITE_NR);
  if (!_rxCharacteristic) {
    ESP_LOGE(TAG, "Failed to create NUS RX characteristic");
    onBleEnd();
    return false;
  }
  _rxCharacteristic->setCallbacks(&s_nusRxCallbacks);

  _txCharacteristic =
      _service->createCharacteristic(NUS_TX_CHARACTERISTIC,
                                     NIMBLE_PROPERTY::NOTIFY);
  if (!_txCharacteristic) {
    ESP_LOGE(TAG, "Failed to create NUS TX characteristic");
    onBleEnd();
    return false;
  }
  _txCharacteristic->setCallbacks(&s_nusTxCallbacks);

  ESP_LOGI(TAG, "NUS initialized");
  return true;
}

void BleNusService::onConnect(NimBLEConnInfo & /*connInfo*/, uint16_t mtu) {
  _connectionGeneration.fetch_add(1, std::memory_order_relaxed);
  _connected = true;
  _notifyEnabled = false;
  _mtu = mtu;
}

void BleNusService::onDisconnect(int /*reason*/) {
  _connected = false;
  _notifyEnabled = false;
}

bool BleNusService::isConnected() const { return _connected; }

void BleNusService::onMtuUpdated(uint16_t mtu) { _mtu = mtu; }

void BleNusService::onBleEnd() {
  _service = nullptr;
  _rxCharacteristic = nullptr;
  _txCharacteristic = nullptr;

  _connected = false;
  _notifyEnabled = false;
  _mtu = 23;

  _peekedByte = -1;
  _hasPeeked = false;

  if (_rxStream) {
    vStreamBufferDelete(_rxStream);
    _rxStream = nullptr;
  }
}

void BleNusService::configureAdvertising(NimBLEAdvertisementData &advData) {
  advData.addServiceUUID(NUS_SERVICE_UUID);
}

int BleNusService::available() {
  if (!_rxStream)
    return 0;
  size_t avail = xStreamBufferBytesAvailable(_rxStream);
  return static_cast<int>(avail + (_hasPeeked ? 1 : 0));
}

int BleNusService::read() {
  if (_hasPeeked) {
    _hasPeeked = false;
    return _peekedByte;
  }
  if (!_rxStream)
    return -1;

  uint8_t c;
  if (xStreamBufferReceive(_rxStream, &c, 1, 0) == 1)
    return c;
  return -1;
}

int BleNusService::peek() {
  if (_hasPeeked)
    return _peekedByte;
  if (!_rxStream)
    return -1;

  uint8_t c;
  if (xStreamBufferReceive(_rxStream, &c, 1, 0) == 1) {
    _peekedByte = c;
    _hasPeeked = true;
    return c;
  }
  return -1;
}

void BleNusService::flush() {
  // noop
}

size_t BleNusService::write(uint8_t c) { return write(&c, 1); }

size_t BleNusService::write(const uint8_t *buffer, size_t size) {
  if (!sendPendingPacket())
    return 0;
  return writeDirect_(buffer, size);
}

bool BleNusService::queuePacket(const uint8_t *buffer, size_t size) {
  if (_pendingSize != 0 || !buffer || size == 0 || size > kMaxPendingPacketSize)
    return false;
  std::copy_n(buffer, size, _pendingPacket);
  _pendingSize = size;
  _pendingSent = 0;
  _pendingConnection = _connectionGeneration.load(std::memory_order_relaxed);
  return true;
}

bool BleNusService::sendPendingPacket() {
  if (_pendingSize == 0)
    return true;

  const uint32_t connection = _connectionGeneration.load(std::memory_order_relaxed);
  if (connection != _pendingConnection) {
    // Новый клиент не получал начало пакета предыдущего подключения.
    _pendingConnection = connection;
    _pendingSent = 0;
  }
  _pendingSent += writeDirect_(_pendingPacket + _pendingSent,
                               _pendingSize - _pendingSent);
  if (_connectionGeneration.load(std::memory_order_relaxed) != connection) {
    _pendingSent = 0;
    return false;
  }
  if (_pendingSent != _pendingSize)
    return false;

  _pendingSize = 0;
  _pendingSent = 0;
  return true;
}

size_t BleNusService::writeDirect_(const uint8_t *buffer, size_t size) {
  if (!_connected || !_txCharacteristic || !_notifyEnabled || !buffer ||
      size == 0) {
    return 0;
  }

  const size_t maxChunk = (_mtu > 3) ? (_mtu - 3) : 20;
  const int maxRetries = 50;
  size_t sent = 0;
  const uint32_t connection = _connectionGeneration.load(std::memory_order_relaxed);

  while (sent < size) {
    if (!_connected || !_notifyEnabled ||
        _connectionGeneration.load(std::memory_order_relaxed) != connection)
      return sent;
    size_t chunk = std::min(maxChunk, size - sent);
    _txCharacteristic->setValue(buffer + sent, chunk);

    int retries = 0;
    while (!_txCharacteristic->notify()) {
      retries++;
      if (retries >= maxRetries) {
        ESP_LOGW(TAG, "Notify failed after %d retries, sent %u/%u bytes",
                 maxRetries, (unsigned)sent, (unsigned)size);
        return sent;
      }

      if (retries < 20) {
        vTaskDelay(pdMS_TO_TICKS(1));
      } else if (retries < 30) {
        vTaskDelay(pdMS_TO_TICKS(2));
      } else if (retries < 40) {
        vTaskDelay(pdMS_TO_TICKS(4));
      } else {
        vTaskDelay(pdMS_TO_TICKS(8));
      }

      if (!_connected || !_notifyEnabled ||
          _connectionGeneration.load(std::memory_order_relaxed) != connection)
        return sent;
    }

    sent += chunk;
  }

  return sent;
}

void BleNusService::onReceive(const uint8_t *data, size_t len) {
  if (!_rxStream || !data || len == 0)
    return;

  size_t pushed = xStreamBufferSend(_rxStream, data, len, 0);
  if (pushed < len) {
    static uint32_t lastOverflowLog = 0;
    static size_t overflowEvents = 0;

    overflowEvents++;
    uint32_t now = xTaskGetTickCount();

    if (now - lastOverflowLog > pdMS_TO_TICKS(1000)) {
      ESP_LOGW(TAG, "RX overflow: lost %u bytes (%u events)",
               (unsigned)(len - pushed), (unsigned)overflowEvents);
      lastOverflowLog = now;
      overflowEvents = 0;
    }
  }
}

void BleNusService::onNotifyStateChanged(bool enabled) {
  _notifyEnabled = enabled;
}
