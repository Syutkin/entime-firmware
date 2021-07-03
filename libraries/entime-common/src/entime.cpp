#include "entime.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

//const size_t capacity = 4 * JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(9);
const size_t json_capacity = 1024;

const uint8_t EVENT_PIN(12);   // номер входа, подключенный к кнопке
const uint8_t RTC_1HZ_PIN(13); // RTC provides a 1Hz interrupt signal on this pin

// For non-AVR boards only. Not needed for AVR boards.
DS3232RTC myRTC(false); // tell constructor not to initialize the I2C bus.

volatile time_t isrUTC;            //ISR's copy of current time in UTC
volatile unsigned long syncmillis; // значение millis() которое было во время последней синхронизации (каждую секунду)

//define not needed for all pins if connected to VSPI; reference for ESP32 physical pins connections to VSPI:
// #define TFT_SCLK 15 //SCL  //SCK - CLK  // SPI `serial port clock - SPI clock input on Display module //hw 15 -> 5
// #define TFT_MOSI 4  //SDA  //SDA - DIN  // MISO - Master In Slave Out - SPI input on Display module   //hw  4 -> 27
#define TFT_CS 17   //CS   //CS - CS    // CS - Chip Select - SPI CS input on Display module
#define TFT_RST 16  //RES  //RST- RST   // Reset (optional, -1 if unused)
#define TFT_DC 2    //DC   //RS - D/C   // Data/Command
//#define TFT_BACKLIGHT 47 // Display backlight pin

// Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_RS, TFT_MOSI, TFT_SCLK, TFT_RES);
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

const uint8_t charge_y = 146; //заряд

BluetoothSerial SerialBT;
Preferences preferences;
Settings settings;

Ticker tickerVcc(printVcc, 60000);

double v_divider;
time_t last_rtc_syncdate;

esp_adc_cal_characteristics_t *adc_chars;
extern const adc_atten_t atten = ADC_ATTEN_DB_6; //6dB ослабление (ADC_ATTEN_DB_6) от 150 до 1750mV
extern const adc_unit_t unit = ADC_UNIT_1;
static const adc_channel_t channel = ADC_CHANNEL_6;

/* положение информации на экране */
const uint8_t clock_y = 0; //часы

String timeToString(time_t time)
{
  char ch[9];
  sprintf(ch, "%.2d:%.2d:%.2d",
          hour(time), minute(time), second(time));
  String str = String(ch);
  return str;
}

String timeToString(time_t time, int16_t millis)
{
  if (millis > 999)
  {               //надо бы половить дигитсы больше 999
    millis = 999; //предположительно, если значение больше 999, то не изменилась секунда события (в противном случае обновились бы syncmillis)
    log_w("Debug: millis > 999");
  }
  char ch[13];
  sprintf(ch, "%.2d:%.2d:%.2d,%.3d",
          hour(time), minute(time), second(time), millis);
  String str = String(ch);
  return str;
}

String digitsToString(int i)
{
  char ch[3];
  sprintf(ch, "%.2d", i);
  String str = String(ch);
  return str;
}

void check_efuse()
{
  //Check TP is burned into eFuse
  if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK)
  {
    Serial.println("eFuse Two Point: Supported");
  }
  else
  {
    Serial.println("eFuse Two Point: NOT supported");
  }
  //Check Vref is burned into eFuse
  if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK)
  {
    Serial.println("eFuse Vref: Supported");
  }
  else
  {
    Serial.println("eFuse Vref: NOT supported");
  }
}

int16_t voltageToCharge(int32_t voltage)
{
  int16_t charge = (voltage - MAX_DISCHARGE) / CHARGE_DIVIDER;
  if (charge > 100)
  {
    charge = 100;
  }
  return charge;
}

void batteryToTFT(uint8_t charge_y_tft, int32_t voltage, int16_t charge)
{
  tft.fillRect(11, charge_y_tft, 24, 14, ST7735_BLACK);
  tft.fillRect(101, charge_y_tft, tft.width(), 14, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, charge_y_tft);
  if (charge > 1)
  {
    tft << "V=" << voltage << "mV Battery:" << charge << "%";
  }
  else if (charge < -100)
  {
    tft << "V=" << voltage << "mV Battery OFF";
  }
  else
  {
    tft << "V=" << voltage << "mV Battery LOW!";
  }
}

