#include "tft_lcd.h"
#include "icons.h"
#include <Fonts/FreeMono9pt7b.h>

#define ST7735_GRAY 0xC618

TFT_LCD TFT = TFT_LCD();

TFT_LCD::TFT_LCD()
{
    ST7735.initR(INITR_GREENTAB); // инициализируем дисплей ST7735, у GREENTAB правильные корректировки:
                                  // _colstart = 2; _rowstart = 1;
    ST7735.setRotation(0);
    uint8_t data = 0xC0;
    ST7735.sendCommand(ST77XX_MADCTL, &data, 1); // Но цвета BRG, меняем обратно на RGB

    ST7735.fillScreen(ST7735_BLACK);
}

void TFT_LCD::drawBattery(uint8_t charge)
{
    ST7735.fillRect(BATTERY_X, TOPBAR_Y, ICON_WIDTH, ICON_HEIGHT, ST7735_BLACK);
    ST7735.drawBitmap(BATTERY_X, TOPBAR_Y, battery_high, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
}

void TFT_LCD::drawSignal(bool ntpOnline, wl_status_t wifi_status, int32_t rssi)
{
    ST7735.fillRect(SIGNAL_X, TOPBAR_Y, ICON_WIDTH, ICON_HEIGHT, ST7735_BLACK);
    if (wifi_status != WL_CONNECTED)
    {
        ST7735.drawBitmap(SIGNAL_X, TOPBAR_Y, signal_off, ICON_WIDTH, ICON_HEIGHT, ST7735_GRAY);
    }
    else if ((!ntpOnline))
    {
        ST7735.drawBitmap(SIGNAL_X, TOPBAR_Y, signal_alert, ICON_WIDTH, ICON_HEIGHT, ST7735_GRAY);
    }
    else
    {
        Serial << "RSSI: " << rssi << endl;
        if (rssi < -90)
        {
            ST7735.drawBitmap(SIGNAL_X, TOPBAR_Y, signal_outline, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else if (rssi < -80)
        {
            ST7735.drawBitmap(SIGNAL_X, TOPBAR_Y, signal_1, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else if (rssi < -65)
        {
            ST7735.drawBitmap(SIGNAL_X, TOPBAR_Y, signal_2, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else if (rssi < -55)
        {
            ST7735.drawBitmap(SIGNAL_X, TOPBAR_Y, signal_3, ICON_WIDTH, ICON_HEIGHT, ST7735_WHITE);
        }
        else
        {
            ST7735.drawBitmap(SIGNAL_X, TOPBAR_Y, signal_4, ICON_WIDTH, ICON_HEIGHT, ST7735_GREEN);
        }
    }
}

void TFT_LCD::drawBluetooth()
{
    ST7735.fillRect(BLUETOOTH_X, TOPBAR_Y, ICON_WIDTH, ICON_HEIGHT, ST7735_BLACK);
    ST7735.drawBitmap(BLUETOOTH_X, TOPBAR_Y, bluetooth, ICON_WIDTH, ICON_HEIGHT, ST7735_BLUE);
}

void TFT_LCD::drawClock(Datime dt)
{
    ST7735.fillRect(CLOCK_X, TOPBAR_Y, ICON_WIDTH * 5, ICON_HEIGHT, ST7735_BLACK);
    char str[9];
    sprintf(str, "%.2d:%.2d:%.2d", dt.hour, dt.minute, dt.second);
    ST7735.setCursor(CLOCK_X + 8, TOPBAR_Y + 8);
    ST7735.print(str);
}
