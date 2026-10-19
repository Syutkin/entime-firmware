#include "handlers.h"
#include "app/touch_calibration_service.h"
#include "timing/sntp.h"
#include "storage/settings.h"
#include "hal/wifi/wifi.h"
#include "hal/ble/ble.h"
#include "hal/gps/gps.h"
#include "hal/ina226/ina226.h"
#include "hal/rtc/rtc.h"
#include "hal/touch/touch.h"
#include "timing/time_sync.h"
#include "timing/pps_isr.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstdlib>
#include <esp_timer.h>
#include <esp_system.h>

static const char *TAG = "CommandHandlers";

void cmdPing(JsonDocument& request, Stream& output) {
    JsonDocument response;
    
    // Копируем поле id из запроса, если оно есть
    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }
    
    // Формируем ответ
    response["cmd"] = "pong";
    response["status"] = "ok";
    
    sendResponse(response, output, true);
    
    ESP_LOGI(TAG, "Ping command processed, sent pong response");
}

void cmdTime(JsonDocument& request, Stream& output) {
    JsonDocument response;
    
    // Копируем поле id из запроса, если оно есть
    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }
    
    response["cmd"] = "time";
    
    // Получаем текущее время ESP таймера
    int64_t esp_time_us = esp_timer_get_time();
    
    // Конвертируем в UTC микросекунды
    int64_t utc_time_us = 0;
    bool time_valid = time_sync_esp_to_utc_us(esp_time_us, utc_time_us);
    
    // Получаем статус синхронизации
    TimeSyncStatus sync_status = time_sync_status();
    
    if (!time_valid || sync_status.source == TimeSource::NONE) {
        // Нет источника времени
        const char* cmd = request["cmd"].as<const char*>();
        sendError(cmd, 201, "RTC not initialized or no time source available", request["id"], output);
        ESP_LOGW(TAG, "Time command failed: no time source available");
        return;
    }
    
    // Определяем источник времени и точность
    const char* source_str = "none";
    switch (sync_status.source) {
        case TimeSource::GPS_PPS:
            source_str = "gps";
            break;
        case TimeSource::RTC:
            source_str = "rtc";
            break;
        case TimeSource::NONE:
        default:
            source_str = "none";
            break;
    }
    int64_t accuracy_us = time_sync_estimate_accuracy_us();
    
    // Определяем статус ответа
    const char* status_str = "ok";
    if (sync_status.source == TimeSource::RTC && !sync_status.gps_time_valid) {
        // RTC используется, но GPS был потерян
        status_str = "warning";
    }
    
    // Формируем ответ
    response["time"] = utc_time_us;
    response["source"] = source_str;
    response["accuracy_us"] = accuracy_us;
    response["status"] = status_str;
    
    sendResponse(response, output, true);
    
    ESP_LOGI(TAG, "Time command processed: time=%lld us, source=%s, accuracy=%lld us, status=%s",
             (long long)utc_time_us, source_str, (long long)accuracy_us, status_str);
}

