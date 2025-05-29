#include <Arduino.h>
#include <Streaming.h>
#include <GyverNTP.h>
#include <GyverDS3231Min.h>

#include "tft_lcd.h"
#include "wifi_helper.h"
#include "bluetooth_helper.h"
#include "settings.h"

// Check if Bluetooth is available
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Check Serial Port Profile
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip.
#endif

GyverDS3231Min rtc;

uint8_t secondsToSyncNTP = 5;

// обработчик событий WiFi
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

// обработчик ошибок NTP
void ntpError();

// Вызов каждую секунду
void onSecond();

void onSync(uint32_t ut);

void setup()
{
    Serial.begin(57600);

    loadSettings("name");

    TFT.begin();

    TFT.drawBattery(0);
    TFT.drawBluetooth(BT.serial.isReady(), BT.serial.hasClient());
    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());

    // обработчик событий от WiFi
    WiFi.onEvent(WiFiEvent);
    // Set WiFi to station mode and disconnect from an AP if it was previously connected.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Если есть сохранённая сеть WiFi, то ищем её и пытаемся подключится
    // При удачном подключении в NTP.onSecond ждём [secondsToSyncNTP] (5) секунд синхронизации NTP
    // Затем выключаем WiFi и включаем BluetoothSerial (они работают на одной антенне)
    // При неудачном подключении WiFi сразу включаем BluetoothSerial
    bool connect = false;
    bool connected = false;
    if (!settings.WiFi.ssid.isEmpty())
    {
        Serial << "Scaning WiFi networks..." << endl;
        int16_t nets = WiFi.scanNetworks();
        Serial.println("WiFi: Completed scan for access points");
        Serial << "Found " << nets << " nets" << endl;
        if (nets > 0)
        {
            for (int i = 0; i < nets; ++i)
            {
                Serial << i + 1 << ": " << "SSID: " << WiFi.SSID(i) << " RSSI: " << WiFi.RSSI(i) << endl;
                if (WiFi.SSID(i) == settings.WiFi.ssid)
                {
                    connect = true;
                }
            }
            if (connect)
            {
                // Will try for about 5 seconds (10x 500ms)
                int tryDelay = 500;
                int numberOfTries = 10;

                Serial << "Connecting to " << settings.WiFi.ssid << "..." << endl;
                // WiFi.setAutoReconnect(false);
                WiFi.begin(settings.WiFi.ssid, settings.WiFi.passwd);

                // Wait for the WiFi event
                while (true)
                {
                    switch (WiFi.status())
                    {
                    case WL_CONNECT_FAILED:
                        Serial.print("[WiFi] Failed - WiFi not connected! Reason: ");
                        connect = false;
                        break;
                    case WL_CONNECTION_LOST:
                        Serial.println("[WiFi] Connection was lost");
                        break;
                    case WL_SCAN_COMPLETED:
                        Serial.println("[WiFi] Scan is completed");
                        break;
                    case WL_DISCONNECTED:
                        Serial.println("[WiFi] WiFi is disconnected");
                        break;
                    case WL_CONNECTED:
                        Serial.println("[WiFi] WiFi is connected!");
                        Serial.print("[WiFi] IP address: ");
                        Serial.println(WiFi.localIP());
                        connected = true;
                        break;
                    default:
                        Serial.print("[WiFi] WiFi Status: ");
                        Serial.println(WiFi.status());
                        break;
                    }
                    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());

                    if (connected | !connect)
                        break;

                    delay(tryDelay);
                    if (numberOfTries <= 0)
                    {
                        Serial.print("[WiFi] Failed to connect to WiFi!");
                        // Use disconnect function to force stop trying to connect
                        WiFi.disconnect();
                        connect = false;
                        break;
                    }
                    else
                    {
                        numberOfTries--;
                    }
                }
            }
        }
    }

    if (!connect)
    {
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        BT.begin();
        TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());
    }

    // обработчик ошибок NTP
    // NTP.onError(ntpError);

    // обработчик секунды (вызывается из тикера)
    NTP.onSecond(onSecond);

    // обработчик синхронизации (вызывается из sync)
    NTP.onSync(onSync);

    // GyverDS3231
    Wire.begin();
    if (rtc.begin())
    {
        Serial << "RTC started successfully" << endl;
    };

    setStampZone(settings.Time.timezone);

    NTP.begin();
    // NTP.begin(3);                        // запустить и указать часовой пояс
    NTP.setPeriod(120);             // период синхронизации в секундах
    NTP.setHost("ru.pool.ntp.org"); // установить другой хост
    // NTP.setHost(IPAddress(1, 2, 3, 4));  // установить другой хост
    // NTP.asyncMode(false);                // выключить асинхронный режим
    // NTP.ignorePing(true);                // не учитывать пинг до сервера
    // NTP.updateNow();                     // обновить прямо сейчас

    // подключить RTC
    NTP.attachRTC(rtc);
}

void loop()
{
    // Serial << NTP.toString(); // NTP.timeToString(), NTP.dateToString()
    // Serial << ':';
    // Serial << NTP.ms() << endl; // + миллисекунды текущей секунды. Внутри tick всегда равно 0

    // счётчик каждую секунду
    NTP.tick();

    // if (NTP.statusChanged())
    // {
    //     String online = NTP.online() ? "online" : "offline";
    //     Serial << "NTP status changed: ";
    //     Serial << NTP.online() << endl;
    // }

    // reading from bluetooth serial;
    BT.read();
}

void onSecond()
{
    Datime dt = NTP;
    TFT.drawClock(dt);
    TFT.drawBattery(0);
    TFT.drawBluetooth(BT.serial.isReady(), BT.serial.hasClient());
    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());

    if (NTP.online() & WiFi.isConnected())
    {
        // 5 seconds to disconnect from WiFi and power up bluetooth serial
        if (secondsToSyncNTP > 0)
        {
            secondsToSyncNTP--;
        }
        else
        {
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_OFF);
            BT.begin();
        }
    }
}

void ntpError()
{
    Serial << "NTP Error: " << NTP.readError() << endl;
    String online = NTP.online() ? "true" : "false";
    Serial << "NTP online: ";
    Serial << online << endl;
    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());
}

void onSync(uint32_t ut)
{
    Serial << "sync: " << endl;
    Serial << "RTC time: " << rtc.getTime().toString() << endl;
    Serial << "NTP time: " << NTP.toString() << endl;
}
