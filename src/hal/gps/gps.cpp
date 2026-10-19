#include "gps.h"
#include "config.h"
#include "esp_log.h"
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "GPS";
static const char* TAG_NMEA = "GPS][NMEA";  // Хак для вывода [GPS][NMEA]

static_assert(GPS_POWER_PIN >= 0, "GPS_POWER_PIN must be configured");

// Глобальный объект GPS
GPS gps;

// GPS Serial (UART2)
static HardwareSerial gpsSerial(2);

// Рабочий буфер полной NMEA строки для дополнительных парсеров и verbose лога.
static char nmeaLineBuffer[128];
static uint8_t nmeaLineIdx = 0;

static bool readNmeaField_(const char*& cursor, char* out, size_t out_len) {
    if (!cursor || *cursor == '\0' || *cursor == '*') {
        return false;
    }

    size_t i = 0;
    while (*cursor != '\0' && *cursor != ',' && *cursor != '*') {
        if (i + 1 < out_len) {
            out[i++] = *cursor;
        }
        ++cursor;
    }
    out[i] = '\0';

    if (*cursor == ',') {
        ++cursor;
    }
    return true;
}

static bool parseIntField_(const char* field, int& value) {
    if (!field || *field == '\0') {
        return false;
    }

    char* end = nullptr;
    long parsed = strtol(field, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }

    value = (int)parsed;
    return true;
}

void GPS::begin(bool enabled) {
    if (_initialized) return;

    pinMode(GPS_POWER_PIN, OUTPUT);
    digitalWrite(GPS_POWER_PIN, enabled ? HIGH : LOW);
    ESP_LOGI(TAG, "Power %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
        ESP_LOGI(TAG, "Disabled by settings");
        return;
    }

    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    ESP_LOGI(TAG, "Initialized (RX:%d, TX:%d, PPS:%d)", GPS_RX_PIN, GPS_TX_PIN, GPS_PPS_PIN);

    _initialized = true;
    updateState_();
    updateSats_();
}

void GPS::update() {
    if (!_initialized) {
        return;
    }

    while (gpsSerial.available()) {
        const int64_t now_us = esp_timer_get_time();
        char c = gpsSerial.read();

        // Мягкий таймстампинг: фиксируем старт предложения по '$'
        if (c == '$') {
            _in_sentence = true;
            _current_sentence_start_us = now_us;
        }

        const int64_t sentence_start_us = _in_sentence ? _current_sentence_start_us : 0;
        uint64_t utc_before = 0;
        const bool had_utc_before = readUtcSignature_(utc_before);
        
        // Собираем полную строку для дополнительных NMEA парсеров и verbose лога.
        if (c == '\n' || c == '\r') {
            if (nmeaLineIdx > 0) {
                nmeaLineBuffer[nmeaLineIdx] = '\0';
                parseGsvSentence_(nmeaLineBuffer, now_us);
                ESP_LOGV(TAG_NMEA, "%s", nmeaLineBuffer);
                nmeaLineIdx = 0;
            }

            if (_in_sentence) {
                _current_sentence_start_us = 0;
                _in_sentence = false;
            }
        } else if (nmeaLineIdx < sizeof(nmeaLineBuffer) - 1) {
            nmeaLineBuffer[nmeaLineIdx++] = c;
        } else {
            // Переполнение — сбрасываем
            nmeaLineIdx = 0;
        }
        
        // Парсинг NMEA
        const bool complete_sentence = _nmea.process(c);
        // process() сообщает о конце строки даже при ошибочном checksum.
        const char* sentence = _nmea.getSentence();
        const bool processed_sentence = complete_sentence && sentence[0] == '$' &&
                                        MicroNMEA::testChecksum(sentence);
        if (processed_sentence) {
            _last_sentence_us = now_us;
            if (_nmea.isValid()) {
                _last_fix_us = now_us;
            }
        }

        uint64_t utc_after = 0;
        if (processed_sentence &&
            sentence_start_us != 0 &&
            readUtcSignature_(utc_after) &&
            (!had_utc_before || utc_after != utc_before) &&
            utc_after != _last_utc_signature) {
            _last_utc_signature = utc_after;
            _last_utc_update_sentence_start_us = sentence_start_us;
            _last_utc_update_us = now_us;
        }
    }

    updateState_();
    updateSats_();
}