void cmdStatus(JsonDocument& request, Stream& output) {
    JsonDocument response;

    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "status";

    const DeviceSettings &device = settings.getDevice();
    JsonObject device_obj = response["device"].to<JsonObject>();
    device_obj["name"] = device.name;
    device_obj["number"] = device.number;
    const char* type_str = "unknown";
    if (device.type == 1) {
        type_str = "start";
    } else if (device.type == 2) {
        type_str = "finish";
    }
    device_obj["type"] = type_str;

    JsonObject firmware_obj = response["firmware"].to<JsonObject>();
    firmware_obj["version"] = VERSION;
    firmware_obj["build_date"] = FIRMWARE_BUILD_DATE;

    JsonObject system_obj = response["system"].to<JsonObject>();
    system_obj["uptime_s"] = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
    system_obj["free_heap_bytes"] = esp_get_free_heap_size();
    const char* reset_reason_str = "unknown";
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            reset_reason_str = "power_on";
            break;
        case ESP_RST_SW:
            reset_reason_str = "software";
            break;
        case ESP_RST_PANIC:
            reset_reason_str = "panic";
            break;
        case ESP_RST_INT_WDT:
            reset_reason_str = "int_wdt";
            break;
        case ESP_RST_TASK_WDT:
            reset_reason_str = "task_wdt";
            break;
        case ESP_RST_WDT:
            reset_reason_str = "wdt";
            break;
        case ESP_RST_DEEPSLEEP:
            reset_reason_str = "deep_sleep";
            break;
        case ESP_RST_BROWNOUT:
            reset_reason_str = "brownout";
            break;
        case ESP_RST_SDIO:
            reset_reason_str = "sdio";
            break;
        case ESP_RST_UNKNOWN:
        default:
            reset_reason_str = "unknown";
            break;
    }
    system_obj["reset_reason"] = reset_reason_str;

    JsonObject wifi_obj = response["wifi"].to<JsonObject>();
    WiFiState wifi_state = wifiManager.getState();
    const char* wifi_state_str = "off";
    switch (wifi_state) {
        case WiFiState::CONNECTING:
        case WiFiState::RECONNECTING:
            wifi_state_str = "connecting";
            break;
        case WiFiState::CONNECTED:
            wifi_state_str = "connected";
            break;
        case WiFiState::DISCONNECTED:
        case WiFiState::ERROR:
            wifi_state_str = "error";
            break;
        case WiFiState::UNINITIALIZED:
        case WiFiState::OFF:
        default:
            wifi_state_str = "off";
            break;
    }
    wifi_obj["state"] = wifi_state_str;
    wifi_obj["rssi"] = wifiManager.getRSSI();
    String ip = wifiManager.getIP();
    if (ip.length() > 0) {
        wifi_obj["ip"] = ip;
    }
    String ssid = wifiManager.getSSID();
    if (ssid.length() > 0) {
        wifi_obj["ssid"] = ssid;
    }

    JsonObject ble_obj = response["ble"].to<JsonObject>();
    BLEState ble_state = ble.getState();
    const char* ble_state_str = "off";
    switch (ble_state) {
        case BLEState::ADVERTISING:
            ble_state_str = "advertising";
            break;
        case BLEState::CONNECTED:
            ble_state_str = "connected";
            break;
        case BLEState::DISCONNECTED:
        default:
            ble_state_str = "off";
            break;
    }
    ble_obj["state"] = ble_state_str;
    ble_obj["clients"] = ble.getClientCount();

    JsonObject rtc_obj = response["rtc"].to<JsonObject>();
    bool rtc_ready = rtc.isReady();
    bool rtc_lost_power = rtc_ready ? rtc.lostPower() : false;
    rtc_obj["ready"] = rtc_ready;
    rtc_obj["lost_power"] = rtc_lost_power;
    rtc_obj["time_valid"] =
        rtc_ready && !rtc_lost_power && rtc.unixTime() >= MIN_VALID_UNIX_SEC;
    if (rtc_ready) {
        rtc_obj["temperature_c"] = rtc.getTemperature();
    }
    int64_t rtc_sync_us = rtc.lastSyncUs();
    if (rtc_sync_us > 0) {
        int64_t now_us = esp_timer_get_time();
        int64_t age_ms = (now_us - rtc_sync_us) / 1000;
        if (age_ms < 0) {
            age_ms = 0;
        }
        rtc_obj["last_sync_ms"] = age_ms;
    } else {
        rtc_obj["last_sync_ms"] = 0;
    }

    JsonObject gps_obj = response["gps"].to<JsonObject>();
    GPSState gps_state = gps.getState();
    const char* gps_state_str = "off";
    switch (gps_state) {
        case GPSState::SEARCHING:
            gps_state_str = "searching";
            break;
        case GPSState::ACTIVE:
            gps_state_str = "active";
            break;
        case GPSState::OFF:
        default:
            gps_state_str = "off";
            break;
    }
    gps_obj["state"] = gps_state_str;
    gps_obj["enabled"] = settings.getGps().enabled;
    const bool gps_fix = gps.nmea().isValid() && gps.nmeaFresh();
    const int gps_satellites_used = gps_fix ? gps.nmea().getNumSatellites() : -1;
    GPSSatelliteInfo gps_satellites_view[GPS::MAX_SATELLITES];
    const uint8_t gps_satellites_view_count =
        gps.satellites(gps_satellites_view, GPS::MAX_SATELLITES, true);
    gps_obj["fix"] = gps_fix;
    gps_obj["satellites_used"] = gps_satellites_used;
    gps_obj["satellites_view"] = gps_satellites_view_count;
    gps_obj["pps_signal"] = pps_is_locked();
    int64_t now_us = esp_timer_get_time();
    int64_t gps_fix_us = gps.lastFixUs();
    if (gps_fix_us > 0) {
        int64_t age_ms = (now_us - gps_fix_us) / 1000;
        if (age_ms < 0) {
            age_ms = 0;
        }
        gps_obj["fix_age_ms"] = age_ms;
    }
    int64_t nmea_us = 0;
    if (gps.lastSentenceUs(nmea_us)) {
        int64_t age_ms = (now_us - nmea_us) / 1000;
        if (age_ms < 0) {
            age_ms = 0;
        }
        gps_obj["nmea_age_ms"] = age_ms;
    }
    int64_t gsv_us = 0;
    if (gps.lastGsvUs(gsv_us)) {
        int64_t age_ms = (now_us - gsv_us) / 1000;
        if (age_ms < 0) {
            age_ms = 0;
        }
        gps_obj["gsv_age_ms"] = age_ms;
    }
    int64_t utc_us = 0;
    if (gps.lastUtcUpdateUs(utc_us)) {
        int64_t age_ms = (now_us - utc_us) / 1000;
        if (age_ms < 0) {
            age_ms = 0;
        }
        gps_obj["utc_age_ms"] = age_ms;
    }
    int64_t pps_us = 0;
    uint32_t pps_count = 0;
    if (pps_get_raw(pps_us, pps_count)) {
        int64_t age_ms = (now_us - pps_us) / 1000;
        if (age_ms < 0) {
            age_ms = 0;
        }
        gps_obj["pps_age_ms"] = age_ms;
    }

    JsonObject sync_obj = response["sync"].to<JsonObject>();
    TimeSyncStatus sync_status = time_sync_status();
    TimeSyncState sync_state = time_sync_state();
    const char* sync_state_str = "nosync";
    switch (sync_state) {
        case TimeSyncState::GPS_OK:
            sync_state_str = "gps_ok";
            break;
        case TimeSyncState::GPS_DEGRADED:
            sync_state_str = "gps_degraded";
            break;
        case TimeSyncState::RTC_OK:
            sync_state_str = "rtc_ok";
            break;
        case TimeSyncState::RTC_DEGRADED:
            sync_state_str = "rtc_degraded";
            break;
        case TimeSyncState::NONE:
        default:
            sync_state_str = "nosync";
            break;
    }
    sync_obj["state"] = sync_state_str;
    int64_t accuracy_us = time_sync_estimate_accuracy_us();
    if (accuracy_us >= 0) {
        sync_obj["accuracy_us"] = accuracy_us;
    }
    const char* sync_source_str = "none";
    if (sync_status.source == TimeSource::GPS_PPS) {
        sync_source_str = "gps";
    } else if (sync_status.source == TimeSource::RTC) {
        sync_source_str = "rtc";
    }
    sync_obj["source"] = sync_source_str;
    if (sync_status.last_sync_us > 0) {
        int64_t now_us = esp_timer_get_time();
        int64_t age_ms = (now_us - sync_status.last_sync_us) / 1000;
        if (age_ms < 0) {
            age_ms = 0;
        }
        sync_obj["last_ms"] = age_ms;
    } else {
        sync_obj["last_ms"] = 0;
    }

    JsonObject touch_obj = response["touch"].to<JsonObject>();
    const TouchSettings &touch_settings = settings.getTouch();
    touch_obj["ready"] = touch.isReady();
    touch_obj["enabled"] = touch_settings.enabled;
    touch_obj["calibrated"] =
        touch_settings.calibration.valid && touch.isCalibrated();

    JsonObject storage_obj = response["storage"].to<JsonObject>();
    size_t used_bytes = 0;
    size_t total_bytes = 0;
    bool storage_ok = settings.getStorageStats(used_bytes, total_bytes);
    storage_obj["ok"] = storage_ok;
    if (storage_ok && total_bytes > 0) {
        storage_obj["used_pct"] =
            static_cast<int>((used_bytes * 100U) / total_bytes);
    }

    if (ina226.isReady() && ina226.hasValidSample()) {
        const float battery_voltage = ina226.getBusVoltage();
        if (std::isfinite(battery_voltage)) {
            JsonObject power_obj = response["power"].to<JsonObject>();
            const float rounded_voltage =
                std::roundf(battery_voltage * 100.0f) / 100.0f;
            power_obj["battery_voltage"] = rounded_voltage;
        }
    }

    response["status"] = "ok";

    sendResponse(response, output, true);

    ESP_LOGI(TAG, "Status command processed");
}

