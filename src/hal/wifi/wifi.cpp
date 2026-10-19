#include "wifi.h"
#include "esp_log.h"

static const char* TAG = "WiFi";

// Глобальный экземпляр
WiFiManager wifiManager;

// ============================================================================
// WiFi Event Handler
// ============================================================================

void WiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    wifiManager.onWiFiEvent(event, info);
}

// ============================================================================
// WiFiManager Implementation
// ============================================================================

void WiFiManager::begin() {
    if (_state != WiFiState::UNINITIALIZED) {
        ESP_LOGW(TAG, "WiFi already initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    // Режим STA (клиент)
    WiFi.mode(WIFI_STA);

    // Отключаем autoreconnect Arduino core, чтобы повторами управлял только WiFiManager.
    WiFi.setAutoReconnect(false);
    
    // Регистрируем обработчик событий только один раз за время жизни процесса.
    if (!_eventRegistered) {
        _eventHandlerId = WiFi.onEvent(WiFiEvent);
        _eventRegistered = true;
    }
    
    // Устанавливаем состояние OFF после успешного завершения всех операций инициализации
    setState(WiFiState::OFF);
    
    ESP_LOGI(TAG, "WiFi initialized");
}

bool WiFiManager::end() {
    if (_state == WiFiState::UNINITIALIZED) {
        return true;
    }
    
    ESP_LOGI(TAG, "Disabling WiFi...");
    
    // Отключаем автопереподключение перед отключением
    _autoReconnect = false;
    
    // Отключаемся от сети и выключаем WiFi одним вызовом
    WiFi.disconnect(true, true);

    // Ждем подтверждения выключения WiFi
    const uint32_t t0 = millis();
    while (millis() - t0 < 1000) {
        if (WiFi.getMode() == WIFI_OFF) {
            break;
        }
        delay(10);
    }

    bool stopped = (WiFi.getMode() == WIFI_OFF);
    
    // Очищаем состояние перед установкой UNINITIALIZED
    // Это предотвращает обработку событий после установки UNINITIALIZED
    _ssid = "";
    _password = "";
    _rssi = 0;
    _reconnectAttempts = 0;
    
    // Устанавливаем UNINITIALIZED в самом конце, после всех операций
    // Обработчик событий остается зарегистрированным, но onWiFiEvent() будет игнорировать события
    setState(WiFiState::UNINITIALIZED);
    
    ESP_LOGI(TAG, "WiFi disabled");
    return stopped;
}

bool WiFiManager::connect(const String& ssid, const String& password) {
    if (_state == WiFiState::UNINITIALIZED) {
        ESP_LOGE(TAG, "WiFi not initialized, call begin() first");
        return false;
    }
    
    if (ssid.length() == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return false;
    }
    
    _ssid = ssid;
    _password = password;
    _autoReconnect = true;
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", _ssid.c_str());
    
    setState(WiFiState::CONNECTING);
    _reconnectAttempts = 0;
    
    // Начало подключения
    if (_password.length() > 0) {
        WiFi.begin(_ssid.c_str(), _password.c_str());
    } else {
        WiFi.begin(_ssid.c_str());
    }
    
    return true;
}

void WiFiManager::disconnect() {
    if (_state == WiFiState::UNINITIALIZED) {
        return;
    }
    
    ESP_LOGI(TAG, "Disconnecting from WiFi...");
    
    _autoReconnect = false;
    WiFi.disconnect(true);
    
    _ssid = "";
    _password = "";
    _rssi = 0;
    _reconnectAttempts = 0;
    setState(WiFiState::OFF);
}

bool WiFiManager::isConnected() {
    return _state != WiFiState::UNINITIALIZED && WiFi.status() == WL_CONNECTED && _state == WiFiState::CONNECTED;
}

WiFiState WiFiManager::getState() {
    return _state;
}

void WiFiManager::setStateCallback(WiFiStateCallback callback) {
    _stateCallback = callback;
}

void WiFiManager::update() {
    if (_state == WiFiState::UNINITIALIZED) {
        return;
    }
    
    // Обновление RSSI если подключено
    if (_state == WiFiState::CONNECTED) {
        uint32_t now = millis();
        if (now - _lastRSSIUpdate >= _rssiUpdateInterval) {
            _lastRSSIUpdate = now;
            int8_t newRSSI = WiFi.RSSI();
            if (newRSSI != _rssi) {
                _rssi = newRSSI;
                // Уведомляем об изменении RSSI
                if (_stateCallback) {
                    _stateCallback(_state, _rssi);
                }
            }
        }
    }
    
    // Автоматическое переподключение
    if (_autoReconnect && _state == WiFiState::RECONNECTING && _ssid.length() > 0) {
        attemptReconnect();
    }
}

int8_t WiFiManager::getRSSI() {
    if (_state == WiFiState::CONNECTED) {
        return _rssi;
    }
    return 0;
}

String WiFiManager::getIP() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return String("");
}

String WiFiManager::getSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return String("");
}

