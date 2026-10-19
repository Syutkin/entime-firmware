#include "config.h"
#include "app/touch_action_handler.h"
#include "app/touch_calibration_service.h"
#include "command/command_parser.h"
#include "esp_log.h"
#include "hal/ble/battery_service.h"
#include "hal/ble/ble.h"
#include "hal/ble/device_info_service.h"
#include "hal/ble/nus_service.h"
#include "hal/wifi/wifi.h"
#include "hal/gps/gps.h"
#include "hal/ina226/ina226.h"
#include "hal/rtc/rtc.h"
#include "hal/tft/tft.h"
#include "hal/touch/touch.h"
#include "storage/settings.h"
#include "timing/event_isr.h"
#include "timing/event_dispatcher.h"
#include "timing/event_timestamp.h"
#include "timing/pps_isr.h"
#include "timing/second_events.h"
#include "timing/sntp.h"
#include "timing/time_sync.h"
#include "ui/main_screen.h"
#include "ui/footer.h"
#include "ui/main_area.h"
#include "ui/status_bar.h"
#include "ui/ui_config.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <time.h>

static const char *TAG = "MAIN";

static ModuleType module_type = ModuleType::START; // значение по умолчанию

// Парсеры команд для Serial и BLE (инициализируются в setup())
static CommandParser* serialParser = nullptr;
static CommandParser* bleParser = nullptr;

namespace {
Footer footer;
MainArea mainArea;
StatusBar statusBar;
MainScreen mainScreen;
TouchActionHandler touchActionHandler;
uint32_t lastBatteryInfoRedrawMs = 0;
uint32_t lastGpsSkyplotRedrawMs = 0;
} // namespace

/**
 * @brief Callback для обновления иконки Bluetooth при изменении состояния BLE
 * @param state Текущее состояние Bluetooth
 */
void onBLEStateChanged(BLEState state) { mainScreen.postBleState(state); }

/**
 * @brief Callback для обновления иконки WiFi при изменении состояния WiFi
 * @param state Текущее состояние WiFi
 * @param rssi Уровень сигнала в dBm
 */
void onWiFiStateChanged(WiFiState state, int8_t rssi) {
  mainScreen.postWiFiState(state, rssi);
}

/**
 * @brief Callback для обновления иконки GPS при изменении состояния
 * @param state Текущее состояние GPS
 */
void onGPSStateChanged(GPSState state) {
  const SyncSettings &sync = settings.getSync();
  if (sync.source == 2) {
    mainScreen.postGpsState(GPSState::OFF);
    return;
  }

  mainScreen.postGpsState(state);
}

void onGPSSatsChanged(int8_t sats) {
  mainScreen.postSats(sats);
}

void onTimeSyncStateChanged(TimeSyncState state) {
  mainScreen.postTimeSyncState(state);
}

void onBatteryStateChanged(InaBatteryLevel level, int percent, bool charging) {
  mainScreen.postBatteryState(level, charging);
  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;
  batteryService.setLevel(static_cast<uint8_t>(percent));
}

static void refreshBatteryInfoScreenIfVisible() {
  if (mainArea.getType() != MainAreaType::BATTERY) {
    lastBatteryInfoRedrawMs = 0;
    return;
  }

  const uint32_t now = millis();
  if (lastBatteryInfoRedrawMs != 0 &&
      now - lastBatteryInfoRedrawMs < 1000UL) {
    return;
  }

  lastBatteryInfoRedrawMs = now;
  mainArea.draw();
}

static void refreshGpsSkyplotIfVisible() {
  if (mainArea.getType() != MainAreaType::GPS) {
    lastGpsSkyplotRedrawMs = 0;
    return;
  }

  const uint32_t now = millis();
  if (lastGpsSkyplotRedrawMs != 0 &&
      now - lastGpsSkyplotRedrawMs < 1000UL) {
    return;
  }

  lastGpsSkyplotRedrawMs = now;
  mainArea.draw();
}

