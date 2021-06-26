/*
Модуль для работы с отсечкой старта
  Версия 18/03/2021 "BLE эдишн"
  Реализовано:
    Основная борда ESP32 + LoRa
    Модуль RTC (DS3231)
    Дисплей (ST7735)
    Время cтарта передаётся по Bluetooth
    Время cтарта передаётся по BLE
    Пищалка
    ReadVcc
  В процессе:
    Время cтарта передаётся по LoRa
*/

#include <Arduino.h>
#include <entime.h>
#include <Tone32.h> //Buzzer в стартовом модуле

// #if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
// #error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
// #endif

/* настройки */
const char START_HEADER = '$'; //первый байт стартового пакета
const char PACKET_ENDER = '#'; //последний байт стартового пакета
const char BEEP_HEADER = 'B';  //первый байт beep пакета
const char VOICE_HEADER = 'V'; //первый байт voice пакета
const uint16_t MAX_CORRECTION = 10000;

/* положение информации на экране */
//const uint8_t clock_y = 0;              //часы
const uint8_t countdown_y = 30;         //обратный отсчёт
const uint8_t countdown_x = 70;         //обратный отсчёт
const uint8_t correction_y = 78;        //поправка
const uint8_t start_y = 110;            //время старта участника (справочно)
const uint8_t correction_border_y = 75; //рамка поправки

/* константы */
#define MODULE_TYPE "entime"
const String MODULE_NAME = "Start"; //имя Bluetooth модуля
#define BUZZER_PIN 23
#define BUZZER_CHANNEL 0

#define R1 4733 //резисторы в делителе напряжения
//start1 - 4733
//start2 - 4722
//start3 - 4646
#define R2 1000

// define the pins used by the transceiver module
#define ss 18
#define rst 14
#define dio0 26

/* переменные */
int cor;                            // поправка, которая выводится на экран
volatile unsigned long startmillis; // значение тысячных времени события по прерыванию (старта или финиша)
volatile time_t starttime;          // время старта
volatile bool start = false;        // был ли старт

// For non-AVR boards only. Not needed for AVR boards.
// DS3232RTC myRTC(false); // tell constructor not to initialize the I2C bus.

// объявление функций
void setStart();
void countdown(time_t t);
void digitalStartDisplay();
// Вывод поправки на экран
void correctionDisplay(int popr);
// Возвращает стартовую поправку
int Correction(time_t time);
// Отправка стартового пакета в Serial
void SendPacketToSerial(String time, int popr);
// Отправка стартового пакета в Bluetooth Serial
void SendPacketToBluetooth(String time, int popr);
// Отправка стартового пакета в BLE
void SendPacketToBLE(String time, int popr);
// Отправка стартового пакета в LoRa
void SendPacketToLoRa(String time, int popr);
// Отправка уведомления в Bluetooth serial, что можно включать обратный отсчёт
void SendBeepToBluetooth(String time);
// Отправка уведомления в BLE, что можно включать обратный отсчёт
void SendBeepToBLE(String time);
// Отправка уведомления в Bluetooth serial, что можно включать голосовое сообщение
void SendVoiceToBluetooth(String time);
// Отправка уведомления в BLE, что можно включать голосовое сообщение
void SendVoiceToBLE(String time);
// Короткий гудок пищалки
void BuzzerShort();
// Длинный гудок пищалки
void BuzzerLong();
// Вывод на TFT обратный отсчёт
void printTftCountdown(String cd);

void setup()
{
  pinMode(EVENT_PIN, INPUT_PULLUP);                                    //инициализируем пин, подключенный к кнопке, как вход
  attachInterrupt(digitalPinToInterrupt(EVENT_PIN), setStart, RISING); //прерывание для считывания показаний старта

  setupModule(MODULE_NAME);

  /* приведение вида стартового экрана в рабочее состояние */
  digitalStartDisplay(); //время старта на экране
  correctionDisplay(0);  //поправка на экране
  digitalClockTFTinit();
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
    countdown(t);
  }

  if (start)
  {
    digitalStartDisplay(); //время старта на экране  //ToDo: на вход время старта
    cor = Correction(starttime);
    correctionDisplay(cor);                                           //поправка на экране
    SendPacketToSerial(timeToString(starttime, startmillis), cor);    //время в сериал порт
    SendPacketToBluetooth(timeToString(starttime, startmillis), cor); //время в Bluetooth Serial
    start = false;

    //ToDo: отправка времени старта в LoRa
    //Send LoRa packet to receiver
    //LoRa.beginPacket();
    //LoRa.print("hello!");
    //LoRa.print(counter);
    //LoRa.endPacket();

    //SendPacketToLoRa(TimeToString(starttime, startmillis), cor);
  }

  // get vcc from battery
  tickerVcc.update();
}

