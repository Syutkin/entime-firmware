#include "bluetooth.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;

String readBTSerial()
{
  uint64_t startTimeOut = millis();
  String receivedData;
  int msgSize = 0;
  // Read RX buffer into String
  while (SerialBT.available() != 0)
  {
    receivedData += (char)SerialBT.read();
    msgSize++;
    // Check for timeout condition
    if ((millis() - startTimeOut) >= 5000)
    {
      log_w("Bluetooth received Data timeout");
      break;
    }
  }
  SerialBT.flush();
  Serial.println("Received message " + receivedData + " over Bluetooth");

  return receivedData;
}