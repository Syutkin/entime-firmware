#include <Arduino.h>
#include <Streaming.h>
#include <GyverNTP.h>
#include <GyverDS3231Min.h>
#include <BluetoothSerial.h>
#include "tft_lcd.h"
#include "wifi_helper.h"

// Check if Bluetooth is available
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Check Serial Port Profile
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip.
#endif

GyverDS3231Min rtc;
BluetoothSerial SerialBT;
String myName = "ESP32-BT-Master";

// обработчик событий WiFi
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

// обработчик ошибок NTP
void ntpError();

// Вызов каждую секунду
void onSecond();

void setup()
{
    Serial.begin(57600);

    TFT.begin();

    if (settings.WiFi.ssid)
    {
        Serial << "Setup WiFi" << endl;
        // настройка wifi
        setupWiFi();
    }
    else
    {
        Serial << "Start Bluetooth" << endl;
        SerialBT.begin(myName);
    }

    // обработчик ошибок NTP
    NTP.onError(ntpError);

    // обработчик секунды (вызывается из тикера)
    NTP.onSecond(onSecond);

    // обработчик синхронизации (вызывается из sync)
    // NTP.onSync([](uint32_t unix) {
    //     Serial.println("sync: ");
    //     Serial.print(unix);
    // });

    // GyverDS3231
    Wire.begin();
    rtc.begin();

    NTP.begin(3); // запустить и указать часовой пояс
    // NTP.setPeriod(30);                   // период синхронизации в секундах
    // NTP.setHost("ntp1.stratum2.ru");     // установить другой хост
    // NTP.setHost(IPAddress(1, 2, 3, 4));  // установить другой хост
    // NTP.asyncMode(false);                // выключить асинхронный режим
    // NTP.ignorePing(true);                // не учитывать пинг до сервера
    // NTP.updateNow();                     // обновить прямо сейчас

    // подключить RTC
    NTP.attachRTC(rtc);

    TFT.drawBattery(0);
    TFT.drawBluetooth();
    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());
    // tft.drawClock("fgh");
}

void loop()
{
    // Serial << NTP.toString(); // NTP.timeToString(), NTP.dateToString()
    // Serial << ':';
    // Serial << NTP.ms() << endl; // + миллисекунды текущей секунды. Внутри tick всегда равно 0

    // счётчик каждую секунду
    NTP.tick();

    if (NTP.statusChanged())
    {
        String online = NTP.online() ? "online" : "offline";
        Serial << "NTP status changed: ";
        Serial << NTP.online() << endl;
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
    Serial << "NTP online: ";
    Serial << online << endl;
    TFT.drawSignal(NTP.online(), WiFi.status(), WiFi.RSSI());
}
