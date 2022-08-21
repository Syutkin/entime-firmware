#pragma once
#include <Arduino.h>
#include <driver/adc.h> //для измерения напряжения питания
#include "esp_adc_cal.h"

const int16_t MAX_DISCHARGE = 6500;
const int16_t MAX_CHARGE = 8200;

#define CHARGE_DIVIDER ((MAX_CHARGE - MAX_DISCHARGE) / 100)

#define DEFAULT_VREF 1100
#define NO_OF_SAMPLES 64 //Multisampling

extern esp_adc_cal_characteristics_t *adc_chars;
extern const adc_atten_t atten;
extern const adc_unit_t unit;

extern double v_divider;

void check_efuse();

/*
 * Конвертирует напряжение в процент заряда
 */
int16_t voltageToCharge(int32_t voltage);

/*
 * Получение значения питающего напряжения
 */
uint32_t getVcc();