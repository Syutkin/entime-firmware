// https://pkolt.github.io
#ifndef ICONS_H
#define ICONS_H

#define ICON_WIDTH 16
#define ICON_HEIGHT 16

static const unsigned char bluetooth[] = {
    0x0, 0x0,  0x1, 0x0,  0x1, 0x80, 0x1, 0xc0,
    0x9, 0xe0, 0x5, 0xa0, 0x3, 0xc0, 0x1, 0x80,
    0x1, 0x80, 0x3, 0xc0, 0x5, 0xa0, 0x9, 0xe0,
    0x1, 0xc0, 0x1, 0x80, 0x1, 0x0,  0x0, 0x0 };

static const unsigned char bluetooth_off[] = {
    0x0, 0x0,  0x1, 0x0,  0x1, 0x80, 0x11, 0xc0,
    0x9, 0xe0, 0x5, 0xa0, 0x2, 0xc0, 0x1,  0x0, 
    0x1, 0x80, 0x3, 0xc0, 0x5, 0xa0, 0x9,  0xf0,
    0x1, 0xc8, 0x1, 0x80, 0x1, 0x0,  0x0,  0x0 };

static const unsigned char bluetooth_connect[] = {
    0x0,  0x0,  0x1, 0x0,  0x1, 0x80, 0x1,  0xc0, 
    0x9,  0xe0, 0x5, 0xa0, 0x3, 0xc0, 0x31, 0x8c, 
    0x31, 0x8c, 0x3, 0xc0, 0x5, 0xa0, 0x9,  0xe0,
    0x1,  0xc0, 0x1, 0x80, 0x1, 0x0,  0x0,  0x0 };

static const unsigned char battery_outline[] = { 
    0x0, 0x0, 0x3, 0xc0, 0x3, 0xc0, 0xf,  0xf0,
    0x8, 0x10, 0x8, 0x10, 0x8, 0x10, 0x8, 0x10,
    0x8, 0x10, 0x8, 0x10, 0x8, 0x10, 0x8, 0x10,
    0x8, 0x10, 0x8, 0x10, 0xf, 0xf0, 0x0, 0x0 };

static const unsigned char battery_low[] = { 
    0x0, 0x0, 0x3, 0xc0, 0x3, 0xc0, 0xf,  0xf0,
    0x8, 0x10, 0x8, 0x10, 0x8, 0x10, 0x8, 0x10,
    0x8, 0x10, 0x8, 0x10, 0x8, 0x10, 0xb, 0xd0,
    0xb, 0xd0, 0x8, 0x10, 0xf, 0xf0, 0x0, 0x0 };

static const unsigned char battery_medium[] = { 
    0x0, 0x0, 0x3, 0xc0, 0x3, 0xc0, 0xf,  0xf0, 
    0x8, 0x10, 0x8, 0x10, 0x8, 0x10, 0x8, 0x10, 
    0xb, 0xd0, 0xb, 0xd0, 0x8, 0x10, 0xb, 0xd0,
    0xb, 0xd0, 0x8, 0x10, 0xf, 0xf0, 0x0, 0x0 };

static const unsigned char battery_high[] = { 
    0x0, 0x0, 0x3, 0xc0, 0x3, 0xc0, 0xf,  0xf0, 
    0x8, 0x10, 0xb, 0xd0, 0xb, 0xd0, 0x8, 0x10, 
    0xb, 0xd0, 0xb, 0xd0, 0x8, 0x10, 0xb, 0xd0,
    0xb, 0xd0, 0x8, 0x10, 0xf, 0xf0, 0x0, 0x0 };

static const unsigned char signal_outline[] = { 
    0x0,  0x0,  0x0,  0x7,  0x0,  0x5,  0x0,  0x5, 
    0x0,  0x75, 0x0,  0x55, 0x0,  0x55, 0x7,  0x55, 
    0x5,  0x55, 0x5,  0x55, 0x75, 0x55, 0x55, 0x55, 
    0x55, 0x55, 0x55, 0x55, 0x77, 0x77, 0x0,  0x0 };

static const unsigned char signal_1[] = { 
    0x0,  0x0,  0x0,  0x7,  0x0,  0x5,  0x0,  0x5,
    0x0,  0x75, 0x0,  0x55, 0x0,  0x55, 0x7,  0x55,
    0x5,  0x55, 0x5,  0x55, 0x75, 0x55, 0x75, 0x55,
    0x75, 0x55, 0x75, 0x55, 0x77, 0x77, 0x0,  0x0 };

static const unsigned char signal_2[] = { 
    0x0,  0x0,  0x0,  0x7,  0x0,  0x5,  0x0,  0x5,
    0x0,  0x75, 0x0,  0x55, 0x0,  0x55, 0x7,  0x55,
    0x7,  0x55, 0x7,  0x55, 0x77, 0x55, 0x77, 0x55,
    0x77, 0x55, 0x77, 0x55, 0x77, 0x77, 0x0,  0x0 };

static const unsigned char signal_3[] = { 
    0x0,  0x0,  0x0,  0x7,  0x0,  0x5,  0x0,  0x5,
    0x0,  0x75, 0x0,  0x75, 0x0,  0x75, 0x7,  0x75,
    0x7,  0x75, 0x7,  0x75, 0x77, 0x75, 0x77, 0x75,
    0x77, 0x75, 0x77, 0x75, 0x77, 0x77, 0x0,  0x0 };

static const unsigned char signal_4[] = { 
    0x0,  0x0,  0x0,  0x7,  0x0,  0x7,  0x0,  0x7,
    0x0,  0x77, 0x0,  0x77, 0x0,  0x77, 0x7,  0x77,
    0x7,  0x77, 0x7,  0x77, 0x77, 0x77, 0x77, 0x77,
    0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x0,  0x0 };

static const unsigned char signal_off[] = {
    0x0,  0x0,  0x0,  0x7,  0x0,  0x7,  0x10, 0x7,
    0x8,  0x77, 0x4,  0x77, 0x2 , 0x77, 0x7,  0x37,
    0x7,  0x97, 0x7,  0x47, 0x77, 0x67, 0x77, 0x73,
    0x77, 0x79, 0x77, 0x74, 0x77, 0x76, 0x0,  0x0 };

static const unsigned char signal_alert[] = {
    0x0,  0x0,  0x18, 0x7,  0x18, 0x7,  0x18, 0x7,
    0x18, 0x77, 0x18, 0x77, 0x18, 0x77, 0x1b, 0x77,
    0x1b, 0x77, 0x3,  0x77, 0x5b, 0x77, 0x5b, 0x77,
    0x43, 0x77, 0x77, 0x77, 0x77, 0x77, 0x0,  0x0 };

#endif // ICONS_H