void cmdLoadConfig(JsonDocument& request, Stream& output) {
    JsonDocument response;

    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "load_config";
    response["status"] = "ok";

    JsonDocument settings_doc = settings.toJson();
    response["data"] = settings_doc.as<JsonVariantConst>();

    sendResponse(response, output, true);

    ESP_LOGI(TAG, "Load_config command processed");
}

void cmdSaveConfig(JsonDocument& request, Stream& output) {
    if (request["data"].isNull()) {
        sendError("save_config", 102, "Missing 'data' field", request["id"], output);
        ESP_LOGW(TAG, "Save_config failed: missing data");
        return;
    }

    if (!request["data"].is<JsonObject>()) {
        sendError("save_config", 103, "Invalid 'data' field type", request["id"], output);
        ESP_LOGW(TAG, "Save_config failed: invalid data type");
        return;
    }

    Settings prev_settings = settings.getAll();

    JsonDocument data_doc;
    data_doc.set(request["data"]);

    if (!settings.fromJson(data_doc)) {
        sendError("save_config", 103, "Invalid config values", request["id"], output);
        ESP_LOGW(TAG, "Save_config failed: validation error");
        return;
    }

    int saved_keys = settings.save();
    if (saved_keys < 0) {
        sendError("save_config", 202, "Failed to save settings", request["id"], output);
        ESP_LOGW(TAG, "Save_config failed: storage error");
        return;
    }

    Settings new_settings = settings.getAll();
    touch.applySettings(new_settings.touch);
    bool reboot_needed =
        prev_settings.device.name != new_settings.device.name ||
        prev_settings.device.number != new_settings.device.number ||
        prev_settings.device.type != new_settings.device.type ||
        prev_settings.wifi.active != new_settings.wifi.active ||
        prev_settings.wifi.ssid != new_settings.wifi.ssid ||
        prev_settings.wifi.passwd != new_settings.wifi.passwd ||
        prev_settings.gps.enabled != new_settings.gps.enabled;

    JsonDocument response;

    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "save_config";
    response["saved_keys"] = saved_keys;
    response["reboot_needed"] = reboot_needed;
    response["status"] = "ok";

    size_t used_bytes = 0;
    size_t total_bytes = 0;
    if (settings.getStorageStats(used_bytes, total_bytes) && total_bytes > 0) {
        response["storage_usage_percent"] =
            static_cast<int>((used_bytes * 100U) / total_bytes);
    }

    sendResponse(response, output, true);

    ESP_LOGI(TAG, "Save_config command processed: saved_keys=%d", saved_keys);
}

