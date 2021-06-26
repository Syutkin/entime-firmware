#include <Arduino.h>
#include "ESPAsyncWebServer.h"
#include <Update.h>
#include <BluetoothSerial.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Streaming.h>
#include <sm16188.h>
#include <fonts/SystemFont5x7.h>
#include <fonts/Arial14.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#define MODULE_TYPE "led"
const String MODULE_NAME = "LEDpanel";
const char *version = "1.0.3";

const char *PARAM_UPPERLINE = "upperline";
const char *PARAM_BOTTOMLINE = "bottomline";
const char *PARAM_SINGLELINE = "singleline";

typedef struct
{
  bool active;
  String name;
  uint8_t number;
} S_Bluetooth;

typedef struct
{
  bool active;
  String ssid;
  String passwd;
} S_WiFi;

typedef struct
{
  uint8_t brightness;
} S_LedPanel;

typedef struct
{
  S_Bluetooth Bluetooth;
  S_WiFi WiFi;
  S_LedPanel LedPanel;
} Settings;

AsyncWebServer server(80);
const char *serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
BluetoothSerial SerialBT;
Preferences preferences;
Settings settings;

bool restartRequired = false; // Set this flag in the callbacks to restart ESP in the main loop

bool wifi_setup(String ssid, String passwd);

// LED Panel setup
#define DISPLAYS_ACROSS 5
#define DISPLAYS_DOWN 1

// const uint8_t D1 = 22;
// const uint8_t D2 = 23;
const gpio_num_t D1 = GPIO_NUM_22;
const gpio_num_t D2 = GPIO_NUM_23;

SM16188<D1, D2> sm16188;

//Timer setup
//create a hardware timer of ESP32
hw_timer_t *timer = NULL;

void IRAM_ATTR triggerScan()
{
  sm16188.updateScreen();
}

void clearupperline()
{
  sm16188.drawFilledBox(0, 0, 159, 7, GRAPHICS_INVERSE);
}

void clearbottomline()
{
  sm16188.drawFilledBox(0, 8, 159, 15, GRAPHICS_INVERSE);
}

void loadSettings(String MODULE_NAME)
{
  Serial << "Settings: Loading..." << endl;

  preferences.begin("Bluetooth", false);
  settings.Bluetooth.active = preferences.getBool("active", true);
  settings.Bluetooth.name = preferences.getString("name", "FR-" + MODULE_NAME + "-"); //имя Bluetooth модуля
  settings.Bluetooth.number = preferences.getUShort("number", 1);                     //номер в имени Bluetooth модуля
  preferences.end();

  preferences.begin("WiFi", false);
  settings.WiFi.active = preferences.getBool("active", false);
  settings.WiFi.ssid = preferences.getString("ssid", "");
  settings.WiFi.passwd = preferences.getString("passwd", "");
  preferences.end();

  preferences.begin("LedPanel", false);
  settings.LedPanel.brightness = preferences.getUShort("brightness", 15);
  preferences.end();

  Serial << "Settings: Loaded" << endl;
  Serial << "WiFi: " << settings.WiFi.active << endl;
  Serial << "ssid: " << settings.WiFi.ssid << endl;
  Serial << "passwd: " << settings.WiFi.passwd << endl;
  Serial << "brightness: " << settings.LedPanel.brightness << endl;
}

