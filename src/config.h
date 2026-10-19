#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// ENTime Firmware Configuration
// ============================================================================

#include "runtime/build_info.h"

#define VERSION "0.2.0"
#define HARDWARE "v1"
#define PROTOCOL "v1"
#define FIRMWARE_BUILD_DATE firmware_build_date_iso()

// --- TFT Display (ILI9341) ---
// Пины определены в platformio.ini через build_flags для TFT_eSPI
// TFT_CS=10, TFT_DC=18, TFT_RST=14
// TFT_MOSI=11, TFT_MISO=13, TFT_SCLK=12
// TOUCH_CS=17
#define DISPLAY_ROTATION 2 // 0..3 (по умолчанию portrait)

// --- GPS NEO-M8N (UART1) ---
// Внимание: GPIO 19/20 заняты USB! Используем 4/5/6
#define GPS_RX_PIN      4
#define GPS_TX_PIN      5
#define GPS_PPS_PIN     6
#define GPS_BAUD        9600
// GPS power switch control.
#define GPS_POWER_PIN   38

// --- RTC DS3231 + INA226 (I2C) ---
// Общая I2C шина для RTC и мониторинга питания (INA226)
#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9
#define RTC_SQW_PIN     7

// --- INA226 ---
#define INA226_I2C_ADDRESS 0x40
#define INA226_ALERT_PIN   16
// Реальные параметры: шунт R100 (0.1 Ом), целевой пиковый ток нагрузки 0.5 А.
#define INA226_SHUNT_OHMS    0.1f
#define INA226_MAX_CURRENT_A 0.5f
static_assert((INA226_SHUNT_OHMS * INA226_MAX_CURRENT_A) <= 0.0819f,
              "INA226 config invalid: maxCurrent * shunt must be <= 0.0819V");
// Целевой период обновления по DATA_READY (примерно): 1000 = ~1 сек, 5000 = ~5 сек.
#define INA226_SAMPLE_PERIOD_MS 1000UL
// Отрицательный ток ниже этого порога считаем зарядкой.
#define INA226_CHARGING_CURRENT_THRESHOLD_A 0.020f
static_assert(INA226_CHARGING_CURRENT_THRESHOLD_A > 0.0f,
              "INA226 charging threshold must be positive");

// --- INA226 battery level thresholds (Li-ion 2S) ---
#define INA226_BAT_CRITICAL_MAX_V   6.6f
#define INA226_BAT_EMPTY_MAX_V      6.8f
#define INA226_BAT_LOW_MAX_V        7.3f
#define INA226_BAT_MID_MAX_V        8.0f
#define INA226_BAT_HYSTERESIS_V     0.1f
static_assert(INA226_BAT_CRITICAL_MAX_V < INA226_BAT_EMPTY_MAX_V,
              "INA226 thresholds invalid: CRITICAL must be < EMPTY");
static_assert(INA226_BAT_EMPTY_MAX_V < INA226_BAT_LOW_MAX_V,
              "INA226 thresholds invalid: EMPTY must be < LOW");
static_assert(INA226_BAT_LOW_MAX_V < INA226_BAT_MID_MAX_V,
              "INA226 thresholds invalid: LOW must be < MID");

// --- External Interrupt ---
#define EXT_INT_PIN     15

// --- Serial ---
#define SERIAL_BAUD     115200

// --- BLE ---
#define BLE_DEVICE_NAME "ENTime"

// --- BLE Device Information Service (DIS) ---
#define BLE_DIS_MANUFACTURER  BLE_DEVICE_NAME  // Manufacturer Name
#define BLE_DIS_MODEL         "Gate"           // Model Number
#define BLE_DIS_HARDWARE      HARDWARE         // Hardware Revision
#define BLE_DIS_FIRMWARE      VERSION          // Firmware version
#define BLE_DIS_SOFTWARE      PROTOCOL         // Protocol Revision
#define BLE_DIS_SERIAL_PREFIX "FR-"            // Serial Number prefix for auto-generation

// --- SNTP/NTP ---
#define SNTP_SYNC_TIMEOUT_MS    15000
#define SNTP_EDGE_TIMEOUT_MS    2500
#define SNTP_EDGE_WINDOW_US     1500
#define SNTP_POLL_DELAY_MS      50
#define SNTP_EDGE_POLL_DELAY_MS 1

// --- Time validation ---
// Минимальное доверенное UTC время: 2020-01-01 00:00:00.
#define MIN_VALID_UNIX_SEC 1577836800UL

// --- Event packets ---
#define START_HEADER '$'
#define FINISH_HEADER 'F'
#define BEEP_HEADER 'B'
#define VOICE_HEADER 'V'
#define PACKET_ENDER '#'

#define MAX_CORRECTION_MS 15000U
#define START_EVENT_DELAY_US 2000000U

// --- Module Type ---
// Тип модуля устройства
enum class ModuleType {
  START,
  FINISH
};

#endif // CONFIG_H
