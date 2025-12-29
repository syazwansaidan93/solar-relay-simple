#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <vector>
#include <ArduinoOTA.h>
#include <Update.h>

Adafruit_INA219 ina219;
bool ina219_found = false;

#define RELAY_PIN 5
#define SDA_PIN 6
#define SCL_PIN 7

const char* ssid = "wifi_slow";
const char* ntpServer = "192.168.1.1";
const long gmtOffset_sec = 28800;
const int daylightOffset_sec = 0;

IPAddress local_IP(192, 168, 1, 5);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);
Preferences preferences;

float voltage_low_cutoff_V;
float voltage_high_on_threshold_V;
float current_on_threshold_mA;

unsigned long debounce_delay_ms = 60000;
unsigned long debounce_timer_start = 0;
int last_stable_state = LOW;

unsigned long wifi_retry_interval = 43200000;
unsigned long last_wifi_attempt = 0;
bool is_online = false;

unsigned long off_start_time = 0;

std::vector<String> eventLogs;
const int MAX_LOGS = 15;

void addLog(String msg) {
  struct tm timeinfo;
  String timestamp = "[No Time] ";
  if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
    char buff[12];
    strftime(buff, sizeof(buff), "%H:%M:%S", &timeinfo);
    timestamp = "[" + String(buff) + "] ";
  }
  String entry = timestamp + msg;
  eventLogs.push_back(entry);
  if (eventLogs.size() > MAX_LOGS) {
    eventLogs.erase(eventLogs.begin());
  }
}

void setINA219PowerDown() {
  if (!ina219_found) return;
  uint16_t config_value = 0x399F;
  config_value &= ~0x0007; 
  Wire.beginTransmission(0x40);
  Wire.write(0x00);
  Wire.write((config_value >> 8) & 0xFF);
  Wire.write(config_value & 0xFF);
  Wire.endTransmission();
}

void setINA219Active() {
  if (!ina219_found) return;
  uint16_t config_value = 0x399F;
  Wire.beginTransmission(0x40);
  Wire.write(0x00);
  Wire.write((config_value >> 8) & 0xFF);
  Wire.write(config_value & 0xFF);
  Wire.endTransmission();
}

void loadSettings() {
  preferences.begin("solar_relay", true);
  voltage_low_cutoff_V = preferences.getFloat("v_low", 12.1);
  voltage_high_on_threshold_V = preferences.getFloat("v_high", 13.2);
  current_on_threshold_mA = preferences.getFloat("c_high", 150.0);
  preferences.end();
  addLog("Settings Loaded");
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return "Time Not Synced";
  char timeStringBuff[20];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void enterDeepSleep() {
  static unsigned long last_sleep_check = 0;
  if (millis() - last_sleep_check < 10000) return;
  last_sleep_check = millis();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return;

  int current_hour = timeinfo.tm_hour;
  int current_min = timeinfo.tm_min;
  int relayState = digitalRead(RELAY_PIN);

  bool is_night = false;
  if (current_hour >= 19) is_night = true;
  if (current_hour < 7) is_night = true;
  if (current_hour == 7 && current_min < 30) is_night = true;

  if (is_night && relayState == LOW) {
    if (off_start_time == 0) {
      off_start_time = millis();
      addLog("Relay OFF after 7PM. Sleep timer started.");
      return;
    }
    
    if (millis() - off_start_time >= 3600000UL) {
      struct tm target_time = timeinfo;
      if (current_hour >= 19) {
        target_time.tm_mday++;
      }
      target_time.tm_hour = 7;
      target_time.tm_min = 30;
      target_time.tm_sec = 0;
      
      time_t now = mktime(&timeinfo);
      time_t then = mktime(&target_time);
      
      uint64_t sleep_us = (uint64_t)(then - now) * 1000000ULL;
      if (sleep_us > 0) {
        addLog("Deep Sleep: Wake 7:30AM");
        setINA219PowerDown();
        digitalWrite(RELAY_PIN, LOW);
        delay(200);
        esp_sleep_enable_timer_wakeup(sleep_us);
        esp_deep_sleep_start();
      }
    }
  } else {
    off_start_time = 0;
  }
}

void handleRoot() {
  setINA219Active();
  delay(60);
  float v = ina219.getBusVoltage_V();
  float c = ina219.getCurrent_mA();
  float p = ina219.getPower_mW();
  setINA219PowerDown();
  
  int relayState = digitalRead(RELAY_PIN);
  
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;padding:15px;max-width:450px;margin:auto;background:#f4f4f4;}";
  html += ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:15px;}";
  html += ".status{font-weight:bold;color:" + String(relayState == HIGH ? "#2e7d32" : "#c62828") + ";}";
  html += "input{width:100%;box-sizing:border-box;margin-bottom:10px;padding:10px;border:1px solid #ccc;border-radius:4px;}";
  html += "button{width:100%;padding:12px;background:#1976d2;color:white;border:none;border-radius:4px;cursor:pointer;}";
  html += ".log-box{background:#212121;color:#00e676;padding:10px;font-family:monospace;font-size:11px;height:150px;overflow-y:auto;border-radius:4px;}</style></head><body>";
  
  html += "<h1>Solar System</h1><div class='card'>";
  html += "<p>Time: " + getTimeString() + "</p>";
  html += "<p>Voltage: <b>" + String(v, 2) + " V</b></p>";
  html += "<p>Current: <b>" + String(c, 1) + " mA</b></p>";
  html += "<p>Power: <b>" + String(p / 1000.0, 2) + " W</b></p>";
  html += "<p>Relay State: <span class='status'>" + String(relayState == HIGH ? "ACTIVE" : "INACTIVE") + "</span></p></div>";
  
  html += "<h2>History</h2><div id='lb' class='log-box'>";
  for (const auto& log : eventLogs) html += "<div>" + log + "</div>";
  html += "</div><script>var b=document.getElementById('lb');b.scrollTop=b.scrollHeight;</script>";
  
  html += "<h2>Config</h2><div class='card'><form action='/save' method='POST'>";
  html += "Low Cutoff (V): <input type='number' step='0.1' name='v_low' value='" + String(voltage_low_cutoff_V, 1) + "'>";
  html += "High Threshold (V): <input type='number' step='0.1' name='v_high' value='" + String(voltage_high_on_threshold_V, 1) + "'>";
  html += "ON Current (mA): <input type='number' step='1' name='c_high' value='" + String(current_on_threshold_mA, 0) + "'>";
  html += "<button type='submit'>Apply Settings</button></form></div>";

  html += "<h2>Update Firmware</h2><div class='card'>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><button type='submit'>Upload BIN</button></form></div>";
  
  html += "<p style='text-align:center'><a href='/'>Manual Refresh</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("v_low")) voltage_low_cutoff_V = server.arg("v_low").toFloat();
  if (server.hasArg("v_high")) voltage_high_on_threshold_V = server.arg("v_high").toFloat();
  if (server.hasArg("c_high")) current_on_threshold_mA = server.arg("c_high").toFloat();
  
  preferences.begin("solar_relay", false);
  preferences.putFloat("v_low", voltage_low_cutoff_V);
  preferences.putFloat("v_high", voltage_high_on_threshold_V);
  preferences.putFloat("c_high", current_on_threshold_mA);
  preferences.end();
  
  addLog("Settings updated");
  server.sendHeader("Location", "/");
  server.send(303);
}

