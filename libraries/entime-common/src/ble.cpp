#include "ble.h"

bool _BLEClientConnected = false;

#define BATTERY_SERVICE_UUID BLEUUID((uint16_t)0x180F) //GATT Service 0x180F Battery

#define SERIAL_SERVICE_UUID "3d1182b8-ec3a-42c3-b1b4-70b28578fab3"
#define SETTINGS_CHARACTERISTIC_UUID "895bcbd5-4cdf-40fb-9461-0801697b83aa"
#define EVENT_CHARACTERISTIC_UUID "f6af336c-8da1-4030-95bc-5e3ce2119305"

BLECharacteristic BatteryLevelCharacteristic(BLEUUID((uint16_t)0x2A19), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY); //GATT Characteristic and Object Type 0x2A19 Battery Level
BLEDescriptor BatteryLevelDescriptor(BLEUUID((uint16_t)0x2901));                                                                                //GATT Descriptor 0x2901 Characteristic User Description

BLECharacteristic SettingsCharacteristic(SETTINGS_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
BLEDescriptor SettingsDescriptor(BLEUUID((uint16_t)0x2901)); //GATT Descriptor 0x2901 Characteristic User Description

BLECharacteristic EventCharacteristic(EVENT_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
BLEDescriptor EventDescriptor(BLEUUID((uint16_t)0x2901)); //GATT Descriptor 0x2901 Characteristic User Description

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    _BLEClientConnected = true;
    pServer->getAdvertising()->start();
  }

  void onDisconnect(BLEServer *pServer)
  {
    _BLEClientConnected = false;
    pServer->getAdvertising()->start();
  }
};

class SettingsCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    String data = pCharacteristic->getValue().c_str();
    log_i("From BLE: %s", data.c_str());
    if (!data.isEmpty())
    {
      String json = jsonToSettings(data);
      pCharacteristic->setValue(settingsToJson().c_str());
    }
  }
};

void InitBLE(String name)
{
  BLEDevice::init(name.c_str());
  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the Battery Service
  BLEService *pBattery = pServer->createService(BATTERY_SERVICE_UUID);

  pBattery->addCharacteristic(&BatteryLevelCharacteristic);
  BatteryLevelDescriptor.setValue("Percentage 0 - 100");
  BatteryLevelCharacteristic.addDescriptor(&BatteryLevelDescriptor);
  BatteryLevelCharacteristic.addDescriptor(new BLE2902());

  pServer->getAdvertising()->addServiceUUID(BATTERY_SERVICE_UUID);

  pBattery->start();

  // Create Settings Service
  BLEService *pSerial = pServer->createService(SERIAL_SERVICE_UUID);
  pSerial->addCharacteristic(&SettingsCharacteristic);
  SettingsCharacteristic.setCallbacks(new SettingsCallbacks());

  SettingsCharacteristic.setValue(settingsToJson().c_str());

  SettingsDescriptor.setValue("Module settings");
  SettingsCharacteristic.addDescriptor(&SettingsDescriptor);
  SettingsCharacteristic.addDescriptor(new BLE2902());

  // Create Event Service
  pSerial->addCharacteristic(&EventCharacteristic);

  EventCharacteristic.setValue("");

  EventDescriptor.setValue("Varios time events");
  EventCharacteristic.addDescriptor(&EventDescriptor);
  EventCharacteristic.addDescriptor(new BLE2902());

  pServer->getAdvertising()->addServiceUUID(SERIAL_SERVICE_UUID);

  pSerial->start();

  // Start advertising
  pServer->getAdvertising()->setScanResponse(true);
  // pServer->getAdvertising()->setMinPreferred(0x06); // functions that help with iPhone connections issue
  // pServer->getAdvertising()->setMinPreferred(0x12);
  pServer->getAdvertising()->start();
}