void WiFiManager::onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    // Ранний выход если WiFiManager не инициализирован
    // Это защищает от обработки событий после вызова end() или до begin()
    if (_state == WiFiState::UNINITIALIZED) {
        ESP_LOGD(TAG, "Ignoring WiFi event %d: WiFiManager not initialized", event);
        return;
    }
    
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            if (_ssid.length() > 0) {
                setState(WiFiState::CONNECTING);
            }
            break;
            
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected to AP: %s", info.wifi_sta_connected.ssid);
            // Еще не получили IP, состояние остается CONNECTING
            break;
            
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            // Запоздавший IP не должен возобновлять отменённое подключение.
            if (!_autoReconnect || _ssid.length() == 0) {
                break;
            }
            ESP_LOGI(TAG, "WiFi got IP: %s", WiFi.localIP().toString().c_str());
            _rssi = WiFi.RSSI();
            _lastRSSIUpdate = millis();
            _reconnectAttempts = 0;
            _autoReconnect = true;
            setState(WiFiState::CONNECTED);
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected, reason: %d", info.wifi_sta_disconnected.reason);
            _rssi = 0;
            
            // Если мы пытались подключиться
            // Дополнительная проверка UNINITIALIZED здесь не нужна, так как она уже в начале функции
            if (_ssid.length() > 0) {
                if (_autoReconnect) {
                    if (_reconnectAttempts < _maxReconnectAttempts) {
                        setState(WiFiState::RECONNECTING);
                        _lastReconnectAttempt = millis();
                    } else {
                        ESP_LOGE(TAG, "Max reconnect attempts reached, giving up");
                        setState(WiFiState::ERROR);
                        _autoReconnect = false;
                    }
                } else {
                    setState(WiFiState::DISCONNECTED);
                }
            } else {
                setState(WiFiState::OFF);
            }
            break;
            
        case ARDUINO_EVENT_WIFI_STA_STOP:
            ESP_LOGI(TAG, "WiFi STA stopped");
            setState(WiFiState::OFF);
            break;
            
        default:
            break;
    }
}

void WiFiManager::setState(WiFiState newState) {
    if (_state != newState) {
        WiFiState oldState = _state;
        _state = newState;
        
        ESP_LOGI(TAG, "WiFi state changed: %d -> %d (wifi_status=%d)",
                 (int)oldState, (int)newState, WiFi.status());
        
        // Уведомляем callback
        if (_stateCallback) {
            _stateCallback(_state, _rssi);
        }
    }
}

void WiFiManager::attemptReconnect() {
    uint32_t now = millis();
    
    // Проверяем интервал между попытками
    if (now - _lastReconnectAttempt < _reconnectInterval) {
        return;
    }
    
    // Проверяем максимальное количество попыток
    if (_reconnectAttempts >= _maxReconnectAttempts) {
        ESP_LOGE(TAG, "Max reconnect attempts reached, giving up");
        setState(WiFiState::ERROR);
        _autoReconnect = false;
        return;
    }
    
    _reconnectAttempts++;
    _lastReconnectAttempt = now;
    
    ESP_LOGI(TAG, "Attempting to reconnect (%d/%d)...", _reconnectAttempts, _maxReconnectAttempts);
    
    setState(WiFiState::CONNECTING);
    
    if (_password.length() > 0) {
        WiFi.begin(_ssid.c_str(), _password.c_str());
    } else {
        WiFi.begin(_ssid.c_str());
    }
}
