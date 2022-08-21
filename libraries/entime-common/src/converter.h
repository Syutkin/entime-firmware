#pragma once
#include <Arduino.h>
#include <TimeLib.h>

/*
 * Конвертирует двузначное число в строку с ведущим нулём
 */
String digitsToString(int i);

/*
 * Конвертирует время в строку
 */
String timeToString(time_t time);

/*
 * Конвертирует время с тысячными в строку
 */
String timeToString(time_t time, int16_t millis);