void digitalClockTFTinit()
{
  tft.setCursor(0, clock_y); //двоеточия у часов
  tft.setTextSize(2);
  tft.print("00:00:00");
}

void digitalClockTFT(time_t t)
{
  static uint8_t prevSecond;
  static uint8_t prevMinute;
  static uint8_t prevHour;
  // digital clock display of the time
  tft.setTextSize(2); //ширина шрифта 12?
  //    tft.fillRect(0, clock_y, tft.width(), 14, ST7735_BLACK);

  if (hour(t) != prevHour)
  {
    //tft.fillRect(0, clock_y, 23, 14, ST7735_BLACK);
    tft.setCursor(0, clock_y);
    tft.setTextColor(ST7735_BLACK);
    tft.print(digitsToString(prevHour));
    tft.setCursor(0, clock_y);
    tft.setTextColor(ST7735_WHITE);
    tft.print(digitsToString(hour(t)));
    prevHour = hour(t);
  }
  if (minute(t) != prevMinute)
  {
    //tft.fillRect(36, clock_y, 23, 14, ST7735_BLACK);
    tft.setCursor(36, clock_y);
    tft.setTextColor(ST7735_BLACK);
    tft.print(digitsToString(prevMinute));
    tft.setCursor(36, clock_y);
    tft.setTextColor(ST7735_WHITE);
    tft.print(digitsToString(minute(t)));
    prevMinute = minute(t);
  }
  if (second(t) != prevSecond)
  {
    //tft.fillRect(72, clock_y, 23, 14, ST7735_BLACK);
    tft.setCursor(72, clock_y);
    tft.setTextColor(ST7735_BLACK);
    tft.print(digitsToString(prevSecond));
    tft.setCursor(72, clock_y);
    tft.setTextColor(ST7735_WHITE);
    tft.print(digitsToString(second(t)));
    prevSecond = second(t);
  }
}

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

String jsonToSettings(String json)
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
      result = settingsToJson();
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

String settingsToJson()
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

uint32_t getVcc()
{
  uint32_t adc_reading = 0;
  for (int i = 0; i < NO_OF_SAMPLES; i++)
  {
    adc_reading += adc1_get_raw((adc1_channel_t)channel);
  }
  adc_reading /= NO_OF_SAMPLES;
  uint32_t voltage = v_divider * esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
  return voltage;
}

void printVcc()
{
  uint32_t voltage = getVcc();
  int16_t charge = voltageToCharge(voltage);
  batteryToTFT(charge_y, voltage, charge);
  log_d("Voltage: %d, charge: %d", voltage, charge);
}

unsigned long processSyncMessage()
{
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if (Serial.find(TIME_HEADER))
  {
    pctime = Serial.parseInt();
    return pctime;
    if (pctime < DEFAULT_TIME)
    {              // check the value is a valid time (greater than Jan 1 2013)
      pctime = 0L; // return 0 to indicate that the time is not valid
    }
  }
  return pctime;
}

time_t getUTC()
{
  // noInterrupts();
  // time_t utc = isrUTC;
  // interrupts();
  // return utc;
  return isrUTC;
}

time_t getUTCNoInterrupts()
{
  noInterrupts();
  time_t utc = isrUTC;
  interrupts();
  return utc;
}

void setUTC(time_t utc)
{
  noInterrupts();
  isrUTC = utc;
  interrupts();
}

void incrementTime()
{
  ++isrUTC;
  syncmillis = millis();
}

void syncRTCfromSerial()
{
  if (Serial.available())
  {
    time_t t = processSyncMessage();
    if (t != 0)
    {
      myRTC.set(t + 1); // set the RTC and the system time to the received value +1 second
      Serial << "RTC synced to local time" << endl;
      preferences.begin("Misc", false);
      preferences.putInt("syncDate", t + 1);
      preferences.end();
      syncFromRTC();
    }
  }
}

void syncFromRTC()
{
  time_t utc = getUTCNoInterrupts(); // synchronize with RTC
  while (utc == getUTCNoInterrupts())
    ;                // wait for increment to the next second
  utc = myRTC.get(); // get the time from the RTC
  setUTC(utc);       // set our time to the RTC's time
  Serial << "RTC: Module time synced from RTC" << endl;
}

