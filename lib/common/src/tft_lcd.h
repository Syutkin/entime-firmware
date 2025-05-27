#pragma once
#ifndef _TFT_LCD_H
#define _TFT_LCD_H

#include <Adafruit_ST7735.h>
#include <Streaming.h>
#include <Datime.h>
#include <WiFiType.h>

// define not needed for all pins if connected to VSPI; reference for ESP32 physical pins connections to VSPI:
//  #define TFT_SCLK 15 //SCL  //SCK - CLK  // SPI `serial port clock - SPI clock input on Display module //hw 15 -> 5
//  #define TFT_MOSI 4  //SDA  //SDA - DIN  // MISO - Master In Slave Out - SPI input on Display module   //hw  4 -> 27
#define TFT_CS 17  // CS   //CS - CS    // CS - Chip Select - SPI CS input on Display module
#define TFT_RST 16 // RES  //RST- RST   // Reset (optional, -1 if unused)
#define TFT_DC 2   // DC   //RS - D/C   // Data/Command
// #define TFT_BACKLIGHT 47 // Display backlight pin

// Где расположен topbar
#define TOPBAR_Y 0

// Где расположены часы
#define CLOCK_X 0

// Где расположена иконка Bluetooth
#define BLUETOOTH_X 80

// Где расположена иконка WiFi
#define SIGNAL_X 96

// Где расположена иконка батареи
#define BATTERY_X 112

class TFT_LCD
{
public:
    TFT_LCD();
    Adafruit_ST7735 ST7735 = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

    void drawBattery(uint8_t charge);
    void drawSignal(bool ntpOnline, wl_status_t wifi_status, int32_t rssi);
    void drawBluetooth();
    void drawClock(Datime dt);
};

extern TFT_LCD TFT;

#endif
