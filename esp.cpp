#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <vector>
#include <Update.h>

Adafruit_INA219 ina219;
bool ina219_found = false;

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
int wake_h;
int wake_m;

float peak_v = 0, peak_c = 0, peak_p = 0;
float total_Wh = 0;

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
  if (eventLogs.size() > MAX_LOGS) eventLogs.erase(eventLogs.begin());
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
  if (Wire.endTransmission() != 0) ina219_found = false;
}

void loadSettings() {
  preferences.begin("solar_relay", true);
  voltage_low_cutoff_V = preferences.getFloat("v_low", 12.1);
  voltage_high_on_threshold_V = preferences.getFloat("v_high", 13.2);
  current_on_threshold_mA = preferences.getFloat("c_high", 150.0);
  wake_h = preferences.getInt("wake_h", 8);
  wake_m = preferences.getInt("wake_m", 0);
  preferences.end();
}

String getTimeStringFull() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return "Time Not Synced";
  char timeStringBuff[20];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void enterDeepSleep() {
  if (!ntp_synced) return;
  static unsigned long last_sleep_check = 0;
  if (millis() - last_sleep_check < 15000) return;
  last_sleep_check = millis();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return;

  int current_total_mins = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  int wake_total_mins = (wake_h * 60) + wake_m;
  int relayState = digitalRead(RELAY_PIN);

  bool is_night = (timeinfo.tm_hour >= 19 || current_total_mins < wake_total_mins);

  if (is_night && relayState == LOW) {
    if (off_start_time == 0) {
      off_start_time = millis();
      return;
    }
    if (millis() - off_start_time >= 60000UL) {
      struct tm target_time = timeinfo;
      if (timeinfo.tm_hour >= 19) target_time.tm_mday++;
      target_time.tm_hour = wake_h;
      target_time.tm_min = wake_m;
      target_time.tm_sec = 0;

      time_t now = mktime(&timeinfo);
      time_t then = mktime(&target_time);
      uint64_t sleep_us = (uint64_t)(then - now) * 1000000ULL;

      if (sleep_us > 0) {
        setINA219PowerDown();
        digitalWrite(RELAY_PIN, LOW);
        delay(100);
        esp_sleep_enable_timer_wakeup(sleep_us);
        esp_deep_sleep_start();
      }
    }
  } else off_start_time = 0;
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
  peak_v = 0; peak_c = 0; peak_p = 0; total_Wh = 0;
  addLog("Stats Reset");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRoot() {
  setINA219Active();
  delay(65);
  float v = ina219_found ? ina219.getBusVoltage_V() : 0.0;
  float c_mA = ina219_found ? ina219.getCurrent_mA() : 0.0;
  float p_mW = ina219_found ? ina219.getPower_mW() : 0.0;
  setINA219PowerDown();

  float current_A = c_mA / 1000.0;
  float power_W = p_mW / 1000.0;
  float peak_c_display = peak_c / 1000.0;
  float peak_p_display = peak_p / 1000.0;

  int relayState = digitalRead(RELAY_PIN);

  String html = "<html><body>";
  html += "<h1>Solar System</h1>";
  html += "<p>Time: " + getTimeStringFull() + "</p>";
  html += "<p>Voltage: " + String(v,2) + " V</p>";
  html += "<p>Current: " + String(current_A,2) + " A</p>";
  html += "<p>Power: " + String(power_W,2) + " W</p>";
  html += "<p>Energy: " + String(total_Wh,3) + " Wh</p>";
  html += "<p>Relay: " + String(relayState==HIGH?"ON":"OFF") + "</p>";
  html += "<a href='/toggle?state=1'>ON</a> | <a href='/toggle?state=0'>OFF</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void checkAndControlRelay() {
  static unsigned long last_read = 0;
  if (millis() - last_read < 10000) return;
  float time_diff_hours = (millis() - last_read) / 3600000.0;
  last_read = millis();

  if (!ina219_found) ina219_found = ina219.begin();
  if (!ina219_found) return;

  setINA219Active();
  delay(65);
  float v = ina219.getBusVoltage_V();
  float c = ina219.getCurrent_mA();
  float p = ina219.getPower_mW();
  setINA219PowerDown();

  if (v < 1.0) return;

  if (v > peak_v) peak_v = v;
  if (c > peak_c) peak_c = c;
  if (p > peak_p) peak_p = p;

  total_Wh += (p / 1000.0) * time_diff_hours;

  int desired = last_stable_state;
  if (v >= voltage_high_on_threshold_V && c >= current_on_threshold_mA) desired = HIGH;
  else if (v <= voltage_low_cutoff_V) desired = LOW;

  if (desired != last_stable_state) {
    digitalWrite(RELAY_PIN, desired);
    last_stable_state = desired;
    addLog("Relay -> " + String(desired==HIGH?"ON":"OFF"));
  }
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!is_online) {
      is_online = true;
      addLog("WiFi Online");
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      server.on("/", handleRoot);
      server.on("/toggle", handleToggle);
      server.on("/reset_peaks", handleResetPeaks);
      server.begin();
    }
    server.handleClient();
  } else {
    if (is_online) {
      is_online = false;
      ntp_synced = false;
      addLog("WiFi Lost");
    }
    if (millis() - last_wifi_check > wifi_check_interval) {
      last_wifi_check = millis();
      WiFi.begin(ssid);
    }
  }
}

void setup() {
  setCpuFrequencyMhz(80);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  ina219_found = ina219.begin();
  if (ina219_found) setINA219PowerDown();

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
  if (is_online && ntp_synced) enterDeepSleep();
  delay(20);
}
