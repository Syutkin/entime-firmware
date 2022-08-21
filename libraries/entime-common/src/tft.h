#pragma once
#include <Adafruit_ST7735.h>
#include <Streaming.h>
#include <TimeLib.h>

#include "converter.h"

extern Adafruit_ST7735 tft;

/*
 * Инициализация времени на экране
 */
void digitalClockTFTinit();

/*
 * Вывод текущего времени на экране
 */
void digitalClockTFT(time_t t);

/*
 * Выводит на экран напряжение VCC
 */
void batteryToTFT(int32_t voltage, int16_t charge);

/*
 * Вывод строки на текущем месте курсора
 */
void drawText(const char *text, uint16_t color);