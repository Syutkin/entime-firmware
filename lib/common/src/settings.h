#pragma once
#include <Streaming.h>
#include <ArduinoJson.h>
#include <Preferences.h>

typedef struct
{
  bool active;
  String name;
  uint8_t number;
} S_Bluetooth;

typedef struct
{
  int8_t timezone;
} S_Time;

typedef struct
{
  bool active;
  uint32_t frequency;       //433E6
  uint8_t txPower;          // 20
  uint8_t spreadingFactor;  // number = 12
  uint32_t signalBandwidth; // number = 125E3
  uint8_t codingRateDenom;  // number = 5
  uint16_t preambleLength;  // number = 8
  byte syncWord;
  bool crc; //b = false
} S_LoRa;

typedef struct
{
  bool active;
  String ssid;
  String passwd;
} S_WiFi;

typedef struct
{
  bool active;
  bool timeout;             // b = false
  uint16_t timeoutDuration; // number = 5
  bool turnOnAtEvent;       // b = false
} S_TFT;

typedef struct
{
  bool active;
  uint16_t shortFrequency; // number = 659
  uint16_t longFrequency;  // number = 659
} S_Buzzer;

typedef struct
{
  uint16_t r1;   //= 4700
  uint16_t r2;   //= 1000
  uint16_t vbat; //милливольты на батарее
} S_VCC;

typedef struct
{
  S_Bluetooth Bluetooth;
  S_Time Time;
  S_LoRa LoRa;
  S_WiFi WiFi;
  S_TFT TFT;
  S_Buzzer Buzzer;
  S_VCC VCC;
} Settings;

void loadSettings(String MODULE_NAME);
String fromJson(String json);
String toJson();

// extern Preferences preferences;
extern Settings settings;
