#pragma once
#ifndef _BT_SERIAL_H
#define _BT_SERIAL_H

#include <BluetoothSerial.h>
#include <Streaming.h>
#include "settings.h"

class BT_SERIAL
{
private:
    String parse();

public:
    BluetoothSerial serial;
    void begin();
    void read();
};

extern BT_SERIAL BT;

#endif
