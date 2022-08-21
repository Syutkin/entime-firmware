#include "entime.h"

Ticker tickerVcc(printVcc, 60000);

void printVcc()
{
  uint32_t voltage = getVcc();
  int16_t charge = voltageToCharge(voltage);
  batteryToTFT(voltage, charge);
  log_d("Voltage: %d, charge: %d", voltage, charge);
}

void setupModule(String MODULE_NAME)
{
  Serial.begin(57600);
  myRTC.begin(); // initialize the I2C bus here.

  pinMode(RTC_1HZ_PIN, INPUT_PULLUP); // enable pullup on interrupt pin (RTC SQW pin is open drain)
  attachInterrupt(digitalPinToInterrupt(RTC_1HZ_PIN), incrementTime, FALLING);
  myRTC.squareWave(DS3232RTC::SQWAVE_1_HZ); // 1 Hz square wave

  loadSettings(MODULE_NAME);

  // Use this initializer if you're using a 1.8" TFT
  tft.initR(INITR_BLACKTAB); // initialize a ST7735S chip, black tab
  tft.setRotation(2);
  tft.fillScreen(ST7735_BLACK);
  drawText("TFT Initialized", ST7735_WHITE);
  tft << endl;

  Serial << "Module: " << MODULE_NAME << " module ESP32+LoRa" << endl;
  Serial << "Firmware: " << VERSION << endl;
  Serial << "Firmware: Upload at " << F(__DATE__ " " __TIME__) << endl;
  tft << "Module type:" << endl;
  tft << MODULE_NAME << " ESP32+LoRa" << endl;
  tft << "Firmware:" << endl;
  tft << VERSION << endl;
  tft << "Upload at:" << endl;
  tft << F(__DATE__ " " __TIME__) << endl;
  tft << endl;

  delay(2000);

  tft << "Waiting for RTC..." << endl;

  syncFromRTC();
  time_t t = getUTC();
  char buf1[20];
  sprintf(buf1, "%02d:%02d:%02d %02d/%02d/%02d", hour(t), minute(t), second(t), day(t), month(t), year(t));

  tft << "RTC synced: " << endl;
  tft << buf1 << endl;

  if (t - last_rtc_syncdate > 86400 /* секунд в сутках */)
  {
    Serial << "RTC: time update needed" << endl;
    drawText("Time update needed", ST7735_RED);
  }
  tft << endl;

  delay(2000);

  //ToDo: пока не выключать BT из настроек
  // if (settings.Bluetooth.active)
  if (true)
  {
    if (!SerialBT.begin(settings.Bluetooth.name + settings.Bluetooth.number))
    {
      Serial << "Bluetooth: Module " << settings.Bluetooth.name << settings.Bluetooth.number << " failed to start" << endl;
      tft << "Bluetooth:" << endl;
      tft << settings.Bluetooth.name << settings.Bluetooth.number << " failed to start" << endl;
    }
    else
    {
      Serial << "Bluetooth: Module " << settings.Bluetooth.name << settings.Bluetooth.number << " is ready to pair" << endl;
      tft << "Bluetooth:" << endl;
      tft << settings.Bluetooth.name << settings.Bluetooth.number << " is ready" << endl;
    }
  }
  else
  {
    Serial << "Bluetooth off" << endl;
    tft << "Bluetooth off" << endl;
  }

  // настройка LoRa:
  // LoRa.setPins(ss, rst, dio0);

  // // в параметре метода LoRa.begin(---E-) укажите частоту,
  // // соответствующую региону вашего проживания;
  // // примечание: она должна соответствовать частоте отправителя;
  // // 433E6 для Азии
  // // 866E6 для Европы
  // // 915E6 для Северной Америки
  // if (!LoRa.begin(433E6))
  // {
  //   Serial.println("Starting LoRa failed!");
  //   // while (1);
  // }
  // else
  // {
  //   LoRa.setSyncWord(0xF3);
  //   Serial.println("LoRa Initializing OK!");
  // }

  delay(5000);
  tft.fillScreen(ST7735_BLACK); //clear display after initialization

  // настройки для показа VCC
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, atten);
  check_efuse();

  //Characterize ADC at particular atten
  adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
  v_divider = (float)(settings.VCC.r1 + settings.VCC.r2) / (float)settings.VCC.r2;

  printVcc();
  tickerVcc.start();
}
