#pragma once
#include <Arduino.h>
// #include <Streaming.h>
#include <Ticker.h>

#include "battery.h"
#include "bluetooth.h"
#include "settings.h"
#include "converter.h"
#include "tft.h"
#include "timer.h"

#define VERSION "Summer 2022 Edit 21/08/22"
#define MODULE_TYPE "entime"

extern Ticker tickerVcc;

void setupModule(String MODULE_NAME);

/*
 * Печать значений напряжения на питании модуля
 */
void printVcc();
