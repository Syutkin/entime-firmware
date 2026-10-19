#include "device_info_service.h"
#include "config.h"
#include <esp_mac.h>
#include <string.h>

BleDeviceInfoService deviceInfoService;

static void addReadString(NimBLEService* svc, uint16_t uuid16, const char* value) {
  auto* c = svc->createCharacteristic(NimBLEUUID(uuid16), NIMBLE_PROPERTY::READ);
  c->setValue(value ? value : "");
}

void BleDeviceInfoService::buildAutoSerialIfNeeded() {
  // Если серийник задан вручную и не пустой — ничего не делаем
  if (_serial && _serial[0] != '\0') return;

  uint8_t mac[6] = {0};

  // Берём стабильный MAC из eFuse (WiFi STA обычно постоянный)
  // ESP_MAC_WIFI_STA почти всегда доступен на ESP32/ESP32-S3.
  // Если вдруг вернёт ошибку — mac останется нулевой, но это редкость.
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  // Формат: PREFIX + 12 hex (без :)
  // Пример: ENTIME-742D782669D8
  const char* p = _serialPrefix ? _serialPrefix : BLE_DIS_SERIAL_PREFIX;
  size_t plen = strnlen(p, 12); // префикс ограничим разумно

  // _serialBuf size=32: хватает.
  snprintf(_serialBuf, sizeof(_serialBuf),
           "%.*s%02X%02X%02X%02X%02X%02X",
           (int)plen, p,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  _serial = _serialBuf;
}

bool BleDeviceInfoService::init(NimBLEServer* server) {
  buildAutoSerialIfNeeded();

  // Device Information Service (DIS) UUID: 0x180A
  _service = server->createService(NimBLEUUID((uint16_t)0x180A));
  if (!_service) {
    return false;
  }

  addReadString(_service, 0x2A29, _manufacturer); // Manufacturer Name
  addReadString(_service, 0x2A24, _model);        // Model Number
  addReadString(_service, 0x2A25, _serial);       // Serial Number
  addReadString(_service, 0x2A26, _fw);           // Firmware Revision
  addReadString(_service, 0x2A27, _hw);           // Hardware Revision
  addReadString(_service, 0x2A28, _sw);           // Software Revision

  return _service->start();
}
