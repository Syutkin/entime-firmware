/*
Модуль для работы с отсечкой финиша
  Версия 18/03/2021 "BLE эдишн"
  Реализовано:
    Основная борда ESP32 + LoRa
    Модуль RTC (DS3231)
    Дисплей (ST7735)
    Время финиша передаётся по Bluetooth
    Время cтарта передаётся по BLE
    ReadVcc
  В процессе:
    По LoRa принимается время старта
*/

#include <Arduino.h>
#include <entime.h>

// #if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
// #error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
// #endif

/* настройки */
const uint8_t EVENT_DELAY = 20; //количество миллисекунд между двумя отсечками
const char MODULE_NUMBER = '1'; //номер в имени Bluetooth модуля //ToDo: сделать его настраиваемым
const char FINISH_HEADER = 'F'; //первый байт финишного пакета
const char PACKET_ENDER = '#';  //последний байт финишного пакета

/* положение информации на экране */
const uint8_t finish_y = 50; //время финиша участника

// задаем константы
const String MODULE_NAME = "Finish"; //имя Bluetooth модуля

#define R1 4677 //резисторы в делителе напряжения
//finish1 - 4677
//finish2 - 4596
//finish3 - 4646
#define R2 1000

// задаем контакты, используемые трансивер-модулем:
#define ss 18
#define rst 14
#define dio0 26

// переменные
volatile unsigned long finishmillis;    // значение тысячных времени события по прерыванию (старта или финиша)
volatile unsigned long eventmillis = 0; // значение millis() во время события по прерыванию (старта или финиша)
unsigned long preveventmillis = 0;      // значение millis() во время предыдущего события по прерыванию (старта или финиша)
volatile time_t finishtime;             // время финиша
volatile bool finish = false;           // был ли финиш
String printfinish[5];

// Прерывание для установки времени старта
void setfinish();
// Время финиша на экран
void digitalFinishDisplay();
// отправка финишного пакета в Serial
void SendPacketToSerial(String time);
// отправка финишного пакета в Bluetooth serial
void SendPacketToSerialBT(String time);
// отправка финишного пакета в BLE
void SendPacketToBLE(String time);

void setup()
{
  pinMode(EVENT_PIN, INPUT_PULLUP);                                     // инициализируем пин, подключенный к кнопке, как
  attachInterrupt(digitalPinToInterrupt(EVENT_PIN), setfinish, RISING); //прерывание для считывания показаний финиша

  setupModule(MODULE_NAME);

  /* приведение вида финишного экрана в рабочее состояние */
  digitalClockTFTinit();
  tft.setCursor(0, finish_y - 13); //надпись "finish"
  tft << "Finish:" << endl;
}

void loop()
{
  syncRTCfromSerial();

  // чтение данных из SerialBT
  // Check if Data over SerialBT has arrived
  if (SerialBT.available() != 0)
  {
    // Get and parse received data
    String data = readBTSerial();
    if (!data.isEmpty())
    {
      String json = jsonToSettings(data);
      if (!json.isEmpty())
      {
        SerialBT.println(json);
      }
    }
  }

  //  static time_t tLast;                              //часы на tft
  static uint8_t secondLast;

  // time_t t = getUTC();
  time_t t = isrUTC;

  if (second(t) != secondLast) //таймер каждую секунду
  {
    secondLast = second(t);
    digitalClockTFT(t); //часы на экране
  }

  // if (finish)
  // {
  //   if (eventmillis - preveventmillis > EVENT_DELAY)
  //   {
  //     preveventmillis = eventmillis;
  //     SendPacketToSerial(TimeToString(finishtime, finishmillis));

  //     //ToDo: время финиша в Bluetooth
  //     SendPacketToSerialBT(TimeToString(finishtime, finishmillis));
  //   }
  //   digitalFinishDisplay();
  //   finish = false;
  // }

  if (finish)
  {
    preveventmillis = eventmillis;
    SendPacketToSerial(timeToString(finishtime, finishmillis));
    SendPacketToSerialBT(timeToString(finishtime, finishmillis));
    digitalFinishDisplay();
    finish = false;
    syncFromRTC();
  }

  //ToDo: сделать реальный парсинг поступающего пакета
  //проверяем, был ли получен пакет:
  // int packetSize = LoRa.parsePacket();
  // if (packetSize)
  // {
  //   String BT_string;
  //   // сообщаем, что пакет получен:
  //   //Serial.print("Received packet "); //  "Пакет получен "
  //   // считываем пакет:
  //   while (LoRa.available())
  //   {
  //     String LoRaData = LoRa.readString();
  //     //Serial.print(LoRaData);
  //     BT_string = LoRaData;
  //   }

  //   // печатаем RSSI пакета:
  //   // int rrsi = LoRa.packetRssi();
  //   // Serial.print(" with RSSI "); //  " с RSSI"
  //   // Serial.println(rrsi);
  //   // BT_string = BT_string + ", RSSI: " + rrsi;
  //   // SerialBT.print(BT_string);
  //   Serial.println(BT_string);
  // }

  // get vcc from battery
  // tickerVcc.update();
  //printVcc();
}

void IRAM_ATTR setfinish()
{
  /*
    time_t st = getUTC();
    unsigned long ms = millis();
    finishtime = st;                             //ставим время старта
    finishmillis = (ms - syncmillis);            //ставим время старта (тысячные секунд)
    finish = true;
  */

  //  unsigned long ms = millis();
  // time_t st = getUTC();
  // if (st - starttime > 3)
  // {                                  //ставим новое время старта, только если прошло более трёх секунд от предыдущего старта
  //   starttime = st;                  //ставим время старта
  //   startmillis = (ms - syncmillis); //ставим время старта (тысячные секунд)
  //   start = true;
  // }

  if (!finish) // проверка, что предыдущее прерывание было обработано
  {
    // time_t ft = getUTC();
    time_t ft = isrUTC;
    unsigned long ms = millis();
    if (ms - eventmillis > EVENT_DELAY)
    {                                   //ставим новое время старта, только если прошло более трёх секунд от предыдущего старта
      finishtime = ft;                  //ставим время старта
      finishmillis = (ms - syncmillis); //ставим время старта (тысячные секунд)
      eventmillis = ms;                 //ставим время события в millis
      finish = true;
    }
  }

  // проверяем, что предыдущее сообщение отправлено в bluetooth
  // if (!finish)
  // {
  //   finishtime = getUTC();
  //   eventmillis = millis();
  //   finishmillis = (eventmillis - syncmillis);
  //   finish = true;
  // }
}

void digitalFinishDisplay()
{
  static long printfinishmillis = 0;
  if (millis() - printfinishmillis > 1000)
  {
    printfinishmillis = millis();
    for (int i = 4; i > 0; i--)
    {
      printfinish[i] = printfinish[i - 1];
    }
    printfinish[0] = timeToString(finishtime, finishmillis);
    tft.fillRect(0, finish_y, tft.width(), 5 * 7 + 4 * 6, ST7735_BLACK);
    tft.setTextSize(1);
    tft.setCursor(0, finish_y);
    for (int i = 0; i < 5; i++)
    {
      tft.println(printfinish[i]);
      tft.setCursor(0, finish_y + 13 * (i + 1));
    }
  }
}

void SendPacketToSerial(String time)
{
  Serial << FINISH_HEADER << time << PACKET_ENDER << endl; //время в сериал порт
}

void SendPacketToSerialBT(String time)
{
  SerialBT << FINISH_HEADER << time << PACKET_ENDER << endl; //время в Bluetooth сериал
}