bool GPS::lastUtcUpdateSentenceStartUs(int64_t &ts_us) const {
    if (_last_utc_update_sentence_start_us == 0) return false;
    ts_us = _last_utc_update_sentence_start_us;
    return true;
}

bool GPS::lastSentenceUs(int64_t &ts_us) const {
    if (_last_sentence_us == 0) return false;
    ts_us = _last_sentence_us;
    return true;
}

bool GPS::lastUtcUpdateUs(int64_t &ts_us) const {
    if (_last_utc_update_us == 0) return false;
    ts_us = _last_utc_update_us;
    return true;
}

bool GPS::lastGsvUs(int64_t &ts_us) const {
    if (_last_gsv_us == 0) return false;
    ts_us = _last_gsv_us;
    return true;
}

bool GPS::nmeaFresh(int64_t max_age_us) const {
    if (!_initialized || _last_sentence_us == 0) return false;
    int64_t age_us = esp_timer_get_time() - _last_sentence_us;
    return age_us >= 0 && age_us <= max_age_us;
}

bool GPS::utcFresh(int64_t max_age_us) const {
    if (!_initialized || _last_utc_update_us == 0) return false;
    int64_t age_us = esp_timer_get_time() - _last_utc_update_us;
    return age_us >= 0 && age_us <= max_age_us;
}

GPSState GPS::getState() const {
    if (!_initialized) return GPSState::OFF;
    return (_nmea.isValid() && nmeaFresh()) ? GPSState::ACTIVE : GPSState::SEARCHING;
}

uint8_t GPS::satellites(GPSSatelliteInfo* out,
                        uint8_t max_count,
                        bool fresh_only) const {
    if (!out || max_count == 0) {
        return 0;
    }

    const int64_t now_us = esp_timer_get_time();
    uint8_t copied = 0;
    for (uint8_t i = 0; i < _satellite_count && copied < max_count; ++i) {
        const GPSSatelliteInfo& sat = _satellites[i];
        if (sat.prn == 0) {
            continue;
        }
        if (fresh_only) {
            const int64_t age_us = now_us - sat.last_seen_us;
            if (sat.last_seen_us == 0 || age_us < 0 ||
                age_us > SATELLITE_STALE_US) {
                continue;
            }
        }
        out[copied++] = sat;
    }
    return copied;
}

void GPS::setStateCallback(void (*callback)(GPSState state)) {
    _stateCallback = callback;
    _lastState = getState();
    notifyStateChanged_();
}

void GPS::setSatsCallback(void (*callback)(int8_t sats)) {
    _satsCallback = callback;
    _lastSats = currentSats_();
    notifySatsChanged_();
}

void GPS::notifyStateChanged_() {
    if (_stateCallback) {
        _stateCallback(getState());
    }
}

void GPS::updateState_() {
    GPSState state = getState();
    if (state == _lastState) return;
    _lastState = state;
    notifyStateChanged_();
}

void GPS::notifySatsChanged_() {
    if (_satsCallback) {
        _satsCallback(_lastSats);
    }
}

int8_t GPS::currentSats_() const {
    if (!_initialized || !_nmea.isValid() || !nmeaFresh()) {
        return -1;
    }

    int sats = (int)_nmea.getNumSatellites();
    if (sats < 0) {
        return -1;
    }
    if (sats > 99) {
        sats = 99;
    }
    return (int8_t)sats;
}

bool GPS::readUtcSignature_(uint64_t &signature) const {
    if (!_initialized || !_nmea.isValid()) {
        return false;
    }

    const uint16_t year = _nmea.getYear();
    const uint8_t month = _nmea.getMonth();
    const uint8_t day = _nmea.getDay();
    const uint8_t hour = _nmea.getHour();
    const uint8_t minute = _nmea.getMinute();
    const uint8_t second = _nmea.getSecond();

    if (year < 2020 || month == 0 || day == 0 ||
        hour > 23 || minute > 59 || second > 60) {
        return false;
    }

    signature = ((uint64_t)year << 26) |
                ((uint64_t)month << 22) |
                ((uint64_t)day << 17) |
                ((uint64_t)hour << 12) |
                ((uint64_t)minute << 6) |
                (uint64_t)second;
    return true;
}

