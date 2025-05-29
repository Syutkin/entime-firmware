#include "settings.h"

#define MODULE_TYPE "entime"

Preferences preferences;
Settings settings;

String fromJson(String json);
String toJson();

String fromJson(String json)
{
    String result;
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, json);
    // Test if parsing succeeds.
    if (error)
    {
        log_e("deserializeJson() failed: %s", error);
        return result;
    }
    if (doc.overflowed())
    {
        log_e("deserializeJson(): no enough memory to store the entire document");
        return result;
    }
    
    if (doc["Read"].is<bool>())
    {
        bool Read = doc["Read"];
        if (Read)
        { // read settings
            log_d("Read settings");
            result = toJson();
            // JSON to Serial
            // serializeJson(docAnswer, SerialBT);
        }
        else
        {
            // write settings
            log_d("Write settings");
            if (doc["Bluetooth"].is<JsonObject>())
            {
                log_d("Write Bluetooth settings");
                JsonObject jBluetooth = doc["Bluetooth"];
                S_Bluetooth s_bluetooth;
                preferences.begin("Bluetooth", false);
                if (jBluetooth["active"].is<bool>())
                {
                    s_bluetooth.active = jBluetooth["active"];
                    if (s_bluetooth.active != settings.Bluetooth.active)
                    {
                        preferences.putBool("active", s_bluetooth.active);
                        // settings.Bluetooth.active = preferences.getBool("active");
                        // if (settings.Bluetooth.active)
                        if (s_bluetooth.active)
                            log_i("Bluetooth enabled");
                        else
                            log_i("Bluetooth disabled");
                    }
                }
                if (jBluetooth["number"].is<uint8_t>())
                {
                    s_bluetooth.number = jBluetooth["number"];
                    if (s_bluetooth.number != settings.Bluetooth.number)
                    {
                        preferences.putUShort("number", s_bluetooth.number);
                        // settings.Bluetooth.number = preferences.getUShort("number");
                        // log_i("Bluetooth module number set to %d", settings.Bluetooth.number);
                        log_i("Bluetooth module number set to %d", s_bluetooth.number);
                    }
                }
                preferences.end();
            }
            if (doc["Time"].is<JsonObject>())
            {
                log_d("Write Time settings");
                JsonObject jTime = doc["Time"];
                S_Time s_Time;
                preferences.begin("Time", false);
                if (jTime["timezone"].is<int8_t>())
                {
                    s_Time.timezone = jTime["timezone"];
                    if (s_Time.timezone != settings.Time.timezone)
                    {
                        preferences.putInt("timezone", s_Time.timezone);
                        log_i("Timezone set to %d", s_Time.timezone);
                    }
                }
                preferences.end();
            }
            if (doc["LoRa"].is<JsonObject>())
            {
                log_d("Write LoRa settings");
                JsonObject jLoRa = doc["LoRa"];
                S_LoRa s_lora;
                preferences.begin("LoRa", false);
                if (jLoRa["active"].is<bool>())
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
                if (jLoRa["frequency"].is<uint32_t>())
                {
                    s_lora.frequency = jLoRa["frequency"];
                    if (s_lora.frequency != settings.LoRa.frequency)
                    {
                        preferences.putUInt("frequency", s_lora.frequency);
                        // settings.LoRa.frequency = preferences.getUInt("frequency");
                        //  log_i("LoRa frequency set to %d Hz", settings.LoRa.frequency);
                        log_i("LoRa frequency set to %d Hz", s_lora.frequency);
                    }
                }
                if (jLoRa["txPower"].is<uint8_t>())
                {
                    s_lora.txPower = jLoRa["txPower"];
                    if (s_lora.txPower != settings.LoRa.txPower)
                    {
                        preferences.putUShort("txPower", s_lora.txPower);
                        // settings.LoRa.txPower = preferences.getUShort("txPower");
                        //  log_i("LoRa txPower set to %d", settings.LoRa.txPower);
                        log_i("LoRa txPower set to %d", s_lora.txPower);
                    }
                }
                if (jLoRa["spreadingFactor"].is<uint8_t>())
                {
                    s_lora.spreadingFactor = jLoRa["spreadingFactor"];
                    if (s_lora.spreadingFactor != settings.LoRa.spreadingFactor)
                    {
                        preferences.putUShort("spreadingFactor", s_lora.spreadingFactor);
                        // settings.LoRa.spreadingFactor = preferences.getUShort("spreadingFactor");
                        //  log_i("LoRa spreading factor set to %d", settings.LoRa.spreadingFactor);
                        log_i("LoRa spreading factor set to %d", s_lora.spreadingFactor);
                    }
                }
                if (jLoRa["signalBandwidth"].is<uint32_t>())
                {
                    s_lora.signalBandwidth = jLoRa["signalBandwidth"];
                    if (s_lora.signalBandwidth != settings.LoRa.signalBandwidth)
                    {
                        preferences.putUInt("signalBandwidth", s_lora.signalBandwidth);
                        // settings.LoRa.signalBandwidth = preferences.getUInt("signalBandwidth");
                        //  log_i("LoRa signal bandwidth set to %d", settings.LoRa.signalBandwidth);
                        log_i("LoRa signal bandwidth set to %d", s_lora.signalBandwidth);
                    }
                }
                if (jLoRa["codingRateDenom"].is<uint8_t>())
                {
                    s_lora.codingRateDenom = jLoRa["codingRateDenom"];
                    if (s_lora.codingRateDenom != settings.LoRa.codingRateDenom)
                    {
                        preferences.putUShort("codingRateDenom", s_lora.codingRateDenom);
                        // settings.LoRa.codingRateDenom = preferences.getUShort("codingRateDenom");
                        //  log_i("LoRa coding rate denominator set to %d", settings.LoRa.codingRateDenom);
                        log_i("LoRa coding rate denominator set to %d", s_lora.codingRateDenom);
                    }
                }
                if (jLoRa["preambleLength"].is<uint16_t>())
                {
                    s_lora.preambleLength = jLoRa["preambleLength"];
                    if (s_lora.preambleLength != settings.LoRa.preambleLength)
                    {
                        preferences.putUShort("preambleLength", s_lora.preambleLength);
                        // settings.LoRa.preambleLength = preferences.getUShort("preambleLength");
                        //  log_i("LoRa preamble length set to %d", settings.LoRa.preambleLength);
                        log_i("LoRa preamble length set to %d", s_lora.preambleLength);
                    }
                }
                if (jLoRa["syncWord"].is<byte>())
                {
                    s_lora.syncWord = jLoRa["syncWord"];
                    if (s_lora.syncWord != settings.LoRa.syncWord)
                    {
                        preferences.putUChar("syncWord", s_lora.syncWord);
                        // settings.LoRa.syncWord = preferences.getUChar("syncWord");
                        //  log_i("LoRa sync word set to %d", settings.LoRa.syncWord);
                        log_i("LoRa sync word set to %d", s_lora.syncWord);
                    }
                }
                if (jLoRa["crc"].is<bool>())
                {
                    s_lora.crc = jLoRa["crc"];
                    if (s_lora.crc != settings.LoRa.crc)
                    {
                        preferences.putBool("crc", s_lora.crc);
                        // settings.LoRa.crc = preferences.getBool("crc");
                        //  if (settings.LoRa.crc)
                        if (s_lora.crc)
                            log_i("LoRa crc enabled");
                        else
                            log_i("LoRa crc disabled");
                    }
                }
                preferences.end();
            }
            if (doc["WiFi"].is<JsonObject>())
            {
                log_d("Write WiFi settings");
                JsonObject jWiFi = doc["WiFi"];
                S_WiFi s_wifi;
                preferences.begin("WiFi", false);
                if (jWiFi["active"].is<bool>())
                {
                    s_wifi.active = jWiFi["active"];
                    if (s_wifi.active != settings.WiFi.active)
                    {
                        preferences.putBool("active", s_wifi.active);
                        // settings.WiFi.active = preferences.getBool("active");
                        //  if (settings.WiFi.active)
                        if (s_wifi.active)
                            log_i("WiFi enabled");
                        else
                            log_i("WiFi disabled");
                    }
                }
                if (jWiFi["ssid"].is<String>())
                {
                    s_wifi.ssid = jWiFi["ssid"].as<String>();
                    if (s_wifi.ssid != settings.WiFi.ssid)
                    {
                        preferences.putString("ssid", s_wifi.ssid);
                        // settings.WiFi.ssid = preferences.getString("ssid");
                        //  if (settings.WiFi.ssid == "")
                        if (s_wifi.ssid == "")
                            log_i("WiFi ssid cleared");
                        else
                            log_i("WiFi ssid set to %s", settings.WiFi.ssid);
                    }
                }
                if (jWiFi["passwd"].is<String>())
                {
                    if (s_wifi.passwd != settings.WiFi.passwd)
                    {
                        s_wifi.passwd = jWiFi["passwd"].as<String>();
                        preferences.putString("passwd", s_wifi.passwd);
                        // settings.WiFi.passwd = preferences.getString("passwd");
                        log_i("WiFi new password set");
                    }
                }
                preferences.end();
            }
            if (doc["TFT"].is<JsonObject>())
            {
                log_d("Write TFT settings");
                JsonObject jTFT = doc["TFT"];
                S_TFT s_tft;
                preferences.begin("TFT", false);
                if (jTFT["active"].is<bool>())
                {
                    s_tft.active = jTFT["active"];
                    if (s_tft.active != settings.TFT.active)
                    {
                        preferences.putBool("active", s_tft.active);
                        // settings.TFT.active = preferences.getBool("active");
                        //  if (settings.TFT.active)
                        if (s_tft.active)
                            log_i("TFT enabled");
                        else
                            log_i("TFT disabled");
                    }
                }
                if (jTFT["timeout"].is<bool>())
                {
                    s_tft.timeout = jTFT["timeout"];
                    if (s_tft.timeout != settings.TFT.timeout)
                    {
                        preferences.putBool("timeout", s_tft.timeout);
                        // settings.TFT.timeout = preferences.getBool("timeout");
                        //  if (settings.TFT.timeout)
                        if (s_tft.timeout)
                            log_i("TFT off at timeout enabled");
                        else
                            log_i("TFT off at timeout disabled");
                    }
                }
                if (jTFT["timeoutDuration"].is<uint16_t>())
                {
                    s_tft.timeoutDuration = jTFT["timeoutDuration"];
                    if (s_tft.timeoutDuration != settings.TFT.timeoutDuration)
                    {
                        preferences.putUShort("timeoutDuration", s_tft.timeoutDuration);
                        // settings.TFT.timeoutDuration = preferences.getUShort("timeoutDuration");
                        //  log_i("TFT switch off timeout duration set to %d sec", settings.TFT.timeoutDuration);
                        log_i("TFT switch off timeout duration set to %d sec", s_tft.timeoutDuration);
                    }
                }
                if (jTFT["turnOnAtEvent"].is<bool>())
                {
                    s_tft.turnOnAtEvent = jTFT["turnOnAtEvent"];
                    if (s_tft.turnOnAtEvent != settings.TFT.turnOnAtEvent)
                    {
                        preferences.putBool("turnOnAtEvent", s_tft.turnOnAtEvent);
                        // settings.TFT.turnOnAtEvent = preferences.getBool("turnOnAtEvent");
                        //  if (settings.TFT.turnOnAtEvent)
                        if (s_tft.turnOnAtEvent)
                            log_i("TFT turn on at event enabled");
                        else
                            log_i("TFT turn on at event disabled");
                    }
                }
                preferences.end();
            }
            if (doc["Buzzer"].is<JsonObject>())
            {
                log_d("Write Buzzer settings");
                JsonObject jBuzzer = doc["Buzzer"];
                S_Buzzer s_buzzer;
                preferences.begin("Buzzer", false);
                if (jBuzzer["active"].is<bool>())
                {
                    s_buzzer.active = jBuzzer["active"];
                    if (s_buzzer.active != settings.Buzzer.active)
                    {
                        preferences.putBool("active", s_buzzer.active);
                        // settings.Buzzer.active = preferences.getBool("active");
                        //  if (settings.Buzzer.active)
                        if (s_buzzer.active)
                            log_i("Buzzer activated");
                        else
                            log_i("Buzzer deactivated");
                    }
                }
                if (jBuzzer["shortFrequency"].is<uint16_t>())
                {
                    s_buzzer.shortFrequency = jBuzzer["shortFrequency"];
                    if (s_buzzer.shortFrequency != settings.Buzzer.shortFrequency)
                    {
                        preferences.putUShort("shortFrequency", s_buzzer.shortFrequency);
                        // settings.Buzzer.shortFrequency = preferences.getUShort("shortFrequency");
                        //  log_i("Buzzer short frequency set to %d Hz", settings.Buzzer.shortFrequency);
                        log_i("Buzzer short frequency set to %d Hz", s_buzzer.shortFrequency);
                    }
                }
                if (jBuzzer["longFrequency"].is<uint16_t>())
                {
                    s_buzzer.longFrequency = jBuzzer["longFrequency"];
                    if (s_buzzer.longFrequency != settings.Buzzer.longFrequency)
                    {
                        preferences.putUShort("longFrequency", s_buzzer.longFrequency);
                        // settings.Buzzer.longFrequency = preferences.getUShort("longFrequency");
                        //  log_i("Buzzer long frequency set to %d Hz", settings.Buzzer.longFrequency);
                        log_i("Buzzer long frequency set to %d Hz", s_buzzer.longFrequency);
                    }
                }
                preferences.end();
            }
            if (doc["VCC"].is<JsonObject>())
            {
                log_d("Write VCC settings");
                JsonObject jVCC = doc["VCC"];
                S_VCC s_vcc;
                preferences.begin("VCC", false);
                if (jVCC["r1"].is<uint16_t>())
                {
                    s_vcc.r1 = jVCC["r1"];
                    if (s_vcc.r1 != settings.VCC.r1)
                    {
                        preferences.putUShort("r1", s_vcc.r1);
                        // settings.VCC.r1 = preferences.getUShort("r1");
                        //  log_i("VCC R1 set to %d Om", settings.VCC.r1);
                        log_i("VCC R1 set to %d Om", s_vcc.r1);
                    }
                }
                if (jVCC["r2"].is<uint16_t>())
                {
                    s_vcc.r2 = jVCC["r2"];
                    if (s_vcc.r2 != settings.VCC.r2)
                    {
                        preferences.putUShort("r2", s_vcc.r2);
                        // settings.VCC.r2 = preferences.getUShort("r2");
                        //  log_i("VCC R2 set to %d Om", settings.VCC.r2);
                        log_i("VCC R2 set to %d Om", s_vcc.r2);
                    }
                }
                if (jVCC["vbat"].is<uint16_t>())
                {
                    // ToDo: вернуть как было
                    log_i("vbat settings not implemented");
                    // s_vcc.vbat = jVCC["vbat"];
                    // if (s_vcc.vbat != 0)
                    // {
                    //     uint16_t r1 = (float)(v_divider * s_vcc.vbat / getVcc()) * settings.VCC.r2 - settings.VCC.r2;
                    //     preferences.putUShort("r1", r1);
                    //     log_i("Modifying VCC R1 to %d Om", r1);
                    // }
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

String toJson()
{
    String result;
    JsonDocument docAnswer;

    docAnswer["Type"] = MODULE_TYPE;

    JsonObject Bluetooth = docAnswer["Bluetooth"].to<JsonObject>();
    Bluetooth["active"] = settings.Bluetooth.active;
    Bluetooth["name"] = settings.Bluetooth.name;
    Bluetooth["number"] = settings.Bluetooth.number;

    JsonObject Time = docAnswer["Time"].to<JsonObject>();
    Time["timezone"] = settings.Time.timezone;

    JsonObject LoRa = docAnswer["LoRa"].to<JsonObject>();
    LoRa["active"] = settings.LoRa.active;
    LoRa["frequency"] = settings.LoRa.frequency;
    LoRa["txPower"] = settings.LoRa.txPower;
    LoRa["spreadingFactor"] = settings.LoRa.spreadingFactor;
    LoRa["signalBandwidth"] = settings.LoRa.signalBandwidth;
    LoRa["codingRateDenom"] = settings.LoRa.codingRateDenom;
    LoRa["preambleLength"] = settings.LoRa.preambleLength;
    LoRa["syncWord"] = settings.LoRa.syncWord;
    LoRa["crc"] = settings.LoRa.crc;

    JsonObject WiFi = docAnswer["WiFi"].to<JsonObject>();
    WiFi["active"] = settings.WiFi.active;
    WiFi["ssid"] = settings.WiFi.ssid;
    // WiFi["passwd"] = settings.WiFi.passwd;
    // Do not transmit password
    WiFi["passwd"] = "";

    JsonObject TFT = docAnswer["TFT"].to<JsonObject>();
    TFT["active"] = settings.TFT.active;
    TFT["timeout"] = settings.TFT.timeout;
    TFT["timeoutDuration"] = settings.TFT.timeoutDuration;
    TFT["turnOnAtEvent"] = settings.TFT.turnOnAtEvent;

    JsonObject Buzzer = docAnswer["Buzzer"].to<JsonObject>();
    Buzzer["active"] = settings.Buzzer.active;
    Buzzer["shortFrequency"] = settings.Buzzer.shortFrequency;
    Buzzer["longFrequency"] = settings.Buzzer.longFrequency;

    JsonObject VCC = docAnswer["VCC"].to<JsonObject>();
    VCC["r1"] = settings.VCC.r1;
    VCC["r2"] = settings.VCC.r2;

    serializeJson(docAnswer, result);
    log_d("Settings: %s", result.c_str());
    return result;
}