void readBTSerial()
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

  const size_t capacity = 768;
  DynamicJsonDocument doc(capacity);

  //JsonObject obj = doc.to<JsonObject>();
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, receivedData);
  // Test if parsing succeeds.
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  if (doc.containsKey("Read"))
  {
    bool Read = doc["Read"];
    if (Read)
    { //read settings
      log_d("Read settings");
      DynamicJsonDocument docAnswer(capacity);

      docAnswer["Type"] = MODULE_TYPE;

      JsonObject Bluetooth = docAnswer.createNestedObject("Bluetooth");
      Bluetooth["active"] = settings.Bluetooth.active;
      Bluetooth["name"] = settings.Bluetooth.name;
      Bluetooth["number"] = settings.Bluetooth.number;

      JsonObject WiFi = docAnswer.createNestedObject("WiFi");
      WiFi["active"] = settings.WiFi.active;
      WiFi["ssid"] = settings.WiFi.ssid;
      WiFi["passwd"] = settings.WiFi.passwd;

      JsonObject LedPanel = docAnswer.createNestedObject("LedPanel");
      LedPanel["brightness"] = settings.LedPanel.brightness;

      // JSON to Serial
      serializeJson(docAnswer, SerialBT);
      SerialBT << endl;

      serializeJson(docAnswer, Serial);
      Serial << endl;
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
      if (doc.containsKey("WiFi"))
      {
        bool reconnect = false;
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
            settings.WiFi.active = s_wifi.active;
            if (s_wifi.active)
              log_i("WiFi enabled");
            else
              log_i("WiFi disabled");
            reconnect = true;
          }
        }
        if (jWiFi.containsKey("ssid"))
        {
          s_wifi.ssid = jWiFi["ssid"].as<String>();
          if (s_wifi.ssid != settings.WiFi.ssid)
          {
            preferences.putString("ssid", s_wifi.ssid);
            settings.WiFi.ssid = s_wifi.ssid;
            if (s_wifi.ssid == "")
              log_i("WiFi ssid cleared");
            else
              log_i("WiFi ssid set to %s", settings.WiFi.ssid);
            reconnect = true;
          }
        }
        if (jWiFi.containsKey("passwd"))
        {
          s_wifi.passwd = jWiFi["passwd"].as<String>();
          if (s_wifi.passwd != settings.WiFi.passwd)
          {
            preferences.putString("passwd", s_wifi.passwd);
            settings.WiFi.passwd = s_wifi.passwd;
            log_i("WiFi new password set");
            reconnect = true;
          }
        }
        preferences.end();
        if (reconnect)
        {
          if (settings.WiFi.active)
          {
            if (WiFi.isConnected())
            {
              server.end();
              WiFi.disconnect();
            }
            if (wifi_setup(settings.WiFi.ssid, settings.WiFi.passwd))
            {
              server.begin();
            }
          }
          else
          {
            server.end();
            WiFi.disconnect();
          }
        }
      }
      if (doc.containsKey("LedPanel"))
      {
        log_d("Write LedPanel settings");
        JsonObject jLedPanel = doc["LedPanel"];
        S_LedPanel s_ledpanel;
        preferences.begin("LedPanel", false);
        if (jLedPanel.containsKey("brightness"))
        {
          s_ledpanel.brightness = jLedPanel["brightness"];
          if (s_ledpanel.brightness != settings.LedPanel.brightness)
          {
            preferences.putUShort("brightness", s_ledpanel.brightness);
            settings.LedPanel.brightness = s_ledpanel.brightness;
            sm16188.setBrightness(s_ledpanel.brightness);
            log_i("LedPanel brightness set to %d", s_ledpanel.brightness);
          }
        }
        preferences.end();
      }
    }
  }
  else
  {
    Serial << "Unknown JSON command" << endl;
  }
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

bool wifi_setup(String ssid, String passwd)
{
  const char *c_ssid = ssid.c_str();
  const char *c_passwd = passwd.c_str();
  // WiFi.mode(WIFI_STA);
  // WiFi.begin(c_ssid, c_passwd);
  // if (WiFi.waitForConnectResult() != WL_CONNECTED)
  // {
  //   Serial.printf("WiFi Failed!\n");
  //   sm16188.clearScreen(true);
  //   sm16188.drawString(0, 0, "WiFi Failed!", 13, GRAPHICS_NORMAL);
  //   return false;
  // }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(c_ssid, c_passwd);

  Serial.print("IP Address: ");
  // Serial.println(WiFi.localIP());
  Serial.println(WiFi.softAPIP());
  sm16188.clearScreen(true);
  sm16188.drawString(0, 0, "IP Address:", 12, GRAPHICS_NORMAL);
  // sm16188.drawString(0, 8, WiFi.localIP().toString().c_str(), WiFi.localIP().toString().length(), GRAPHICS_NORMAL);
  sm16188.drawString(0, 8, WiFi.softAPIP().toString().c_str(), WiFi.softAPIP().toString().length(), GRAPHICS_NORMAL);
  return true;
}

