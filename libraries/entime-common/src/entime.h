#pragma once
#include <Arduino.h>
#include <TimeLib.h>
#include <DS3232RTC.h> // https://github.com/JChristensen/DS3232RTC
#include <Adafruit_ST7735.h>
#include <BluetoothSerial.h>
#include <Streaming.h>
#include <driver/adc.h> //для измерения напряжения питания
#include "esp_adc_cal.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Ticker.h>

#define VERSION "Summer 2021 Edit 6/07/21"
#define TIME_HEADER "T" // Header tag for serial time sync message
#define MODULE_TYPE "entime"

const int16_t MAX_DISCHARGE = 6500;
const int16_t MAX_CHARGE = 8200;

#define CHARGE_DIVIDER ((MAX_CHARGE - MAX_DISCHARGE) / 100)

typedef struct
{
  bool active;
  String name;
  uint8_t number;
} S_Bluetooth;

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
  S_LoRa LoRa;
  S_WiFi WiFi;
  S_TFT TFT;
  S_Buzzer Buzzer;
  S_VCC VCC;
} Settings;

extern DS3232RTC myRTC;
extern volatile time_t isrUTC;            //ISR's copy of current time in UTC
extern volatile unsigned long syncmillis; // значение millis() которое было во время последней синхронизации (каждую секунду)

extern const uint8_t EVENT_PIN;
extern const uint8_t RTC_1HZ_PIN;

extern BluetoothSerial SerialBT;
extern Preferences preferences;
extern Settings settings;
extern Adafruit_ST7735 tft;

#define DEFAULT_VREF 1100
#define NO_OF_SAMPLES 64 //Multisampling

extern esp_adc_cal_characteristics_t *adc_chars;
extern const adc_atten_t atten;
extern const adc_unit_t unit;

extern double v_divider;

extern Ticker tickerVcc;

extern bool isInterruptAttached; // выставлено ли прерывание по событию

/*
 * Конвертирует время в строку
 */
String timeToString(time_t time);

/*
 * Конвертирует время с тысячными в строку
 */
String timeToString(time_t time, int16_t millis);

/*
 * Конвертирует двузначное число в строку с ведущим нулём
 */
String digitsToString(int i);

void check_efuse();

/*
 * Конвертирует напряжение в процент заряда
 */
int16_t voltageToCharge(int32_t voltage);

/*
 * Выводит на экран напряжение VCC
 */
void batteryToTFT(uint8_t charge_y, int32_t voltage, int16_t charge);

/*
 * Отправляет в BLE процент заряда батареи
 */
void batteryToBLE(int32_t voltage, int16_t charge);

/*
 * Инициализация времени на экране
 */
void digitalClockTFTinit();

/*
 * Вывод текущего времени на экране
 */
void digitalClockTFT(time_t t);

/*
 * Загрузка настроек модуля
 */
void loadSettings(String MODULE_NAME);

/*
 * readBTSerial
 * read all data from BTSerial receive buffer
 * parse data for valid WiFi credentials
 */
String readBTSerial();

/*
 * Парсит JSON строку с настройками и записывает их
 * 
 * Возвращает JSON строку, если был запрос настроек {read: true}
 * (Для совместимости в BTSerial)
 */
String jsonToSettings(String json);

/*
 * Настройки в Json строку
 */
String settingsToJson();

/*
 * Печать значений напряжения на питании модуля
 */
void printVcc();

/*
 * Получение значения питающего напряжения
 */
uint32_t getVcc();

unsigned long processSyncMessage();

/*
 * return current time
 */
time_t getUTC();

/*
 * set the current time
 */
void setUTC(time_t utc);

/* 
 * 1Hz RTC interrupt handler increments the current time
 */
void incrementTime();

/*
 * синхронизация RTC и внутреннего времени из Serial порта
 */
void syncRTCfromSerial();

/*
 * Синхронизация внутреннего времени модуля со временем RTC
 */
void syncFromRTC();

void drawText(const char *text, uint16_t color);

void setupModule(String MODULE_NAME);