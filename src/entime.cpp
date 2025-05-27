#include <Arduino.h>
#include <GyverNTP.h>
#include <Streaming.h>
#include "tft_lcd.h"
#include "icons.h"

// TFT_LCD tft = TFT_LCD();

// События WiFi
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

void ntpError();

// Вызов каждую секунду
void onSecond();

void setup()
{
    Serial.begin(57600);

    // обработчик событий от WiFi
    WiFi.onEvent(WiFiEvent);
    // Set WiFi to station mode and disconnect from an AP if it was previously connected.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    //  WiFi.setScanActiveMinTime(min_time);
    // сканируем сети WiFi
    WiFi.scanNetworks();

    // обработчик ошибок NTP
    NTP.onError(ntpError);

    // обработчик секунды (вызывается из тикера)
    NTP.onSecond(onSecond);

    // обработчик синхронизации (вызывается из sync)
    // NTP.onSync([](uint32_t unix) {
    //     Serial.println("sync: ");
    //     Serial.print(unix);
    // });

    NTP.begin(3); // запустить и указать часовой пояс
    // NTP.setPeriod(30);                   // период синхронизации в секундах
    // NTP.setHost("ntp1.stratum2.ru");     // установить другой хост
    // NTP.setHost(IPAddress(1, 2, 3, 4));  // установить другой хост
    // NTP.asyncMode(false);                // выключить асинхронный режим
    // NTP.ignorePing(true);                // не учитывать пинг до сервера
    // NTP.updateNow();                     // обновить прямо сейчас

    TFT.drawBattery(100);
    TFT.drawBluetooth();
    // tft.drawSignal();
    // tft.drawClock("fgh");
}

void loop()
{
    // Serial << NTP.toString(); // NTP.timeToString(), NTP.dateToString()
    // Serial << ':';
    // Serial << NTP.ms() << endl; // + миллисекунды текущей секунды. Внутри tick всегда равно 0

    NTP.tick();

    if (NTP.statusChanged())
    {
        Serial << "STATUS: ";
        Serial << NTP.online() << endl;
        // TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());
    }
}

void onSecond()
{
    Datime dt = NTP;
    TFT.drawClock(dt);
    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());
}

void ntpError()
{
    Serial << "NTP Error: " << NTP.readError() << endl;
    String online = NTP.online() ? "true" : "false";
    Serial << "online: ";
    Serial << online << endl;
    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());
}

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.printf("[WiFi-event] event: %d\n", event);

    switch (event)
    {
    case ARDUINO_EVENT_WIFI_READY:
        Serial.println("WiFi interface ready");
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
        Serial.println("Completed scan for access points");
        Serial << "Found " << info.wifi_scan_done.number << " nets" << endl;
        for (int i = 0; i < info.wifi_scan_done.number; ++i)
        {
            Serial << i + 1 << ":" << endl;
            Serial << "SSID: " << WiFi.SSID(i) << " RSSI: " << WiFi.RSSI(i) << endl;
            if (WiFi.SSID(i) = "Pipes")
            {
                WiFi.begin(WiFi.SSID(i), "korol123");
                break;
            }
        }
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        Serial.println("WiFi client started");
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        Serial.println("WiFi clients stopped");
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.println("Connected to access point");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("Disconnected from WiFi access point");
        Serial.print("WiFi lost connection. Reason: ");
        Serial << WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)) << endl;

        switch (info.wifi_sta_disconnected.reason)
        {
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            WiFi.disconnect();
            break;
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            WiFi.disconnect();
            break;
        default:
            break;
        }
        break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        Serial.println("Authentication mode of access point has changed");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.print("Obtained IP address: ");
        Serial.println(WiFi.localIP());
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        Serial.println("Lost IP address and IP address is reset to 0");
        break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
        Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_PIN:
        Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode");
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        Serial.println("WiFi access point started");
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        Serial.println("WiFi access point  stopped");
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        Serial.println("Client connected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.println("Client disconnected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        Serial.println("Assigned IP address to client");
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        Serial.println("Received probe request");
        break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
        Serial.println("AP IPv6 is preferred");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
        Serial.println("STA IPv6 is preferred");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
        Serial.println("Ethernet IPv6 is preferred");
        break;
    case ARDUINO_EVENT_ETH_START:
        Serial.println("Ethernet started");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("Ethernet stopped");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("Ethernet connected");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("Ethernet disconnected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.println("Obtained IP address");
        break;
    default:
        break;
    }
}