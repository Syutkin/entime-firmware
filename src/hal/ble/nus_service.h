#pragma once

#include <NimBLEDevice.h>
#include <atomic>

#include "ble.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

class BleNusService : public IBleServicePlugin, public Stream {
public:
  bool init(NimBLEServer *server) override;
  void onConnect(NimBLEConnInfo & /*connInfo*/, uint16_t mtu) override;
  void onDisconnect(int /*reason*/) override;
  void onMtuUpdated(uint16_t mtu) override;
  void onBleEnd() override;
  void configureAdvertising(NimBLEAdvertisementData &advData) override;

  bool isConnected() const;
  uint32_t connectionGeneration() const {
    return _connectionGeneration.load(std::memory_order_acquire);
  }

  // Один надежный пакет в RAM; остальные записи сначала завершают его отправку.
  static constexpr size_t kMaxPendingPacketSize = 40;
  bool queuePacket(const uint8_t *buffer, size_t size);
  bool sendPendingPacket();

  // Stream
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Для внутреннего callback-хендлинга.
  void onReceive(const uint8_t *data, size_t len);
  void onNotifyStateChanged(bool enabled);

private:
  void resetRx_();
  size_t writeDirect_(const uint8_t *buffer, size_t size);
  uint8_t _pendingPacket[kMaxPendingPacketSize]{};
  size_t _pendingSize = 0;
  size_t _pendingSent = 0;
  uint32_t _pendingConnection = 0;

  NimBLEService *_service = nullptr;
  NimBLECharacteristic *_rxCharacteristic = nullptr;
  NimBLECharacteristic *_txCharacteristic = nullptr;

  StreamBufferHandle_t _rxStream = nullptr;
  int _peekedByte = -1;
  bool _hasPeeked = false;

  bool _connected = false;
  bool _notifyEnabled = false;
  std::atomic<uint32_t> _connectionGeneration{0};
  uint16_t _mtu = 23;
};

extern BleNusService nusService;