void checkAndControlRelay() {
  static unsigned long last_read_time = 0;
  if (millis() - last_read_time < 2000) return;
  last_read_time = millis();

  if (!ina219_found) return;
  
  setINA219Active();
  delay(60);
  float current_voltage_V = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  setINA219PowerDown();

  int desired_state = last_stable_state;
  if (current_voltage_V >= voltage_high_on_threshold_V && current_mA >= current_on_threshold_mA) {
    desired_state = HIGH;
  }
  else if (current_voltage_V <= voltage_low_cutoff_V) {
    desired_state = LOW;
  }

  if (desired_state != last_stable_state) {
    if (debounce_timer_start == 0) debounce_timer_start = millis();
    if (millis() - debounce_timer_start >= debounce_delay_ms) {
      digitalWrite(RELAY_PIN, desired_state);
      last_stable_state = desired_state;
      debounce_timer_start = 0;
      addLog("Relay -> " + String(desired_state == HIGH ? "ON" : "OFF"));
    }
  } else {
    debounce_timer_start = 0;
  }
}

void connectAndSync() {
  addLog("Connecting...");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  
  WiFi.begin(ssid);
  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 8000) {
    delay(200);
    checkAndControlRelay(); 
  }

  if (WiFi.status() == WL_CONNECTED) {
    addLog("Connected: " + WiFi.localIP().toString());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    struct tm timeinfo;
    unsigned long startNtp = millis();
    bool syncOk = false;
    while (millis() - startNtp < 5000) {
      if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
        addLog("NTP Ok: " + getTimeString());
        syncOk = true;
        break;
      }
      delay(200);
      checkAndControlRelay();
    }
    
    if (syncOk) {
      is_online = true;
      off_start_time = 0; 
      
      ArduinoOTA.setHostname("solar-relay-c3");
      ArduinoOTA.begin();

      server.on("/", handleRoot);
      server.on("/save", HTTP_POST, handleSave);
      
      server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK. REBOOTING...");
        delay(1000);
        ESP.restart();
      }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
          } else {
          }
        }
      });

      server.begin();
      addLog("Web/OTA Enabled");

      if (ina219_found) {
        setINA219Active();
        delay(100);
        float v = ina219.getBusVoltage_V();
        float c = ina219.getCurrent_mA();
        if (v >= voltage_high_on_threshold_V && c >= current_on_threshold_mA) {
          digitalWrite(RELAY_PIN, HIGH);
          last_stable_state = HIGH;
          addLog("Startup Check: Relay ON");
        } else {
          addLog("Startup Check: Relay OFF");
        }
        setINA219PowerDown();
      }
    } else {
      addLog("NTP Fail");
      WiFi.disconnect();
      is_online = false;
    }
  } else {
    addLog("WiFi Timeout");
    is_online = false;
  }
  last_wifi_attempt = millis();
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 

  Wire.begin(SDA_PIN, SCL_PIN);
  ina219_found = ina219.begin();
  
  loadSettings();
  
  connectAndSync();
}

void loop() {
  checkAndControlRelay();
  
  if (is_online) {
    ArduinoOTA.handle();
    server.handleClient();
    if (WiFi.status() != WL_CONNECTED) {
      is_online = false;
      last_wifi_attempt = millis();
      addLog("WiFi Lost");
    }
    enterDeepSleep();
  } else {
    if (millis() - last_wifi_attempt > wifi_retry_interval) {
      connectAndSync();
    }
  }
  
  delay(10);
}