void cmdSyncNtp(JsonDocument& request, Stream& output) {
    if (!wifiManager.isConnected()) {
        sendError("sync_ntp", 204, "WiFi not connected", request["id"], output);
        ESP_LOGW(TAG, "Sync_ntp failed: WiFi not connected");
        return;
    }

    if (!rtc.isReady()) {
        sendError("sync_ntp", 201, "RTC not initialized", request["id"], output);
        ESP_LOGW(TAG, "Sync_ntp failed: RTC not initialized");
        return;
    }

    uint32_t duration_ms = 0;
    const SyncSettings &sync = settings.getSync();
    const char *ntp1 = (sync.ntp1.length() > 0) ? sync.ntp1.c_str() : DEFAULT_SYNC_NTP1;
    const char *ntp2 = (sync.ntp2.length() > 0) ? sync.ntp2.c_str() : DEFAULT_SYNC_NTP2;
    const char *ntp3 = (sync.ntp3.length() > 0) ? sync.ntp3.c_str() : DEFAULT_SYNC_NTP3;
    bool ok = syncRtcUtcFromNtpPrecise(
        rtc,
        wifiManager,
        ntp1,
        ntp2,
        ntp3,
        SNTP_SYNC_TIMEOUT_MS,
        SNTP_EDGE_TIMEOUT_MS,
        SNTP_EDGE_WINDOW_US,
        &duration_ms
    );

    if (!ok) {
        sendError("sync_ntp", 203, "NTP sync failed", request["id"], output);
        ESP_LOGW(TAG, "Sync_ntp failed: unknown error during sync");
        return;
    }

    JsonDocument response;

    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "sync_ntp";
    response["status"] = "ok";
    response["rtc_time"] = rtc.unixTime();
    JsonArray ntp_servers = response["ntp_servers"].to<JsonArray>();
    ntp_servers.add(ntp1);
    ntp_servers.add(ntp2);
    ntp_servers.add(ntp3);
    response["sync_duration_ms"] = duration_ms;

    sendResponse(response, output, true);

    ESP_LOGI(TAG, "Sync_ntp command processed: duration=%lu ms",
             (unsigned long)duration_ms);
}

