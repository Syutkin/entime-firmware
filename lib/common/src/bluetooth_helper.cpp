#include "bluetooth_helper.h"


BT_SERIAL BT;

String myName = "ESP32-BT-Master";

void BT_SERIAL::begin()
{
        Serial << "Start Bluetooth" << endl;
        serial.begin(myName);
}

void BT_SERIAL::read()
{
    // чтение данных из SerialBT
  if (serial.available() != 0)
  {
    // Get and parse received data
    String data = parse();
    if (!data.isEmpty())
    {
      String json = fromJson(data);
      if (!json.isEmpty())
      {
        serial.println(json);
      }
    }
  }
}

String BT_SERIAL::parse()
{
  uint64_t startTimeOut = millis();
  String receivedData;
  int msgSize = 0;
  // Read RX buffer into String
  while (serial.available() != 0)
  {
    receivedData += (char)serial.read();
    msgSize++;
    // Check for timeout condition
    if ((millis() - startTimeOut) >= 5000)
    {
      log_w("Bluetooth received Data timeout");
      break;
    }
  }
  serial.flush();
  Serial.println("Received message " + receivedData + " over Bluetooth");

  return receivedData;
}


      
  // Check if Data over SerialBT has arrived