void server_setup()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "FRaction finish LEDPanel\r\nFirmware version: " + String(version)); });

  // Send a GET request to <IP>/get?message=<message>
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              String upperline;
              String bottomline;
              String singleline;
              if (request->hasParam(PARAM_UPPERLINE))
              {
                upperline = request->getParam(PARAM_UPPERLINE)->value();
              }
              if (request->hasParam(PARAM_BOTTOMLINE))
              {
                bottomline = request->getParam(PARAM_BOTTOMLINE)->value();
              }
              if (request->hasParam(PARAM_SINGLELINE))
              {
                singleline = request->getParam(PARAM_SINGLELINE)->value();
              }
              // else {
              //     message = "No message sent";
              // }
              request->send(200, "text/plain", "Hello, GET:\r\nupperline:  " + upperline + "\r\nbottomline: " + bottomline + "\r\nsingleline: " + singleline);
              Serial.println("GET");
              Serial.println("upperline:  " + upperline);
              Serial.println("bottomline: " + bottomline);
              Serial.println("singleline: " + singleline);
            });

  // Send a POST request to <IP>/post with a form field message set to <message>
  server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              String upperline;
              String bottomline;
              String singleline;
              if (request->hasParam(PARAM_UPPERLINE, true))
              {
                upperline = request->getParam(PARAM_UPPERLINE, true)->value();
              }
              if (request->hasParam(PARAM_BOTTOMLINE, true))
              {
                bottomline = request->getParam(PARAM_BOTTOMLINE, true)->value();
              }
              if (request->hasParam(PARAM_SINGLELINE, true))
              {
                singleline = request->getParam(PARAM_SINGLELINE, true)->value();
              }
              // else
              // {
              //     message = "No message sent";
              // }
              request->send(200, "text/plain", "Hello, POST:\r\nupperline:  " + upperline + "\r\nbottomline: " + bottomline + "\r\nsingleline: " + singleline);
              Serial.println("POST");
              Serial.println("upperline:  " + upperline);
              Serial.println("bottomline: " + bottomline);
              Serial.println("singleline: " + singleline);
              sm16188.clearScreen(true);
              if (!upperline.isEmpty())
              {
                // clearupperline();
                sm16188.drawString(0, 0, upperline.c_str(), upperline.length(), GRAPHICS_NORMAL);
              }
              if (!bottomline.isEmpty())
              {
                // clearbottomline();
                sm16188.drawString(0, 8, bottomline.c_str(), bottomline.length(), GRAPHICS_NORMAL);
              }
              if (!singleline.isEmpty())
              {
                sm16188.clearScreen(true);
                sm16188.selectFont(Arial_14);
                sm16188.drawString(0, 2, singleline.c_str(), singleline.length(), GRAPHICS_NORMAL);
                sm16188.selectFont(SystemFont5x7);
              }
            });

  server.on("/firmware", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              AsyncWebServerResponse *response = request->beginResponse(200, "text/html", serverIndex);
              response->addHeader("Connection", "close");
              response->addHeader("Access-Control-Allow-Origin", "*");
              request->send(response);
              // disable updating LEDPanel, 'cause it didn't work with "noInterrupts" at UpdateScreen();
              timerAlarmDisable(timer);
            });

  server.on(
      "/update", HTTP_POST, [](AsyncWebServerRequest *request)
      {
        // the request handler is triggered after the upload has finished...
        // create the response, add header, and send response
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");
        restartRequired = true; // Tell the main loop to restart the ESP
        request->send(response);
      },
      [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
      {
        //Upload handler chunks in data

        if (!index)
        { // if index == 0 then this is the first frame of data
          Serial.printf("UploadStart: %s\n", filename.c_str());
          Serial.setDebugOutput(true);

          // calculate sketch space required for the update
          uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!Update.begin(maxSketchSpace))
          { //start with max available size
            Update.printError(Serial);
          }
          // Update.runAsync(true); // tell the updaterClass to run in async mode
        }

        //Write chunked data to the free sketch space
        if (Update.write(data, len) != len)
        {
          Update.printError(Serial);
        }

        if (final)
        { // if the final flag is set then this is the last frame of data
          if (Update.end(true))
          { //true to set the size to the current progress
            Serial.printf("Update Success: %u B\nRebooting...\n", index + len);
          }
          else
          {
            Update.printError(Serial);
          }
          Serial.setDebugOutput(false);
        }
      });

  server.onNotFound(notFound);
}

void setup()
{
  Serial.begin(57600);

  loadSettings(MODULE_NAME);

  // включаем панель
  sm16188.begin(DISPLAYS_ACROSS, DISPLAYS_DOWN);
  sm16188.selectFont(Arial_14);
  sm16188.drawString(10, 1, "Hello there", 12, GRAPHICS_NORMAL);
  sm16188.selectFont(SystemFont5x7);
  sm16188.setBrightness(settings.LedPanel.brightness);

  //ToDo: пока не выключать BT из настроек
  // if (settings.Bluetooth.active)
  if (true)
  {
    if (!SerialBT.begin(settings.Bluetooth.name + settings.Bluetooth.number))
    {
      Serial << "Bluetooth: Module " << settings.Bluetooth.name << settings.Bluetooth.number << " failed to start" << endl;
    }
    else
    {
      Serial << "Bluetooth: Module " << settings.Bluetooth.name << settings.Bluetooth.number << " is ready to pair" << endl;
    }
  }
  else
  {
    Serial << "Bluetooth off" << endl;
  }

  // Настраиваем сервер
  server_setup();

  // Включем WiFi
  if (settings.WiFi.active)
  {
    if (wifi_setup(settings.WiFi.ssid, settings.WiFi.passwd))
    {
      server.begin();
    }
  }

  Serial.print("Firmware version: ");
  Serial.println(version);

  // return the clock speed of the CPU
  // uint8_t cpuClock = ESP.getCpuFreqMHz();

  // Use 1st timer of 4
  // divide cpu clock speed on its speed value by MHz to get 1us for each signal  of the timer
  // timer = timerBegin(0, cpuClock, true);
  timer = timerBegin(0, 80, true);
  // Attach triggerScan function to our timer
  timerAttachInterrupt(timer, &triggerScan, true);
  // Set alarm to call triggerScan function
  // Repeat the alarm (third parameter)
  timerAlarmWrite(timer, 100000, true);

  // Start an alarm
  timerAlarmEnable(timer);
}

void loop()
{
  // чтение данных из SerialBT
  // Check if Data over SerialBT has arrived
  if (SerialBT.available() != 0)
  {
    // Get and parse received data
    readBTSerial();
  }

  if (restartRequired)
  { // check the flag here to determine if a restart is required
    Serial.printf("Restarting ESP\n\r");
    restartRequired = false;
    ESP.restart();
  }
}