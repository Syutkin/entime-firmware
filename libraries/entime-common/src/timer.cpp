#include "timer.h"
#include "settings.h"

//ToDo: move pins to GPIO32-GPIO39
const uint8_t EVENT_PIN(12);   // номер входа, подключенный к кнопке
const uint8_t RTC_1HZ_PIN(13); // RTC provides a 1Hz interrupt signal on this pin

// For non-AVR boards only. Not needed for AVR boards.
DS3232RTC myRTC(false); // tell constructor not to initialize the I2C bus.

volatile time_t isrUTC;                   //ISR's copy of current time in UTC
volatile unsigned long isrSyncMillis;     // значение millis() которое было во время последней синхронизации (каждую секунду)
volatile bool isEvent = false;            // было ли внешнее прерывание по событию
volatile unsigned long eventMillis = 0;   // значение millis() во время обработки события по прерыванию
volatile time_t eventTime = 0;      

void initTimer()
{
  myRTC.begin(); // initialize the I2C bus here.
  pinMode(RTC_1HZ_PIN, INPUT_PULLUP); // enable pullup on interrupt pin (RTC SQW pin is open drain)
  attachInterrupt(digitalPinToInterrupt(RTC_1HZ_PIN), handleIncrementTime, FALLING);
  myRTC.squareWave(DS3232RTC::SQWAVE_1_HZ); // 1 Hz square wave

  pinMode(EVENT_PIN, INPUT_PULLUP);                                    //инициализируем пин, подключенный к кнопке, как вход
  attachInterrupt(digitalPinToInterrupt(EVENT_PIN), handleEvent, RISING); //прерывание для считывания времени события 
}

// time_t IRAM_ATTR getUTC()
// {
//   // noInterrupts();
//   // time_t utc = isrUTC;
//   // interrupts();
//   // return utc;
//   return isrUTC;
// }

// time_t getUTCNoInterrupts()
// {
//   noInterrupts();
//   time_t utc = isrUTC;
//   interrupts();
//   return utc;
// }

void IRAM_ATTR setUTC(time_t utc)
{
  noInterrupts();
  isrUTC = utc;
  interrupts();
}

void IRAM_ATTR handleIncrementTime()
{
  ++isrUTC;
  isrSyncMillis = millis();
}

void syncRTCfromSerial()
{
  if (Serial.available())
  {
    time_t t = processSyncMessage();
    if (t != 0)
    {
      myRTC.set(t + 1); // set the RTC and the system time to the received value +1 second
      Serial << "RTC synced to local time" << endl;
      preferences.begin("Misc", false);
      preferences.putInt("syncDate", t + 1);
      preferences.end();
      syncFromRTC();
    }
  }
}

void syncFromRTC()
{
  noInterrupts();
  // time_t utc = getUTCNoInterrupts(); // synchronize with RTC
  time_t utc = isrUTC; // synchronize with RTC
  while (utc == isrUTC)
    ;                // wait for increment to the next second
  utc = myRTC.get(); // get the time from the RTC
  interrupts();
  setUTC(utc);       // set our time to the RTC's time
  Serial << "RTC: Module time synced from RTC" << endl;
}

unsigned long processSyncMessage()
{
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if (Serial.find(TIME_HEADER))
  {
    pctime = Serial.parseInt();
    return pctime;
    if (pctime < DEFAULT_TIME)
    {              // check the value is a valid time (greater than Jan 1 2013)
      pctime = 0L; // return 0 to indicate that the time is not valid
    }
  }
  return pctime;
}

void IRAM_ATTR handleEvent()
{
  if (!isEvent)
  {
    eventMillis = millis() - isrSyncMillis;
    eventTime = isrUTC;
    isEvent = true;
  }
}