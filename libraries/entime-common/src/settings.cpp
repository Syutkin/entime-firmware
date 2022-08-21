#include "settings.h"

//const size_t capacity = 4 * JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(9);
const size_t json_capacity = 1024;

Preferences preferences;
Settings settings;
time_t last_rtc_syncdate;

void loadSettings(String MODULE_NAME)
{
  Serial << "Settings: Loading..." << endl;

  preferences.begin("Bluetooth", false);
  settings.Bluetooth.active = preferences.getBool("active", true);
  settings.Bluetooth.name = preferences.getString("name", "FR-" + MODULE_NAME + "-"); //имя Bluetooth модуля
  settings.Bluetooth.number = preferences.getUShort("number", 1);                     //номер в имени Bluetooth модуля
  preferences.end();

  preferences.begin("LoRa", false);
  settings.LoRa.active = preferences.getBool("active", false);
  settings.LoRa.frequency = preferences.getInt("frequency", 433E6);
  settings.LoRa.txPower = preferences.getUShort("txPower", 20);
  settings.LoRa.spreadingFactor = preferences.getUShort("spreadingFactor", 12);
  settings.LoRa.signalBandwidth = preferences.getInt("signalBandwidth", 125E3);
  settings.LoRa.codingRateDenom = preferences.getUShort("codingRateDenom", 5);
  settings.LoRa.preambleLength = preferences.getUShort("preambleLength", 8);
  settings.LoRa.syncWord = preferences.getChar("syncWord", 0x12);
  settings.LoRa.crc = preferences.getBool("crc", false);
  preferences.end();

  preferences.begin("WiFi", false);
  settings.WiFi.active = preferences.getBool("active", false);
  settings.WiFi.ssid = preferences.getString("ssid", "");
  settings.WiFi.passwd = preferences.getString("passwd", "");
  preferences.end();

  preferences.begin("TFT", false);
  settings.TFT.active = preferences.getBool("active", true);
  settings.TFT.timeout = preferences.getBool("timeout", false);
  settings.TFT.timeoutDuration = preferences.getUShort("timeoutDuration", 5);
  settings.TFT.turnOnAtEvent = preferences.getBool("turnOnAtEvent", false);
  preferences.end();

  preferences.begin("Buzzer", false);
  settings.Buzzer.active = preferences.getBool("active", true);
  settings.Buzzer.shortFrequency = preferences.getUShort("shortFrequency", 659);
  settings.Buzzer.longFrequency = preferences.getUShort("longFrequency", 659);
  preferences.end();

  preferences.begin("VCC", false);
  settings.VCC.r1 = preferences.getUShort("r1", 4700);
  settings.VCC.r2 = preferences.getUShort("r2", 1000);
  //settings.VCC.vbat = preferences.getUShort("vbat");
  preferences.end();

  preferences.begin("Misc", false);
  last_rtc_syncdate = preferences.getInt("syncDate", 0);
  preferences.end();

  Serial << "Settings: Loaded" << endl;
}

