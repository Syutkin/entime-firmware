#pragma once
#include <TimeLib.h>
#include <DS3232RTC.h> // https://github.com/JChristensen/DS3232RTC
#include <Streaming.h>

#define TIME_HEADER "T" // Header tag for serial time sync message

extern DS3232RTC myRTC;
extern volatile time_t isrUTC;            //ISR's copy of current time in UTC
extern volatile unsigned long syncmillis; // значение millis() которое было во время последней синхронизации (каждую секунду)

extern const uint8_t EVENT_PIN;
extern const uint8_t RTC_1HZ_PIN;

extern bool isInterruptAttached; // выставлено ли прерывание по событию

unsigned long processSyncMessage();

/*
 * return current time
 */
time_t getUTC();

/*
 * set the current time
 */
void setUTC(time_t utc);

/* 
 * 1Hz RTC interrupt handler increments the current time
 */
void incrementTime();

/*
 * синхронизация RTC и внутреннего времени из Serial порта
 */
void syncRTCfromSerial();

/*
 * Синхронизация внутреннего времени модуля со временем RTC
 */
void syncFromRTC();