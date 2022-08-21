#pragma once
#include <BluetoothSerial.h>

extern BluetoothSerial SerialBT;

/*
 * readBTSerial
 * read all data from BTSerial receive buffer
 * parse data for valid WiFi credentials
 */
String readBTSerial();