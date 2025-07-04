#include "tft_lcd.h"
#include "icons.h"
#include <Fonts/FreeMono9pt7b.h>

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

// More colors
#define ST7735_GRAY 0x8410

TFT_LCD TFT;

GFXcanvas16 icon_canvas(ICON_WIDTH, ICON_HEIGHT);

void TFT_LCD::begin()
{
    ST7735.initR(INITR_GREENTAB); // инициализируем дисплей ST7735, у GREENTAB правильные координаты:
                                  // _colstart = 2; _rowstart = 1;
    ST7735.setRotation(0);
    uint8_t data = 0xC0;
    ST7735.sendCommand(ST77XX_MADCTL, &data, 1); // Но цвета BRG, меняем обратно на RGB

    ST7735.fillScreen(ST7735_BLACK);
}

void TFT_LCD::drawBattery(uint8_t charge)
{
    icon_canvas.fillScreen(ST7735_BLACK);
    icon_canvas.setCursor(0, 0);

    if (charge < 10)
    {
        icon_canvas.drawBitmap(0, 0, battery_outline, ICON_WIDTH, ICON_HEIGHT, ST7735_RED);
    }
    else if (charge < 40)
    {
        icon_canvas.drawBitmap(0, 0, battery_low, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
    }
    else if (charge < 70)
    {
        icon_canvas.drawBitmap(0, 0, battery_medium, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
    }
    else
    {
        icon_canvas.drawBitmap(0, 0, battery_high, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
    }
    ST7735.drawRGBBitmap(BATTERY_X, TOPBAR_Y, icon_canvas.getBuffer(), icon_canvas.width(), icon_canvas.height());
}

void TFT_LCD::drawSignal(bool ntpOnline, wl_status_t wifi_status, int32_t rssi)
{
    icon_canvas.fillScreen(ST7735_BLACK);
    icon_canvas.setCursor(0, 0);
    if (wifi_status != WL_CONNECTED)
    {
        icon_canvas.drawBitmap(0, 0, signal_off, ICON_WIDTH, ICON_HEIGHT, ST7735_GRAY);
    }
    else if ((!ntpOnline))
    {
        icon_canvas.drawBitmap(0, 0, signal_alert, ICON_WIDTH, ICON_HEIGHT, ST7735_RED);
    }
    else
    {
        if (rssi < -90)
        {
            icon_canvas.drawBitmap(0, 0, signal_outline, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else if (rssi < -80)
        {
            icon_canvas.drawBitmap(0, 0, signal_1, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else if (rssi < -65)
        {
            icon_canvas.drawBitmap(0, 0, signal_2, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else if (rssi < -55)
        {
            icon_canvas.drawBitmap(0, 0, signal_3, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else
        {
            icon_canvas.drawBitmap(0, 0, signal_4, ICON_WIDTH, ICON_HEIGHT, ST7735_GREEN);
        }
    }
    ST7735.drawRGBBitmap(SIGNAL_X, TOPBAR_Y, icon_canvas.getBuffer(), icon_canvas.width(), icon_canvas.height());
}

void TFT_LCD::drawBluetooth(bool isAvailable, bool hasClients)
{
    icon_canvas.fillScreen(ST7735_BLACK);
    icon_canvas.setCursor(0, 0);
    if (!isAvailable)
    {
        icon_canvas.drawBitmap(0, 0, bluetooth_off, ICON_WIDTH, ICON_HEIGHT, ST7735_GRAY);
    }
    else if (!hasClients)
    {
        icon_canvas.drawBitmap(0, 0, bluetooth, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
    }
    else
    {
        icon_canvas.drawBitmap(0, 0, bluetooth_connect, ICON_WIDTH, ICON_HEIGHT, ST7735_BLUE);
    }
    ST7735.drawRGBBitmap(BLUETOOTH_X, TOPBAR_Y, icon_canvas.getBuffer(), icon_canvas.width(), icon_canvas.height());
}

void TFT_LCD::drawClock(Datime dt)
{
    ST7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
    char str[9];
    sprintf(str, "%.2d:%.2d:%.2d", dt.hour, dt.minute, dt.second);
    ST7735.setCursor(CLOCK_X + 8, TOPBAR_Y + 8);
    ST7735 << str;
}