String jsonToSettings(String json, String MODULE_TYPE)
{
  String result;
  DynamicJsonDocument doc(json_capacity);

  //JsonObject obj = doc.to<JsonObject>();
  // Deserialize the JSON document
  // DeserializationError error = deserializeJson(doc, receivedData);
  DeserializationError error = deserializeJson(doc, json);
  // Test if parsing succeeds.
  if (error)
  {
    log_i("deserializeJson() failed: %s", error.c_str());
    return result;
  }
  if (doc.containsKey("Read"))
  {
    bool Read = doc["Read"];
    if (Read)
    { //read settings
      log_d("Read settings");
      result = settingsToJson(MODULE_TYPE);
      // JSON to Serial
      // serializeJson(docAnswer, SerialBT);
    }
    else
    {
      //write settings
      log_d("Write settings");
      if (doc.containsKey("Bluetooth"))
      {
        log_d("Write Bluetooth settings");
        JsonObject jBluetooth = doc["Bluetooth"];
        S_Bluetooth s_bluetooth;
        preferences.begin("Bluetooth", false);
        if (jBluetooth.containsKey("active"))
        {
          s_bluetooth.active = jBluetooth["active"];
          if (s_bluetooth.active != settings.Bluetooth.active)
          {
            preferences.putBool("active", s_bluetooth.active);
            //settings.Bluetooth.active = preferences.getBool("active");
            //if (settings.Bluetooth.active)
            if (s_bluetooth.active)
              log_i("Bluetooth enabled");
            else
              log_i("Bluetooth disabled");
          }
        }
        if (jBluetooth.containsKey("number"))
        {
          s_bluetooth.number = jBluetooth["number"];
          if (s_bluetooth.number != settings.Bluetooth.number)
          {
            preferences.putUShort("number", s_bluetooth.number);
            //settings.Bluetooth.number = preferences.getUShort("number");
            //log_i("Bluetooth module number set to %d", settings.Bluetooth.number);
            log_i("Bluetooth module number set to %d", s_bluetooth.number);
          }
        }
        preferences.end();
      }
      if (doc.containsKey("LoRa"))
      {
        log_d("Write LoRa settings");
        JsonObject jLoRa = doc["LoRa"];
        S_LoRa s_lora;
        preferences.begin("LoRa", false);
        if (jLoRa.containsKey("active"))
        {
          s_lora.active = jLoRa["active"];
          if (s_lora.active != settings.LoRa.active)
          {
            preferences.putBool("active", s_lora.active);
            // settings.LoRa.active = preferences.getBool("active");
            // if (settings.LoRa.active)
            if (s_lora.active)
              log_i("LoRa enabled");
            else
              log_i("LoRa disabled");
          }
        }
        if (jLoRa.containsKey("frequency"))
        {
          s_lora.frequency = jLoRa["frequency"];
          if (s_lora.frequency != settings.LoRa.frequency)
          {
            preferences.putUInt("frequency", s_lora.frequency);
            //settings.LoRa.frequency = preferences.getUInt("frequency");
            // log_i("LoRa frequency set to %d Hz", settings.LoRa.frequency);
            log_i("LoRa frequency set to %d Hz", s_lora.frequency);
          }
        }
        if (jLoRa.containsKey("txPower"))
        {
          s_lora.txPower = jLoRa["txPower"];
          if (s_lora.txPower != settings.LoRa.txPower)
          {
            preferences.putUShort("txPower", s_lora.txPower);
            //settings.LoRa.txPower = preferences.getUShort("txPower");
            // log_i("LoRa txPower set to %d", settings.LoRa.txPower);
            log_i("LoRa txPower set to %d", s_lora.txPower);
          }
        }
        if (jLoRa.containsKey("spreadingFactor"))
        {
          s_lora.spreadingFactor = jLoRa["spreadingFactor"];
          if (s_lora.spreadingFactor != settings.LoRa.spreadingFactor)
          {
            preferences.putUShort("spreadingFactor", s_lora.spreadingFactor);
            //settings.LoRa.spreadingFactor = preferences.getUShort("spreadingFactor");
            // log_i("LoRa spreading factor set to %d", settings.LoRa.spreadingFactor);
            log_i("LoRa spreading factor set to %d", s_lora.spreadingFactor);
          }
        }
        if (jLoRa.containsKey("signalBandwidth"))
        {
          s_lora.signalBandwidth = jLoRa["signalBandwidth"];
          if (s_lora.signalBandwidth != settings.LoRa.signalBandwidth)
          {
            preferences.putUInt("signalBandwidth", s_lora.signalBandwidth);
            //settings.LoRa.signalBandwidth = preferences.getUInt("signalBandwidth");
            // log_i("LoRa signal bandwidth set to %d", settings.LoRa.signalBandwidth);
            log_i("LoRa signal bandwidth set to %d", s_lora.signalBandwidth);
          }
        }
        if (jLoRa.containsKey("codingRateDenom"))
        {
          s_lora.codingRateDenom = jLoRa["codingRateDenom"];
          if (s_lora.codingRateDenom != settings.LoRa.codingRateDenom)
          {
            preferences.putUShort("codingRateDenom", s_lora.codingRateDenom);
            //settings.LoRa.codingRateDenom = preferences.getUShort("codingRateDenom");
            // log_i("LoRa coding rate denominator set to %d", settings.LoRa.codingRateDenom);
            log_i("LoRa coding rate denominator set to %d", s_lora.codingRateDenom);
          }
        }
        if (jLoRa.containsKey("preambleLength"))
        {
          s_lora.preambleLength = jLoRa["preambleLength"];
          if (s_lora.preambleLength != settings.LoRa.preambleLength)
          {
            preferences.putUShort("preambleLength", s_lora.preambleLength);
            //settings.LoRa.preambleLength = preferences.getUShort("preambleLength");
            // log_i("LoRa preamble length set to %d", settings.LoRa.preambleLength);
            log_i("LoRa preamble length set to %d", s_lora.preambleLength);
          }
        }
        if (jLoRa.containsKey("syncWord"))
        {
          s_lora.syncWord = jLoRa["syncWord"];
          if (s_lora.syncWord != settings.LoRa.syncWord)
          {
            preferences.putUChar("syncWord", s_lora.syncWord);
            //settings.LoRa.syncWord = preferences.getUChar("syncWord");
            // log_i("LoRa sync word set to %d", settings.LoRa.syncWord);
            log_i("LoRa sync word set to %d", s_lora.syncWord);
          }
        }
        if (jLoRa.containsKey("crc"))
        {
          s_lora.crc = jLoRa["crc"];
          if (s_lora.crc != settings.LoRa.crc)
          {
            preferences.putBool("crc", s_lora.crc);
            //settings.LoRa.crc = preferences.getBool("crc");
            // if (settings.LoRa.crc)
            if (s_lora.crc)
              log_i("LoRa crc enabled");
            else
              log_i("LoRa crc disabled");
          }
        }
        preferences.end();
      }
      if (doc.containsKey("WiFi"))
      {
        log_d("Write WiFi settings");
        JsonObject jWiFi = doc["WiFi"];
        S_WiFi s_wifi;
        preferences.begin("WiFi", false);
        if (jWiFi.containsKey("active"))
        {
          s_wifi.active = jWiFi["active"];
          if (s_wifi.active != settings.WiFi.active)
          {
            preferences.putBool("active", s_wifi.active);
            //settings.WiFi.active = preferences.getBool("active");
            // if (settings.WiFi.active)
            if (s_wifi.active)
              log_i("WiFi enabled");
            else
              log_i("WiFi disabled");
          }
        }
        if (jWiFi.containsKey("ssid"))
        {
          s_wifi.ssid = jWiFi["ssid"].as<String>();
          if (s_wifi.ssid != settings.WiFi.ssid)
          {
            preferences.putString("ssid", s_wifi.ssid);
            //settings.WiFi.ssid = preferences.getString("ssid");
            // if (settings.WiFi.ssid == "")
            if (s_wifi.ssid == "")
              log_i("WiFi ssid cleared");
            else
              log_i("WiFi ssid set to %s", settings.WiFi.ssid);
          }
        }
        if (jWiFi.containsKey("passwd"))
        {
          if (s_wifi.passwd != settings.WiFi.passwd)
          {
            s_wifi.passwd = jWiFi["passwd"].as<String>();
            preferences.putString("passwd", s_wifi.passwd);
            //settings.WiFi.passwd = preferences.getString("passwd");
            log_i("WiFi new password set");
          }
        }
        preferences.end();
      }
      if (doc.containsKey("TFT"))
      {
        log_d("Write TFT settings");
        JsonObject jTFT = doc["TFT"];
        S_TFT s_tft;
        preferences.begin("TFT", false);
        if (jTFT.containsKey("active"))
        {
          s_tft.active = jTFT["active"];
          if (s_tft.active != settings.TFT.active)
          {
            preferences.putBool("active", s_tft.active);
            //settings.TFT.active = preferences.getBool("active");
            // if (settings.TFT.active)
            if (s_tft.active)
              log_i("TFT enabled");
            else
              log_i("TFT disabled");
          }
        }
        if (jTFT.containsKey("timeout"))
        {
          s_tft.timeout = jTFT["timeout"];
          if (s_tft.timeout != settings.TFT.timeout)
          {
            preferences.putBool("timeout", s_tft.timeout);
            //settings.TFT.timeout = preferences.getBool("timeout");
            // if (settings.TFT.timeout)
            if (s_tft.timeout)
              log_i("TFT off at timeout enabled");
            else
              log_i("TFT off at timeout disabled");
          }
        }
        if (jTFT.containsKey("timeoutDuration"))
        {
          s_tft.timeoutDuration = jTFT["timeoutDuration"];
          if (s_tft.timeoutDuration != settings.TFT.timeoutDuration)
          {
            preferences.putUShort("timeoutDuration", s_tft.timeoutDuration);
            //settings.TFT.timeoutDuration = preferences.getUShort("timeoutDuration");
            // log_i("TFT switch off timeout duration set to %d sec", settings.TFT.timeoutDuration);
            log_i("TFT switch off timeout duration set to %d sec", s_tft.timeoutDuration);
          }
        }
        if (jTFT.containsKey("turnOnAtEvent"))
        {
          s_tft.turnOnAtEvent = jTFT["turnOnAtEvent"];
          if (s_tft.turnOnAtEvent != settings.TFT.turnOnAtEvent)
          {
            preferences.putBool("turnOnAtEvent", s_tft.turnOnAtEvent);
            //settings.TFT.turnOnAtEvent = preferences.getBool("turnOnAtEvent");
            // if (settings.TFT.turnOnAtEvent)
            if (s_tft.turnOnAtEvent)
              log_i("TFT turn on at event enabled");
            else
              log_i("TFT turn on at event disabled");
          }
        }
        preferences.end();
      }
      if (doc.containsKey("Buzzer"))
      {
        log_d("Write Buzzer settings");
        JsonObject jBuzzer = doc["Buzzer"];
        S_Buzzer s_buzzer;
        preferences.begin("Buzzer", false);
        if (jBuzzer.containsKey("active"))
        {
          s_buzzer.active = jBuzzer["active"];
          if (s_buzzer.active != settings.Buzzer.active)
          {
            preferences.putBool("active", s_buzzer.active);
            //settings.Buzzer.active = preferences.getBool("active");
            // if (settings.Buzzer.active)
            if (s_buzzer.active)
              log_i("Buzzer activated");
            else
              log_i("Buzzer deactivated");
          }
        }
        if (jBuzzer.containsKey("shortFrequency"))
        {
          s_buzzer.shortFrequency = jBuzzer["shortFrequency"];
          if (s_buzzer.shortFrequency != settings.Buzzer.shortFrequency)
          {
            preferences.putUShort("shortFrequency", s_buzzer.shortFrequency);
            //settings.Buzzer.shortFrequency = preferences.getUShort("shortFrequency");
            // log_i("Buzzer short frequency set to %d Hz", settings.Buzzer.shortFrequency);
            log_i("Buzzer short frequency set to %d Hz", s_buzzer.shortFrequency);
          }
        }
        if (jBuzzer.containsKey("longFrequency"))
        {
          s_buzzer.longFrequency = jBuzzer["longFrequency"];
          if (s_buzzer.longFrequency != settings.Buzzer.longFrequency)
          {
            preferences.putUShort("longFrequency", s_buzzer.longFrequency);
            //settings.Buzzer.longFrequency = preferences.getUShort("longFrequency");
            // log_i("Buzzer long frequency set to %d Hz", settings.Buzzer.longFrequency);
            log_i("Buzzer long frequency set to %d Hz", s_buzzer.longFrequency);
          }
        }
        preferences.end();
      }
      if (doc.containsKey("VCC"))
      {
        log_d("Write VCC settings");
        JsonObject jVCC = doc["VCC"];
        S_VCC s_vcc;
        preferences.begin("VCC", false);
        if (jVCC.containsKey("r1"))
        {
          s_vcc.r1 = jVCC["r1"];
          if (s_vcc.r1 != settings.VCC.r1)
          {
            preferences.putUShort("r1", s_vcc.r1);
            //settings.VCC.r1 = preferences.getUShort("r1");
            // log_i("VCC R1 set to %d Om", settings.VCC.r1);
            log_i("VCC R1 set to %d Om", s_vcc.r1);
          }
        }
        if (jVCC.containsKey("r2"))
        {
          s_vcc.r2 = jVCC["r2"];
          if (s_vcc.r2 != settings.VCC.r2)
          {
            preferences.putUShort("r2", s_vcc.r2);
            //settings.VCC.r2 = preferences.getUShort("r2");
            // log_i("VCC R2 set to %d Om", settings.VCC.r2);
            log_i("VCC R2 set to %d Om", s_vcc.r2);
          }
        }
        if (jVCC.containsKey("vbat"))
        {
          s_vcc.vbat = jVCC["vbat"];
          if (s_vcc.vbat != 0)
          {
            uint16_t r1 = (float)(v_divider * s_vcc.vbat / getVcc()) * settings.VCC.r2 - settings.VCC.r2;
            preferences.putUShort("r1", r1);
            //settings.VCC.r1 = preferences.getUShort("r1");
            // log_i("Modifying VCC R1 to %d Om", s_vcc.r1);
            log_i("Modifying VCC R1 to %d Om", r1);
            // Update current settings
            // v_divider = (float)(settings.VCC.r1 + settings.VCC.r2) / (float)settings.VCC.r2;
            // printVcc();
          }
        }
        preferences.end();
      }
    }
  }
  else
  {
    log_i("Unknown JSON command");
  }
  return result;
}