/* прерывание для установки времени старта */

void setStart()
{
  unsigned long ms = millis();
  // time_t st = getUTC();
  time_t st = isrUTC;
  if (st - starttime > 3)
  {                                  //ставим новое время старта, только если прошло более трёх секунд от предыдущего старта
    starttime = st;                  //ставим время старта
    startmillis = (ms - syncmillis); //ставим время старта (тысячные секунд)
    start = true;
  }
}

/* обратный отсчёт */
void countdown(time_t t)
{
  switch (second(t))
  {
  case 55:
    printTftCountdown("5");
    BuzzerShort();
    break;
  case 56:
    printTftCountdown("4");
    BuzzerShort();
    SendBeepToBluetooth(timeToString(t));
    break;
  case 57:
    printTftCountdown("3");
    BuzzerShort();
    break;
  case 58:
    printTftCountdown("2");
    BuzzerShort();
    break;
  case 59:
    printTftCountdown("1");
    BuzzerShort();
    break;
  case 0:
    printTftCountdown("GO");
    BuzzerLong();
    break;
  case 10:
    tft.fillRect(countdown_x, countdown_y, tft.width(), 28, ST7735_BLACK);
    break;
  case 15:
    SendVoiceToBluetooth(timeToString(t));
    break;
  case 30:
    syncFromRTC();
    break;
  }
}

void printTftCountdown(String cd)
{
  tft.fillRect(countdown_x, countdown_y, tft.width(), 28, ST7735_BLACK);
  tft.setTextSize(4);
  tft.setCursor(countdown_x, countdown_y);
  tft.print(cd);
}

/* вывод времени старта на экран */

// void digitalStartDisplay()
// {
//   tft.fillRect(0, start_y, tft.width(), 7, ST7735_BLACK);
//   tft.setTextSize(1);
//   tft.setCursor(0, start_y);
//   printDigits(hour(starttime));
//   tft.print(":");
//   printDigits(minute(starttime));
//   tft.print(":");
//   printDigits(second(starttime));
//   tft.print(".");
//   printZeroMillis(startmillis);
// }

void digitalStartDisplay()
{
  tft.fillRect(0, start_y, tft.width(), 7, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, start_y);
  tft.print(timeToString(starttime, startmillis));
}

void correctionDisplay(int popr)
{
  if (popr < MAX_CORRECTION)
  {
    tft.fillRect(0, correction_border_y, tft.width(), 27, ST7735_BLUE);
    tft.setTextSize(3);
    tft.setCursor(0, correction_y);
    tft.print(popr);
  }
}

int Correction(time_t time)
{
  int result;
  if (second(time) < 10)
  { //старт после нуля, поправка отрицательная
    result = 0 - ((second(time) * 1000 + startmillis));
    return result;
  }
  else if (second(time) > 50)
  { //старт раньше нуля, поправка положительная
    result = MAX_CORRECTION - ((second(time) - 50) * 1000 + startmillis);
    /*if (result < 1000) {           //странная бессмысленная проверка, может что-то глючило?
      tft.print(result);
      }
      if (result > 999) {
      tft.print(result);
      }*/
    return result;
  }
  else
  {
    return MAX_CORRECTION;
  }
}

void SendPacketToSerial(String time, int popr)
{
  Serial << START_HEADER << time << ";" << popr << PACKET_ENDER << endl; //время в сериал порт
}

void SendPacketToBluetooth(String time, int popr)
{
  if (popr < MAX_CORRECTION)
  {
    SerialBT << START_HEADER << time << ";" << popr << PACKET_ENDER << endl; //время в Bluetooth serial
  }
}

void SendPacketToLoRa(String time, int popr)
{
  if (popr < MAX_CORRECTION)
  {
    //LoRa.beginPacket();
    //LoRa << START_HEADER << time << ";" << popr << PACKET_ENDER; //время в LoRa
    //LoRa.endPacket();
  }
}

void SendBeepToBluetooth(String time)
{
  //if (SerialBT.connected())
  SerialBT << BEEP_HEADER << time << PACKET_ENDER << endl;
}

void SendVoiceToBluetooth(String time)
{
  //if (SerialBT.connected)
  SerialBT << VOICE_HEADER << time << PACKET_ENDER << endl;
}

void BuzzerShort()
{
  if (settings.Buzzer.active)
    tone(BUZZER_PIN, settings.Buzzer.shortFrequency, 500, BUZZER_CHANNEL);
}

void BuzzerLong()
{
  if (settings.Buzzer.active)
    tone(BUZZER_PIN, settings.Buzzer.longFrequency, 950, BUZZER_CHANNEL);
}