void cmdWifi(JsonDocument& request, Stream& output) {
    if (request["enable"].isNull()) {
        sendError("wifi", 102, "Missing 'enable' field", request["id"], output);
        ESP_LOGW(TAG, "WiFi command failed: missing enable");
        return;
    }

    if (!request["enable"].is<bool>()) {
        sendError("wifi", 103, "Invalid 'enable' field type", request["id"], output);
        ESP_LOGW(TAG, "WiFi command failed: invalid enable type");
        return;
    }

    bool enable = request["enable"].as<bool>();
    if (!enable) {
        bool stopped = wifiManager.end();
        if (!stopped) {
            sendError("wifi", 206, "WiFi stop timeout", request["id"], output);
            ESP_LOGW(TAG, "WiFi stop timeout");
            return;
        }

        JsonDocument response;
        if (!request["id"].isNull()) {
            response["id"] = request["id"];
        }

        response["cmd"] = "wifi";
        response["state"] = "disabled";
        response["status"] = "ok";

        sendResponse(response, output, true);
        ESP_LOGI(TAG, "WiFi disabled via command");
        return;
    }

    String ssid;
    String passwd;
    bool has_ssid = false;
    if (!request["ssid"].isNull()) {
        ssid = request["ssid"].as<String>();
        has_ssid = ssid.length() > 0;
        if (!request["passwd"].isNull()) {
            passwd = request["passwd"].as<String>();
        }
    }

    if (!has_ssid) {
        const WifiSettings &wifi = settings.getWifi();
        ssid = wifi.ssid;
        passwd = wifi.passwd;
        has_ssid = ssid.length() > 0;
    }

    if (!has_ssid) {
        sendError("wifi", 102, "Missing 'ssid' field", request["id"], output);
        ESP_LOGW(TAG, "WiFi command failed: missing ssid");
        return;
    }

    wifiManager.begin();
    if (!wifiManager.connect(ssid, passwd)) {
        sendError("wifi", 205, "WiFi start failed", request["id"], output);
        ESP_LOGW(TAG, "WiFi command failed: connect start failed");
        return;
    }

    JsonDocument response;
    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "wifi";
    response["state"] = "enabled";
    response["status"] = "ok";

    sendResponse(response, output, true);
    ESP_LOGI(TAG, "WiFi enabled via command: ssid=%s", ssid.c_str());
}