static bool routeTouchEvent(const TouchEvent &event) {
  UiTouchTarget target = UiTouchTarget::None;

  if (statusBar.onTouchEvent(event, target)) {
    if (event.type == TouchEventType::Press) {
      ESP_LOGI(TAG, "Touch route: ui=%s", uiTouchTargetText(target));
    }
    touchActionHandler.handleTouchEvent(event, target);
    return true;
  }
  if (mainArea.onTouchEvent(event, target)) {
    if (event.type == TouchEventType::Press) {
      ESP_LOGI(TAG, "Touch route: ui=%s", uiTouchTargetText(target));
    }
    return true;
  }
  if (footer.onTouchEvent(event, target)) {
    if (event.type == TouchEventType::Press) {
      ESP_LOGI(TAG, "Touch route: ui=%s", uiTouchTargetText(target));
    }
    return true;
  }
  if (event.type == TouchEventType::Press) {
    ESP_LOGI(TAG, "Touch route: ui=%s", uiTouchTargetText(UiTouchTarget::None));
  }
  return false;
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  ESP_LOGI(TAG, "ENTime v%s starting...", VERSION);
  ESP_LOGI(TAG, "Build date: %s", FIRMWARE_BUILD_DATE);

  ESP_LOGI(TAG, "tick rate: %d Hz", configTICK_RATE_HZ);

  // Инициализация дисплея (раньше, чтобы можно было показывать логи)
  display.begin(DISPLAY_ROTATION);

  // Инициализация статус-бара
  statusBar.init(display.tft());
  statusBar.draw();

  // Установка callback для мгновенного обновления иконки WiFi при
  // изменении состояния (независимо от текущих настроек)
  wifiManager.setStateCallback(onWiFiStateChanged);

  // Инициализация mainArea в режиме загрузки
  mainArea.init(display.tft());
  touchActionHandler.init(mainArea, settings, wifiManager);
  mainArea.setType(MainAreaType::LOADING);
  mainArea.draw();
  mainArea.addLogLine("ENTime v" VERSION " starting...");
  mainArea.addLogLine(String("Build date: ") + FIRMWARE_BUILD_DATE);
  ESP_LOGI(TAG, "ENTime v" VERSION " starting...");
  ESP_LOGI(TAG, "Build date: %s", FIRMWARE_BUILD_DATE);

  // Инициализация настроек (загрузка из NVS)
  mainArea.addLogLine("Loading settings...");
  if (!settings.begin()) {
    ESP_LOGE(TAG, "Failed to initialize settings manager");
    mainArea.addLogLine("ERROR: Settings init failed");
    mainArea.addLogLine("Default settings loaded");
    delay(2000);
    // Продолжаем работу с настройками по умолчанию
  } else {
    mainArea.addLogLine("Settings loaded");
  }

  touch.begin(display.tft(), settings.getTouch(), DISPLAY_ROTATION);

  const TouchSettings &touchSettings = settings.getTouch();
  if (touchSettings.enabled && !touch.isCalibrated()) {
    mainArea.addLogLine("Touch calibration required");
    ESP_LOGW(TAG,
             "Touch is enabled but not calibrated, running calibration wizard");

    while (true) {
      TouchCalibrationFlowResult calibrationResult = runTouchCalibrationFlow(true);
      if (calibrationResult.status == TouchCalibrationFlowStatus::Ok) {
        mainArea.addLogLine("Touch calibration complete");
        ESP_LOGI(TAG, "Touch calibration saved, keys updated: %d",
                 calibrationResult.savedKeys);
        break;
      }

      ESP_LOGE(TAG, "Touch calibration failed: %s",
               touchCalibrationFlowStatusText(calibrationResult.status));
    }
  }

  // Инициализация типа модуля из настроек и формирование имени устройства
  const DeviceSettings &device = settings.getDevice();
  if (device.type == 1) {
    module_type = ModuleType::START;
  } else if (device.type == 2) {
    module_type = ModuleType::FINISH;
  } else {
    ESP_LOGW(TAG, "Unknown device type %u, defaulting to START", device.type);
    module_type = ModuleType::START;
  }
  ESP_LOGI(TAG, "Module type: %u (%s)", device.type,
           module_type == ModuleType::START ? "START" : "FINISH");
  String moduleTypeStr =
      "Module type: " +
      String(module_type == ModuleType::START ? "START" : "FINISH");
  mainArea.addLogLine(moduleTypeStr);

  // Инициализация footer после определения типа модуля.
  footer.init(display.tft(), module_type, String(VERSION));
  mainScreen.init(footer, mainArea, statusBar);

  // Инициализация прерывания на событие
  event_isr_init(EXT_INT_PIN);
  ESP_LOGI(TAG, "Event ISR initialized");

  // Инициализация GPS
  const GpsSettings &gpsSettings = settings.getGps();
  if (gpsSettings.enabled) {
    mainArea.addLogLine("Initializing GPS...");
  } else {
    mainArea.addLogLine("GPS disabled in settings");
  }
  gps.begin(gpsSettings.enabled);
  if (gpsSettings.enabled) {
    mainArea.addLogLine("GPS initialized");
  }
  gps.setStateCallback(onGPSStateChanged);
  gps.setSatsCallback(onGPSSatsChanged);
  onGPSStateChanged(gps.getState());

  // Инициализация Wire
  if (Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
    ESP_LOGI(TAG, "Wire initialized");
    mainArea.addLogLine("Wire initialized");
  } else {
    ESP_LOGE(TAG, "Wire init failed");
    mainArea.addLogLine("ERROR: Wire init failed");
  }

  // Инициализация RTC
  mainArea.addLogLine("Initializing RTC...");
  if (rtc.begin()) {
    mainArea.addLogLine("RTC initialized");
    // Проверка потери питания
    if (rtc.lostPower()) {
      mainArea.addLogLine("WARN: RTC lost power");
      delay(2000);
    }
  } else {
    mainArea.addLogLine("ERROR: RTC init failed");
    delay(5000);
  }

  // Инициализация INA226
  mainArea.addLogLine("Initializing INA226...");
  ina226.setBatteryStateChangedCallback(onBatteryStateChanged);
  if (ina226.begin()) {
    mainArea.addLogLine("INA226 initialized");
  } else {
    mainArea.addLogLine("WARN: INA226 init failed");
    mainScreen.postBatteryState(InaBatteryLevel::NoData, false);
  }

  // PPS синхронизация от GPS
  pps_init(GPS_PPS_PIN);

  // Инициализация подсистемы синхронизации времени
  mainArea.addLogLine("Starting time sync...");
  time_sync_begin();
  time_sync_set_state_callback(onTimeSyncStateChanged);
  ESP_LOGI(TAG, "Time sync initialized");
  mainArea.addLogLine("Time sync initialized");

  // Инициализация BLE (Nordic UART Service)
  mainArea.addLogLine("Initializing BLE...");

  // Формирование имени устройства: "Имя-Номер"
  String deviceName = device.name + "-" + String(device.number);
  String deviceNameStr = "Device: " + deviceName;
  mainArea.addLogLine(deviceNameStr);

  if (ble.init(deviceName)) {
    ESP_LOGI(TAG, "BLE initialized as: %s", deviceName.c_str());

    // Инициализация NUS сервиса
    mainArea.addLogLine("Initializing NUS service...");
    if (ble.registerService(nusService)) {
      mainArea.addLogLine("NUS service initialized");
      ESP_LOGI(TAG, "NUS service initialized");
    } else {
      mainArea.addLogLine("WARN: NUS service register failed");
      ESP_LOGW(TAG, "NUS service registration failed");
    }

    // Инициализация сервиса батареи
    mainArea.addLogLine("Initializing battery service...");
    if (ble.registerService(batteryService)) {
      mainArea.addLogLine("Battery service initialized");
      ESP_LOGI(TAG, "Battery service initialized");
    } else {
      mainArea.addLogLine("WARN: Battery service register failed");
      ESP_LOGW(TAG, "Battery service registration failed");
    }

    // Инициализация сервиса устройства
    mainArea.addLogLine("Initializing device info service...");
    if (ble.registerService(deviceInfoService)) {
      mainArea.addLogLine("Device info service initialized");
      ESP_LOGI(TAG, "Device info service initialized");
    } else {
      mainArea.addLogLine("WARN: Device info service register failed");
      ESP_LOGW(TAG, "Device info service registration failed");
    }

    // Запуск рекламы
    ble.startAdvertising();
  } else {
    mainArea.addLogLine("ERROR: BLE init failed");
    ESP_LOGE(TAG, "BLE init failed");
  }

  // Установка callback для мгновенного обновления иконки Bluetooth при
  // изменении состояния
  ble.setStateCallback(onBLEStateChanged);

  // Небольшая задержка для того, чтобы реклама успела запуститься
  // delay(50);

  ESP_LOGI(TAG, "BLE ready");
  mainArea.addLogLine("BLE ready");

  // Инициализация парсеров команд для Serial и BLE
  serialParser = new CommandParser(Serial, "Serial");
  bleParser = new CommandParser(nusService, "BLE", 1024, [] {
    return nusService.connectionGeneration();
  });
  ESP_LOGI(TAG, "Command parsers initialized");

  // Инициализация WiFi (если включен в настройках)
  const WifiSettings &wifi = settings.getWifi();
  if (wifi.active && wifi.ssid.length() > 0) {
    mainArea.addLogLine("Initializing WiFi...");
    wifiManager.begin();
    // Подключение к WiFi сети
    if (wifiManager.connect(wifi.ssid, wifi.passwd)) {
      ESP_LOGI(TAG, "WiFi connecting to: %s", wifi.ssid.c_str());
      mainArea.addLogLine("WiFi connecting...");
    } else {
      ESP_LOGE(TAG, "WiFi connect failed");
      mainArea.addLogLine("ERROR: WiFi connect failed");
    }
  } else {
    ESP_LOGI(TAG, "WiFi disabled in settings");
    mainScreen.postWiFiState(WiFiState::OFF, 0);
  }

  ESP_LOGI(TAG, "Setup complete");
  mainArea.addLogLine("Setup complete");

  // Пауза для чтения лога загрузки (3 секунды)
  // delay(5000);

  // Установка типа в зависимости от типа модуля
  if (module_type == ModuleType::START) {
    mainArea.setType(MainAreaType::START);
  } else {
    mainArea.setType(MainAreaType::FINISH);
  }

  mainScreen.draw();

  mainScreen.postTimeSyncState(time_sync_state());
  mainScreen.update();
}

void loop() {
  // Секундные события (BEEP/VOICE/Часы/Countdown и др.)
  second_events_handle_tick(module_type, statusBar, mainArea);

  touch.update();
  TouchEvent touchEvent;
  while (touch.pollEvent(touchEvent)) {
    routeTouchEvent(touchEvent);
  }

  // Обновление состояния синхронизации времени в footer
  // Обновление GPS
  gps.update();

  // Обновление синхронизации времени
  time_sync_update();

  // Обновление WiFi (обработка событий, обновление RSSI)
  wifiManager.update();

  // Обработка INA226 (DATA_READY через ISR flag)
  ina226.update();
  refreshBatteryInfoScreenIfVisible();
  refreshGpsSkyplotIfVisible();

  // Весь UI-рендер из одного контекста (loop task).
  mainScreen.update();

  // Обработка команд из Serial и BLE
  if (serialParser) {
    serialParser->update();
  }
  if (bleParser) {
    bleParser->update();
  }

  if (consumeTouchCalibrationUiRedrawRequest()) {
    mainScreen.draw();
  }

  // Проверка события и обработка временного штампа
  event_dispatcher_handle_event_isr(module_type, mainArea);
}
