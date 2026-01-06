#include <Wire.h>
#include <Adafruit_INA219.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <vector>
#include <Update.h>

Adafruit_INA219 ina219;
RTC_DS3231 rtc;
bool ina219_found = false;
bool rtc_found = false;

#define RELAY_PIN 5
#define SDA_PIN 8
#define SCL_PIN 9

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

float peak_v = 0, peak_c = 0, peak_p = 0;

unsigned long debounce_delay_ms = 60000;
unsigned long debounce_timer_start = 0;
int last_stable_state = LOW;

bool is_online = false;
bool ntp_synced = false;
unsigned long last_wifi_check = 0;
const unsigned long wifi_check_interval = 30000; 

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

void syncInternalClockFromRTC() {
  if (!rtc_found) return;
  DateTime now = rtc.now();
  struct tm tm;
  tm.tm_year = now.year() - 1900;
  tm.tm_mon = now.month() - 1;
  tm.tm_mday = now.day();
  tm.tm_hour = now.hour();
  tm.tm_min = now.minute();
  tm.tm_sec = now.second();
  time_t t = mktime(&tm);
  struct timeval tv = { .tv_sec = t };
  settimeofday(&tv, NULL);
}

void enterDeepSleep() {
  static unsigned long last_sleep_check = 0;
  if (millis() - last_sleep_check < 10000) return;
  last_sleep_check = millis();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return;

  int current_hour = timeinfo.tm_hour;
  int relayState = digitalRead(RELAY_PIN);

  bool is_night = false;
  if (current_hour >= 19 || current_hour < 8) is_night = true;

  if (is_night && relayState == LOW) {
    if (off_start_time == 0) {
      off_start_time = millis();
      addLog("Night mode: Timer start (30m)");
      return;
    }
    
    if (millis() - off_start_time >= 1800000UL) {
      struct tm target_time = timeinfo;
      if (current_hour >= 19) {
        target_time.tm_mday++;
      }
      target_time.tm_hour = 8;
      target_time.tm_min = 0;
      target_time.tm_sec = 0;
      
      time_t now = mktime(&timeinfo);
      time_t then = mktime(&target_time);
      
      uint64_t sleep_us = (uint64_t)(then - now) * 1000000ULL;
      if (sleep_us > 0) {
        addLog("Sleep: Wake 8AM");
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

void handleToggle() {
  if (server.hasArg("state")) {
    int s = server.arg("state").toInt();
    digitalWrite(RELAY_PIN, s);
    last_stable_state = s;
    addLog("Manual -> " + String(s == HIGH ? "ON" : "OFF"));
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleResetPeaks() {
  peak_v = 0;
  peak_c = 0;
  peak_p = 0;
  addLog("Peaks Reset");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRoot() {
  setINA219Active();
  delay(60);
  float v = ina219.getBusVoltage_V();
  float c = ina219.getCurrent_mA();
  float p = ina219.getPower_mW();
  setINA219PowerDown();
  
  if (v > peak_v) peak_v = v;
  if (c > peak_c) peak_c = c;
  if (p > peak_p) peak_p = p;

  float temp = 0;
  if (rtc_found) {
    temp = rtc.getTemperature() - 2.0;
  }
  
  int relayState = digitalRead(RELAY_PIN);
  
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;padding:15px;max-width:450px;margin:auto;background:#f4f4f4;}";
  html += ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:15px;}";
  html += ".status{font-weight:bold;color:" + String(relayState == HIGH ? "#2e7d32" : "#c62828") + ";}";
  html += "input{width:100%;box-sizing:border-box;margin-bottom:10px;padding:10px;border:1px solid #ccc;border-radius:4px;}";
  html += "button{width:100%;padding:12px;background:#1976d2;color:white;border:none;border-radius:4px;cursor:pointer;margin-bottom:5px;}";
  html += ".btn-off{background:#c62828;} .btn-on{background:#2e7d32;} .btn-reset{background:#757575; font-size:12px; padding:8px;}";
  html += ".peak{color:#d32f2f; font-size: 0.85em;}";
  html += ".log-box{background:#212121;color:#00e676;padding:10px;font-family:monospace;font-size:11px;height:150px;overflow-y:auto;border-radius:4px;}</style></head><body>";
  
  html += "<h1>Solar System</h1><div class='card'>";
  html += "<p>Time: " + getTimeString() + "</p>";
  html += "<p>RTC Temp: <b>" + String(temp, 1) + " C</b></p>";
  html += "<p>Voltage: <b>" + String(v, 2) + " V</b> <span class='peak'>(Peak: " + String(peak_v, 2) + ")</span></p>";
  html += "<p>Current: <b>" + String(c, 1) + " mA</b> <span class='peak'>(Peak: " + String(peak_c, 1) + ")</span></p>";
  html += "<p>Power: <b>" + String(p / 1000.0, 2) + " W</b> <span class='peak'>(Peak: " + String(peak_p / 1000.0, 2) + ")</span></p>";
  html += "<p>Relay State: <span class='status'>" + String(relayState == HIGH ? "ACTIVE" : "INACTIVE") + "</span></p>";
  html += "<button class='btn-reset' onclick=\"location.href='/reset_peaks'\">Reset Peak Values</button></div>";
  
  html += "<h2>Manual Control</h2><div class='card'>";
  html += "<button class='btn-on' onclick=\"location.href='/toggle?state=1'\">FORCE ON</button>";
  html += "<button class='btn-off' onclick=\"location.href='/toggle?state=0'\">FORCE OFF</button></div>";

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
  float current_mW = ina219.getPower_mW();
  setINA219PowerDown();

  if (current_voltage_V > peak_v) peak_v = current_voltage_V;
  if (current_mA > peak_c) peak_c = current_mA;
  if (current_mW > peak_p) peak_p = current_mW;

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

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!is_online) {
      addLog("WiFi Connected");
      is_online = true;
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      
      server.on("/", handleRoot);
      server.on("/save", HTTP_POST, handleSave);
      server.on("/toggle", handleToggle);
      server.on("/reset_peaks", handleResetPeaks);
      server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK. REBOOTING...");
        delay(1000);
        ESP.restart();
      }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Update.begin(UPDATE_SIZE_UNKNOWN);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          Update.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
          Update.end(true);
        }
      });
      server.begin();
    }

    if (!ntp_synced) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
        if (rtc_found) {
          rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
        }
        addLog("NTP Synced");
        ntp_synced = true;
      }
    }
    
    server.handleClient();
  } else {
    if (is_online) {
      addLog("WiFi Connection Lost");
      is_online = false;
      ntp_synced = false;
    }
    
    if (millis() - last_wifi_check > wifi_check_interval || last_wifi_check == 0) {
      last_wifi_check = millis();
      WiFi.begin(ssid);
    }
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 

  Wire.begin(SDA_PIN, SCL_PIN);
  
  ina219_found = ina219.begin();
  rtc_found = rtc.begin();
  
  if (rtc_found) {
    syncInternalClockFromRTC();
    addLog("RTC Init OK");
  } else {
    addLog("RTC Not Found");
  }

  loadSettings();

  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid);
}

void loop() {
  checkAndControlRelay();
  maintainWiFi();

  if (rtc_found && !ntp_synced) {
    static unsigned long last_rtc_sync = 0;
    if (millis() - last_rtc_sync > 60000) {
      syncInternalClockFromRTC();
      last_rtc_sync = millis();
    }
  }

  if (is_online) {
    enterDeepSleep();
  }
  
  delay(10);
}