void cmdSyncSource(JsonDocument& request, Stream& output) {
    if (request["source"].isNull()) {
        sendError("sync_source", 102, "Missing 'source' field", request["id"], output);
        ESP_LOGW(TAG, "Sync_source failed: missing source");
        return;
    }

    if (!request["source"].is<const char*>()) {
        sendError("sync_source", 103, "Invalid 'source' field type", request["id"], output);
        ESP_LOGW(TAG, "Sync_source failed: invalid source type");
        return;
    }

    const char* source_str = request["source"].as<const char*>();
    uint8_t source_value = 0;
    if (strcmp(source_str, "auto") == 0) {
        source_value = 0;
    } else if (strcmp(source_str, "gps") == 0) {
        source_value = 1;
    } else if (strcmp(source_str, "rtc") == 0) {
        source_value = 2;
    } else {
        sendError("sync_source", 103, "Invalid 'source' value", request["id"], output);
        ESP_LOGW(TAG, "Sync_source failed: invalid source value '%s'", source_str);
        return;
    }

    SyncSettings sync = settings.getSync();
    sync.source = source_value;
    if (!settings.setSync(sync)) {
        sendError("sync_source", 103, "Invalid sync settings", request["id"], output);
        ESP_LOGW(TAG, "Sync_source failed: settings validation error");
        return;
    }

    if (settings.save() < 0) {
        sendError("sync_source", 202, "Failed to save settings", request["id"], output);
        ESP_LOGW(TAG, "Sync_source failed: storage error");
        return;
    }

    const TimeSyncStatus sync_status = time_sync_status();
    const char* active_source = "none";
    if (sync_status.source == TimeSource::GPS_PPS) {
        active_source = "gps";
    } else if (sync_status.source == TimeSource::RTC) {
        active_source = "rtc";
    }

    const char* reason = "ok";
    if (source_value == 0) {
        reason = "auto";
    } else if (source_value == 1 && strcmp(active_source, "gps") != 0) {
        reason = "no_gps_signal";
    }

    int64_t now_us = esp_timer_get_time();
    int64_t utc_us = 0;
    if (!time_sync_esp_to_utc_us(now_us, utc_us)) {
        utc_us = 0;
    }

    JsonDocument response;
    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "sync_source";
    response["active_source"] = active_source;
    response["reason"] = reason;
    response["timestamp"] = utc_us;
    response["status"] = "ok";

    sendResponse(response, output, true);

    ESP_LOGI(TAG, "Sync_source command processed: requested=%s, active=%s, reason=%s",
             source_str, active_source, reason);
}

void cmdCalibrate(JsonDocument& request, Stream& output) {
    if (request["offset"].isNull()) {
        sendError("calibrate", 102, "Missing 'offset' field", request["id"], output);
        ESP_LOGW(TAG, "Calibrate failed: missing offset");
        return;
    }

    if (!request["offset"].is<float>() && !request["offset"].is<double>() && !request["offset"].is<int>()) {
        sendError("calibrate", 103, "Invalid 'offset' field type", request["id"], output);
        ESP_LOGW(TAG, "Calibrate failed: invalid offset type");
        return;
    }

    if (!rtc.isReady()) {
        sendError("calibrate", 201, "RTC not initialized", request["id"], output);
        ESP_LOGW(TAG, "Calibrate failed: RTC not initialized");
        return;
    }

    float offset = request["offset"].as<float>();
    float previous_offset = 0.0f;
    if (!rtc.getAgingOffsetPpm(previous_offset)) {
        sendError("calibrate", 202, "Failed to read RTC calibration", request["id"], output);
        ESP_LOGW(TAG, "Calibrate failed: RTC read error");
        return;
    }

    float applied_offset = 0.0f;
    if (!rtc.setAgingOffsetPpm(offset, applied_offset)) {
        sendError("calibrate", 202, "Failed to write RTC calibration", request["id"], output);
        ESP_LOGW(TAG, "Calibrate failed: RTC write error");
        return;
    }

    JsonDocument response;
    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "calibrate";
    response["previous_offset"] = previous_offset;
    response["new_offset"] = applied_offset;

    TimeSyncStatus sync_status = time_sync_status();
    response["estimated_error_us"] = llabs(sync_status.last_offset_us);
    response["status"] = "ok";

    sendResponse(response, output, true);

    ESP_LOGI(TAG, "Calibrate command processed: previous=%.3f, new=%.3f",
             previous_offset, offset);
}

