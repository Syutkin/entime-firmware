#include "battery_service.h"
#include "esp_log.h"

static const char* TAG = "BatteryService";

BleBatteryService batteryService;

class BatteryLevelCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic * /*pCharacteristic*/,
                     NimBLEConnInfo & /*connInfo*/, uint16_t subValue) override {
      bool notify = (subValue & 0x01) != 0;
      ESP_LOGI(TAG, "Battery notify %s", notify ? "enabled" : "disabled");
      batteryService.onNotifyStateChanged(notify);
    }
  };

static BatteryLevelCallbacks s_batteryLevelCallbacks;

bool BleBatteryService::init(NimBLEServer* server) {
    // init() вызывается один раз перед advertising
    _notifyEnabled = false;

    _service = server->createService(NimBLEUUID((uint16_t)0x180F));
    if (!_service) {
      ESP_LOGE(TAG, "Failed to create Battery Service");
      return false;
    }

    _levelChar = _service->createCharacteristic(
        NimBLEUUID((uint16_t)0x2A19),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    if (!_levelChar) {
      ESP_LOGE(TAG, "Failed to create Battery Level characteristic");
      return false;
    }
    _levelChar->setCallbacks(&s_batteryLevelCallbacks);

    uint8_t initial = 100;
    _levelChar->setValue(&initial, 1);
    _lastLevel = initial;
    return true;
}

void BleBatteryService::setLevel(uint8_t percent) {
    if (!_levelChar) return;

    if (percent > 100) percent = 100;
    if (percent == _lastLevel) return;

    _lastLevel = percent;
    _levelChar->setValue(&percent, 1);

    if (_notifyEnabled) {
        _levelChar->notify();
    }
}

void BleBatteryService::onNotifyStateChanged(bool enabled) {
    _notifyEnabled = enabled;
  }
  
