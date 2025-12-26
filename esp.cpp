#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <vector>

Adafruit_INA219 ina219;
bool ina219_found = false;

#define RELAY_PIN 5

const char* ssid = "wifi_slow";
const char* ntpServer = "192.168.1.1";
const long  gmtOffset_sec = 28800;
const int   daylightOffset_sec = 0;

IPAddress local_IP(192, 168, 1, 5);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);
Preferences preferences;

float voltage_low_cutoff_V;
float voltage_high_on_threshold_V;

unsigned long debounce_delay_ms = 60000;
unsigned long debounce_timer_start = 0;
int last_stable_state = LOW;

unsigned long wifi_retry_interval = 43200000;
unsigned long last_wifi_attempt = 0;
bool is_online = false;

std::vector<String> eventLogs;
const int MAX_LOGS = 20;

void addLog(String msg) {
  struct tm timeinfo;
  String timestamp = "[No Time] ";
  if (getLocalTime(&timeinfo)) {
    char buff[10];
    strftime(buff, sizeof(buff), "%H:%M:%S", &timeinfo);
    timestamp = "[" + String(buff) + "] ";
  }
  String entry = timestamp + msg;
  Serial.println(entry);
  eventLogs.push_back(entry);
  if (eventLogs.size() > MAX_LOGS) {
    eventLogs.erase(eventLogs.begin());
  }
}

void setINA219PowerDown() {
  uint16_t config_value = 0x399F;
  config_value &= ~0x0007;
  Wire.beginTransmission(0x40);
  Wire.write(0x00);
  Wire.write((config_value >> 8) & 0xFF);
  Wire.write(config_value & 0xFF);
  Wire.endTransmission();
}

void setINA219Active() {
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
  preferences.end();
  addLog("Settings Loaded");
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Time Not Synced";
  char timeStringBuff[20];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void enterDeepSleep() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  if (timeinfo.tm_year < 120) return;
  int current_hour = timeinfo.tm_hour;
  if (current_hour >= 0 && current_hour < 7) {
    struct tm target_time = timeinfo;
    target_time.tm_hour = 7;
    target_time.tm_min = 0;
    target_time.tm_sec = 0;
    time_t now = mktime(&timeinfo);
    time_t then = mktime(&target_time);
    uint64_t sleep_us = (uint64_t)(then - now) * 1000000ULL;
    if (sleep_us > 0) {
      addLog("Entering Deep Sleep until 7AM");
      delay(100);
      esp_sleep_enable_timer_wakeup(sleep_us);
      esp_deep_sleep_start();
    }
  }
}

void handleRoot() {
  setINA219Active();
  delay(50);
  float v = ina219.getBusVoltage_V();
  float c = ina219.getCurrent_mA();
  float p = ina219.getPower_mW();
  setINA219PowerDown();
  int relayState = digitalRead(RELAY_PIN);
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;} .status{font-weight:bold;color:" + String(relayState == HIGH ? "green" : "red") + ";} input{width:100%;margin-bottom:10px;padding:8px;} button{width:100%;padding:10px;background:#4CAF50;color:white;border:none;cursor:pointer;} .log-box{background:#eee;padding:10px;font-size:12px;height:200px;overflow-y:auto;border:1px solid #ccc;}</style></head><body>";
  html += "<h1>Solar Status</h1><p>Time: " + getTimeString() + "</p><p>Voltage: " + String(v) + " V</p><p>Current: " + String(c) + " mA</p><p>Power: " + String(p / 1000.0) + " W</p><p>Relay: <span class='status'>" + String(relayState == HIGH ? "ON" : "OFF") + "</span></p><h2>System Log</h2><div id='lb' class='log-box'>";
  for (const auto& log : eventLogs) html += "<div>" + log + "</div>";
  html += "</div><script>var b=document.getElementById('lb');b.scrollTop=b.scrollHeight;</script><h2>Settings</h2><form action='/save' method='POST'>V Low Cutoff (V): <input type='number' step='0.1' name='v_low' value='" + String(voltage_low_cutoff_V) + "'>V High On (V): <input type='number' step='0.1' name='v_high' value='" + String(voltage_high_on_threshold_V) + "'><button type='submit'>Save Settings</button></form><p><a href='/'>Refresh Data</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("v_low")) voltage_low_cutoff_V = server.arg("v_low").toFloat();
  if (server.hasArg("v_high")) voltage_high_on_threshold_V = server.arg("v_high").toFloat();
  preferences.begin("solar_relay", false);
  preferences.putFloat("v_low", voltage_low_cutoff_V);
  preferences.putFloat("v_high", voltage_high_on_threshold_V);
  preferences.end();
  addLog("Settings saved");
  server.sendHeader("Location", "/");
  server.send(303);
}

void checkAndControlRelay() {
  if (!ina219_found) return;
  setINA219Active();
  delay(50);
  float current_voltage_V = ina219.getBusVoltage_V();
  setINA219PowerDown();
  int desired_state = last_stable_state;
  if (current_voltage_V >= voltage_high_on_threshold_V) desired_state = HIGH;
  else if (current_voltage_V <= voltage_low_cutoff_V) desired_state = LOW;
  if (desired_state != last_stable_state) {
    if (debounce_timer_start == 0) debounce_timer_start = millis();
    if (millis() - debounce_timer_start >= debounce_delay_ms) {
      digitalWrite(RELAY_PIN, desired_state);
      last_stable_state = desired_state;
      debounce_timer_start = 0;
      addLog("Relay switched " + String(desired_state == HIGH ? "ON" : "OFF"));
    }
  } else {
    debounce_timer_start = 0;
  }
}

void connectAndSync() {
  addLog("Attempting connection...");
  if (!WiFi.config(local_IP, gateway, subnet)) Serial.println("STA Failed to configure");
  WiFi.begin(ssid);
  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 5000) delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    addLog("WiFi Connected: " + WiFi.localIP().toString());
    struct tm timeinfo;
    bool needsSync = true;
    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_year > 120) {
        needsSync = false;
        addLog("RTC Valid: " + getTimeString());
      }
    }
    if (needsSync) {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      unsigned long startNtp = millis();
      while (millis() - startNtp < 3000) {
        if (getLocalTime(&timeinfo)) {
          addLog("NTP Sync Success: " + getTimeString());
          break;
        }
        delay(100);
      }
    }
    if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
      is_online = true;
      server.on("/", handleRoot);
      server.on("/save", HTTP_POST, handleSave);
      server.begin();
      addLog("Server Ready");
    } else {
      addLog("NTP Failed - Staying offline");
      WiFi.disconnect();
    }
  } else {
    addLog("WiFi Timeout - Running offline");
  }
  last_wifi_attempt = millis();
}

void setup() {
  Serial.begin(115200);
  loadSettings();
  connectAndSync();
  Wire.begin(6, 7);
  ina219_found = ina219.begin();
  if (!ina219_found) addLog("INA219 Not Found!");
  else addLog("INA219 OK");
  pinMode(RELAY_PIN, OUTPUT);
  setINA219PowerDown();
}

void loop() {
  checkAndControlRelay();
  if (is_online) server.handleClient();
  else if (millis() - last_wifi_attempt > wifi_retry_interval) connectAndSync();
  enterDeepSleep();
}
