#include "battery.h"

esp_adc_cal_characteristics_t *adc_chars;
extern const adc_atten_t atten = ADC_ATTEN_DB_6; //6dB ослабление (ADC_ATTEN_DB_6) от 150 до 1750mV
extern const adc_unit_t unit = ADC_UNIT_1;
static const adc_channel_t channel = ADC_CHANNEL_6;

double v_divider;

void check_efuse()
{
  //Check TP is burned into eFuse
  if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK)
  {
    Serial.println("eFuse Two Point: Supported");
  }
  else
  {
    Serial.println("eFuse Two Point: NOT supported");
  }
  //Check Vref is burned into eFuse
  if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK)
  {
    Serial.println("eFuse Vref: Supported");
  }
  else
  {
    Serial.println("eFuse Vref: NOT supported");
  }
}

int16_t voltageToCharge(int32_t voltage)
{
  int16_t charge = (voltage - MAX_DISCHARGE) / CHARGE_DIVIDER;
  if (charge > 100)
  {
    charge = 100;
  }
  return charge;
}

uint32_t getVcc()
{
  uint32_t adc_reading = 0;
  for (int i = 0; i < NO_OF_SAMPLES; i++)
  {
    adc_reading += adc1_get_raw((adc1_channel_t)channel);
  }
  adc_reading /= NO_OF_SAMPLES;
  uint32_t voltage = v_divider * esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
  return voltage;
}

