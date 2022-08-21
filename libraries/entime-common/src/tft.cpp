#include "tft.h"

//define not needed for all pins if connected to VSPI; reference for ESP32 physical pins connections to VSPI:
// #define TFT_SCLK 15 //SCL  //SCK - CLK  // SPI `serial port clock - SPI clock input on Display module //hw 15 -> 5
// #define TFT_MOSI 4  //SDA  //SDA - DIN  // MISO - Master In Slave Out - SPI input on Display module   //hw  4 -> 27
#define TFT_CS 17   //CS   //CS - CS    // CS - Chip Select - SPI CS input on Display module
#define TFT_RST 16  //RES  //RST- RST   // Reset (optional, -1 if unused)
#define TFT_DC 2    //DC   //RS - D/C   // Data/Command
//#define TFT_BACKLIGHT 47 // Display backlight pin

// Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_RS, TFT_MOSI, TFT_SCLK, TFT_RES);
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

/* положение информации на экране */
const uint8_t clock_y = 0; //часы
const uint8_t charge_y = 146; //индикация заряда

void digitalClockTFTinit()
{
  tft.setCursor(0, clock_y); //двоеточия у часов
  tft.setTextSize(2);
  tft.print("00:00:00");
}

void digitalClockTFT(time_t t)
{
  static uint8_t prevSecond;
  static uint8_t prevMinute;
  static uint8_t prevHour;
  // digital clock display of the time
  tft.setTextSize(2); //ширина шрифта 12?
  //    tft.fillRect(0, clock_y, tft.width(), 14, ST7735_BLACK);

  if (hour(t) != prevHour)
  {
    //tft.fillRect(0, clock_y, 23, 14, ST7735_BLACK);
    tft.setCursor(0, clock_y);
    tft.setTextColor(ST7735_BLACK);
    tft.print(digitsToString(prevHour));
    tft.setCursor(0, clock_y);
    tft.setTextColor(ST7735_WHITE);
    tft.print(digitsToString(hour(t)));
    prevHour = hour(t);
  }
  if (minute(t) != prevMinute)
  {
    //tft.fillRect(36, clock_y, 23, 14, ST7735_BLACK);
    tft.setCursor(36, clock_y);
    tft.setTextColor(ST7735_BLACK);
    tft.print(digitsToString(prevMinute));
    tft.setCursor(36, clock_y);
    tft.setTextColor(ST7735_WHITE);
    tft.print(digitsToString(minute(t)));
    prevMinute = minute(t);
  }
  if (second(t) != prevSecond)
  {
    //tft.fillRect(72, clock_y, 23, 14, ST7735_BLACK);
    tft.setCursor(72, clock_y);
    tft.setTextColor(ST7735_BLACK);
    tft.print(digitsToString(prevSecond));
    tft.setCursor(72, clock_y);
    tft.setTextColor(ST7735_WHITE);
    tft.print(digitsToString(second(t)));
    prevSecond = second(t);
  }
}

void batteryToTFT(int32_t voltage, int16_t charge)
{
  tft.fillRect(11, charge_y, 24, 14, ST7735_BLACK);
  tft.fillRect(101, charge_y, tft.width(), 14, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, charge_y);
  if (charge > 1)
  {
    tft << "V=" << voltage << "mV Battery:" << charge << "%";
  }
  else if (charge < -100)
  {
    tft << "V=" << voltage << "mV Battery OFF";
  }
  else
  {
    tft << "V=" << voltage << "mV Battery LOW!";
  }
}

void drawText(const char *text, uint16_t color)
{
  tft.setTextColor(color);
  tft.setTextWrap(true);
  tft.println(text);
  tft.setTextColor(ST7735_WHITE);
}