void cmdTouchCalibrate(JsonDocument& request, Stream& output) {
    TouchCalibrationFlowResult result = runTouchCalibrationFlow(true);
    if (result.status != TouchCalibrationFlowStatus::Ok) {
        switch (result.status) {
            case TouchCalibrationFlowStatus::TouchNotReady:
                sendError("touch_calibrate", 201, "Touch is not initialized", request["id"], output);
                ESP_LOGW(TAG, "Touch_calibrate failed: touch is not initialized");
                return;
            case TouchCalibrationFlowStatus::WizardFailed:
                sendError("touch_calibrate", 202, "Touch calibration failed", request["id"], output);
                ESP_LOGW(TAG, "Touch_calibrate failed: wizard returned error");
                return;
            case TouchCalibrationFlowStatus::InvalidSettings:
                sendError("touch_calibrate", 103, "Invalid touch settings", request["id"], output);
                ESP_LOGW(TAG, "Touch_calibrate failed: settings validation error");
                return;
            case TouchCalibrationFlowStatus::SaveFailed:
                sendError("touch_calibrate", 202, "Failed to save touch calibration", request["id"], output);
                ESP_LOGW(TAG, "Touch_calibrate failed: settings save error");
                return;
            case TouchCalibrationFlowStatus::ApplyFailed:
                sendError("touch_calibrate", 202, "Failed to apply touch calibration", request["id"], output);
                ESP_LOGW(TAG, "Touch_calibrate failed: apply calibration error");
                return;
            case TouchCalibrationFlowStatus::Ok:
            default:
                break;
        }
    }

    JsonDocument response;
    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "touch_calibrate";
    response["status"] = "ok";
    response["saved_keys"] = result.savedKeys;
    response["cal_valid"] = result.calibration.valid;
    JsonArray cal = response["calibration"].to<JsonArray>();
    for (uint8_t i = 0; i < 5; i++) {
        cal.add(result.calibration.data[i]);
    }

    sendResponse(response, output, true);
    ESP_LOGI(TAG, "Touch_calibrate command processed: saved_keys=%d",
             result.savedKeys);
}

void cmdFactoryReset(JsonDocument& request, Stream& output) {
    if (!settings.factoryReset()) {
        sendError("factory_reset", 202, "Failed to reset settings", request["id"], output);
        ESP_LOGW(TAG, "Factory_reset failed: storage error");
        return;
    }

    JsonDocument response;
    if (!request["id"].isNull()) {
        response["id"] = request["id"];
    }

    response["cmd"] = "factory_reset";
    response["message"] = "Device will reset in 2 seconds";
    response["status"] = "ok";

    sendResponse(response, output, true);

    static esp_timer_handle_t reset_timer = nullptr;
    if (reset_timer == nullptr) {
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = [](void*) { esp_restart(); };
        timer_args.name = "factory_reset";
        if (esp_timer_create(&timer_args, &reset_timer) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create reset timer");
            return;
        }
    } else if (esp_timer_is_active(reset_timer)) {
        esp_timer_stop(reset_timer);
    }

    if (esp_timer_start_once(reset_timer, 2000000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start reset timer");
        return;
    }

    ESP_LOGI(TAG, "Factory_reset completed, scheduled reboot in 2 seconds");
}

void sendResponse(JsonDocument& response, Stream& output, bool addNewline) {
    String jsonStr;
    serializeJson(response, jsonStr);
    if (addNewline) {
        jsonStr += '\n';
    }
    output.write((const uint8_t*)jsonStr.c_str(), jsonStr.length());
}

void sendError(const char* cmd, int errorCode, const char* errorMessage, 
               const JsonVariant& requestId, Stream& output) {
    JsonDocument response;
    
    response["cmd"] = cmd;
    response["status"] = "error";
    response["error_code"] = errorCode;
    response["error_message"] = errorMessage;
    
    // Копируем id из запроса, если оно есть
    if (!requestId.isNull()) {
        response["id"] = requestId;
    }
    
    sendResponse(response, output, true);
}
