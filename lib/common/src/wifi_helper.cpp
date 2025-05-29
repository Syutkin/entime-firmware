#include "wifi_helper.h"

void setupWiFi()
{
    // обработчик событий от WiFi
    WiFi.onEvent(WiFiEvent);
    // Set WiFi to station mode and disconnect from an AP if it was previously connected.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    //  WiFi.setScanActiveMinTime(min_time);
    // сканируем сети WiFi
    WiFi.scanNetworks();
}

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    bool noNetwork = true;
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_READY:
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
         break;
    case ARDUINO_EVENT_WIFI_STA_START:
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        switch (info.wifi_sta_disconnected.reason)
        {
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            WiFi.disconnect(true, false);
            BT.begin();
            break;
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            WiFi.disconnect(true, false);
            BT.begin();
            break;
        default:
            break;
        }
        break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.print("WiFi: Obtained IP address: ");
        Serial.println(WiFi.localIP());
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        Serial.println("WiFi: Lost IP address and IP address is reset to 0");
        break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
        break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        break;
    case ARDUINO_EVENT_WPS_ER_PIN:
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
        break;
    case ARDUINO_EVENT_ETH_START:
        break;
    case ARDUINO_EVENT_ETH_STOP:
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        break;
    default:
        break;
    }
}