void drawText(const char *text, uint16_t color)
{
  tft.setTextColor(color);
  tft.setTextWrap(true);
  tft.println(text);
  tft.setTextColor(ST7735_WHITE);
}

void setupModule(String MODULE_NAME)
{
  Serial.begin(57600);
  myRTC.begin(); // initialize the I2C bus here.

  pinMode(RTC_1HZ_PIN, INPUT_PULLUP); // enable pullup on interrupt pin (RTC SQW pin is open drain)
  attachInterrupt(digitalPinToInterrupt(RTC_1HZ_PIN), incrementTime, FALLING);
  myRTC.squareWave(SQWAVE_1_HZ); // 1 Hz square wave

  loadSettings(MODULE_NAME);

  // Use this initializer if you're using a 1.8" TFT
  tft.initR(INITR_BLACKTAB); // initialize a ST7735S chip, black tab
  tft.setRotation(2);
  tft.fillScreen(ST7735_BLACK);
  drawText("TFT Initialized", ST7735_WHITE);
  tft << endl;

  Serial << "Module: " << MODULE_NAME << " module ESP32+LoRa" << endl;
  Serial << "Firmware: " << VERSION << endl;
  Serial << "Firmware: Upload at " << F(__DATE__ " " __TIME__) << endl;
  tft << "Module type:" << endl;
  tft << MODULE_NAME << " ESP32+LoRa" << endl;
  tft << "Firmware:" << endl;
  tft << VERSION << endl;
  tft << "Upload at:" << endl;
  tft << F(__DATE__ " " __TIME__) << endl;
  tft << endl;

  delay(2000);

  tft << "Waiting for RTC..." << endl;

  syncFromRTC();
  time_t t = getUTC();
  char buf1[20];
  sprintf(buf1, "%02d:%02d:%02d %02d/%02d/%02d", hour(t), minute(t), second(t), day(t), month(t), year(t));

  tft << "RTC synced: " << endl;
  tft << buf1 << endl;

  if (t - last_rtc_syncdate > 86400 /* секунд в сутках */)
  {
    Serial << "RTC: time update needed" << endl;
    drawText("Time update needed", ST7735_RED);
  }
  tft << endl;

  delay(2000);

  //ToDo: пока не выключать BT из настроек
  // if (settings.Bluetooth.active)
  if (true)
  {
    if (!SerialBT.begin(settings.Bluetooth.name + settings.Bluetooth.number))
    {
      Serial << "Bluetooth: Module " << settings.Bluetooth.name << settings.Bluetooth.number << " failed to start" << endl;
      tft << "Bluetooth:" << endl;
      tft << settings.Bluetooth.name << settings.Bluetooth.number << " failed to start" << endl;
    }
    else
    {
      Serial << "Bluetooth: Module " << settings.Bluetooth.name << settings.Bluetooth.number << " is ready to pair" << endl;
      tft << "Bluetooth:" << endl;
      tft << settings.Bluetooth.name << settings.Bluetooth.number << " is ready" << endl;
    }
  }
  else
  {
    Serial << "Bluetooth off" << endl;
    tft << "Bluetooth off" << endl;
  }

  // настройка LoRa:
  // LoRa.setPins(ss, rst, dio0);

  // // в параметре метода LoRa.begin(---E-) укажите частоту,
  // // соответствующую региону вашего проживания;
  // // примечание: она должна соответствовать частоте отправителя;
  // // 433E6 для Азии
  // // 866E6 для Европы
  // // 915E6 для Северной Америки
  // if (!LoRa.begin(433E6))
  // {
  //   Serial.println("Starting LoRa failed!");
  //   // while (1);
  // }
  // else
  // {
  //   LoRa.setSyncWord(0xF3);
  //   Serial.println("LoRa Initializing OK!");
  // }

  delay(5000);
  tft.fillScreen(ST7735_BLACK); //clear display after initialization

  // настройки для показа VCC
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, atten);
  check_efuse();

  //Characterize ADC at particular atten
  adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
  v_divider = (float)(settings.VCC.r1 + settings.VCC.r2) / (float)settings.VCC.r2;

  printVcc();
  tickerVcc.start();
}