void GPS::parseGsvSentence_(const char* sentence, int64_t now_us) {
    if (!sentence || sentence[0] != '$' || strlen(sentence) < 7) {
        return;
    }

    if (sentence[3] != 'G' || sentence[4] != 'S' || sentence[5] != 'V' ||
        sentence[6] != ',') {
        return;
    }

    const char* checksum = strchr(sentence, '*');
    if (checksum && !MicroNMEA::testChecksum(sentence)) {
        return;
    }

    char talker[3] = {sentence[1], sentence[2], '\0'};
    const char* cursor = sentence + 7;
    char field[8] = {0};
    int total_messages = 0;
    int message_number = 0;
    int total_satellites = 0;

    if (!readNmeaField_(cursor, field, sizeof(field)) ||
        !parseIntField_(field, total_messages) ||
        !readNmeaField_(cursor, field, sizeof(field)) ||
        !parseIntField_(field, message_number) ||
        !readNmeaField_(cursor, field, sizeof(field)) ||
        !parseIntField_(field, total_satellites)) {
        return;
    }

    if (total_messages <= 0 || message_number <= 0 ||
        message_number > total_messages || total_satellites < 0) {
        return;
    }

    _last_gsv_us = now_us;

    while (*cursor != '\0' && *cursor != '*') {
        int prn = 0;
        int elevation = -1;
        int azimuth = -1;
        int snr = -1;

        if (!readNmeaField_(cursor, field, sizeof(field)) ||
            !parseIntField_(field, prn)) {
            break;
        }
        if (!readNmeaField_(cursor, field, sizeof(field)) ||
            !parseIntField_(field, elevation)) {
            break;
        }
        if (!readNmeaField_(cursor, field, sizeof(field)) ||
            !parseIntField_(field, azimuth)) {
            break;
        }
        // Запятая после азимута обязательна даже при пустом последнем SNR.
        if (cursor[-1] != ',') {
            break;
        }
        if (*cursor == '*' || *cursor == '\0') {
            field[0] = '\0';
        } else if (!readNmeaField_(cursor, field, sizeof(field))) {
            break;
        }
        if (field[0] != '\0') {
            if (!parseIntField_(field, snr)) {
                break;
            }
        }

        if (prn <= 0 || prn > 255 ||
            elevation < 0 || elevation > 90 ||
            azimuth < 0 || azimuth > 359 ||
            snr > 99) {
            continue;
        }

        updateSatellite_(talker,
                         (uint8_t)prn,
                         (int8_t)elevation,
                         (int16_t)azimuth,
                         (int8_t)snr,
                         now_us);
    }
}

void GPS::updateSatellite_(const char* talker,
                           uint8_t prn,
                           int8_t elevation_deg,
                           int16_t azimuth_deg,
                           int8_t snr_db,
                           int64_t now_us) {
    if (!talker || prn == 0) {
        return;
    }

    uint8_t slot = MAX_SATELLITES;
    for (uint8_t i = 0; i < _satellite_count; ++i) {
        if (_satellites[i].prn == prn &&
            _satellites[i].talker[0] == talker[0] &&
            _satellites[i].talker[1] == talker[1]) {
            slot = i;
            break;
        }
    }

    if (slot == MAX_SATELLITES) {
        if (_satellite_count < MAX_SATELLITES) {
            slot = _satellite_count++;
        } else {
            int64_t oldest_us = _satellites[0].last_seen_us;
            slot = 0;
            for (uint8_t i = 1; i < MAX_SATELLITES; ++i) {
                if (_satellites[i].last_seen_us < oldest_us) {
                    oldest_us = _satellites[i].last_seen_us;
                    slot = i;
                }
            }
        }
    }

    GPSSatelliteInfo& sat = _satellites[slot];
    sat.talker[0] = talker[0];
    sat.talker[1] = talker[1];
    sat.talker[2] = '\0';
    sat.prn = prn;
    sat.elevation_deg = elevation_deg;
    sat.azimuth_deg = azimuth_deg;
    sat.snr_db = snr_db;
    sat.last_seen_us = now_us;
}

void GPS::updateSats_() {
    int8_t sats = currentSats_();
    if (sats == _lastSats) return;
    _lastSats = sats;
    notifySatsChanged_();
}