String settingsToJson(String MODULE_TYPE)
{
  String result;
  DynamicJsonDocument docAnswer(json_capacity);

  docAnswer["Type"] = MODULE_TYPE;

  JsonObject Bluetooth = docAnswer.createNestedObject("Bluetooth");
  Bluetooth["active"] = settings.Bluetooth.active;
  Bluetooth["name"] = settings.Bluetooth.name;
  Bluetooth["number"] = settings.Bluetooth.number;

  JsonObject LoRa = docAnswer.createNestedObject("LoRa");
  LoRa["active"] = settings.LoRa.active;
  LoRa["frequency"] = settings.LoRa.frequency;
  LoRa["txPower"] = settings.LoRa.txPower;
  LoRa["spreadingFactor"] = settings.LoRa.spreadingFactor;
  LoRa["signalBandwidth"] = settings.LoRa.signalBandwidth;
  LoRa["codingRateDenom"] = settings.LoRa.codingRateDenom;
  LoRa["preambleLength"] = settings.LoRa.preambleLength;
  LoRa["syncWord"] = settings.LoRa.syncWord;
  LoRa["crc"] = settings.LoRa.crc;

  JsonObject WiFi = docAnswer.createNestedObject("WiFi");
  WiFi["active"] = settings.WiFi.active;
  WiFi["ssid"] = settings.WiFi.ssid;
  // WiFi["passwd"] = settings.WiFi.passwd;
  // Do not transmit password
  WiFi["passwd"] = "";

  JsonObject TFT = docAnswer.createNestedObject("TFT");
  TFT["active"] = settings.TFT.active;
  TFT["timeout"] = settings.TFT.timeout;
  TFT["timeoutDuration"] = settings.TFT.timeoutDuration;
  TFT["turnOnAtEvent"] = settings.TFT.turnOnAtEvent;

  JsonObject Buzzer = docAnswer.createNestedObject("Buzzer");
  Buzzer["active"] = settings.Buzzer.active;
  Buzzer["shortFrequency"] = settings.Buzzer.shortFrequency;
  Buzzer["longFrequency"] = settings.Buzzer.longFrequency;

  JsonObject VCC = docAnswer.createNestedObject("VCC");
  VCC["r1"] = settings.VCC.r1;
  VCC["r2"] = settings.VCC.r2;

  serializeJson(docAnswer, result);
  log_d("Settings: %s", result.c_str());
  return result;
}