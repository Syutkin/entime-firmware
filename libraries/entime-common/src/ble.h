#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h> //Library to use BLE as server
#include <BLE2902.h>
#include <entime.h>

extern BLECharacteristic BatteryLevelCharacteristic;
extern BLECharacteristic SettingsCharacteristic;
extern BLECharacteristic EventCharacteristic;

void InitBLE(String name);
