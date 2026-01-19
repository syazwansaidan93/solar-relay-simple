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
  if (Wire.endTransmission() != 0) {
    ina219_found = false;
  }
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
  peak_v = 0; peak_c = 0; peak_p = 0;
  addLog("Peaks Reset");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleUpdatePage() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;padding:15px;max-width:450px;margin:auto;background:#f4f4f4;}";
  html += ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:15px;}";
  html += "button{width:100%;padding:12px;background:#1976d2;color:white;border:none;border-radius:4px;cursor:pointer;}";
  html += "input{width:100%;margin-bottom:10px;}</style></head><body>";
  html += "<h1>Firmware Update</h1><div class='card'><form method='POST' action='/update_exec' enctype='multipart/form-data'>";
  html += "<input type='file' name='update'><button type='submit'>Upload BIN</button></form></div>";
  html += "<p style='text-align:center'><a href='/'>Back Home</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleConfigPage() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;padding:15px;max-width:450px;margin:auto;background:#f4f4f4;}";
  html += ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:15px;}";
  html += "input{width:100%;box-sizing:border-box;margin-bottom:10px;padding:10px;border:1px solid #ccc;border-radius:4px;}";
  html += "button{width:100%;padding:12px;background:#1976d2;color:white;border:none;border-radius:4px;cursor:pointer;}</style></head><body>";
  html += "<h1>Configuration</h1><div class='card'><form action='/save' method='POST'>";
  html += "Low Cutoff (V): <input type='number' step='0.1' name='v_low' value='" + String(voltage_low_cutoff_V, 1) + "'>";
  html += "High Threshold (V): <input type='number' step='0.1' name='v_high' value='" + String(voltage_high_on_threshold_V, 1) + "'>";
  html += "ON Current (mA): <input type='number' step='1' name='c_high' value='" + String(current_on_threshold_mA, 0) + "'>";
  html += "Wake Hour (0-23): <input type='number' name='wake_h' value='" + String(wake_h) + "'>";
  html += "Wake Minute (0-59): <input type='number' name='wake_m' value='" + String(wake_m) + "'>";
  html += "<button type='submit'>Save Changes</button></form></div>";
  html += "<p style='text-align:center'><a href='/'>Back Home</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleRoot() {
  setINA219Active();
  delay(65);
  float v = (ina219_found) ? ina219.getBusVoltage_V() : 0.0;
  float c_mA = (ina219_found) ? ina219.getCurrent_mA() : 0.0;
  float p_mW = (ina219_found) ? ina219.getPower_mW() : 0.0;
  setINA219PowerDown();
  
  float current_A = c_mA / 1000.0;
  float power_W = p_mW / 1000.0;
  float peak_c_display = peak_c / 1000.0;
  float peak_p_display = peak_p / 1000.0;
  
  int relayState = digitalRead(RELAY_PIN);
  
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;padding:15px;max-width:450px;margin:auto;background:#f4f4f4;}";
  html += ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:15px;}";
  html += ".status{font-weight:bold;color:" + String(relayState == HIGH ? "#2e7d32" : "#c62828") + ";}";
  html += "button{width:100%;padding:12px;background:#1976d2;color:white;border:none;border-radius:4px;cursor:pointer;margin-bottom:5px;}";
  html += ".btn-off{background:#c62828;} .btn-on{background:#2e7d32;} .btn-reset{background:#757575; font-size:12px; padding:8px;}";
  html += ".peak{color:#d32f2f; font-size: 0.85em;}";
  html += ".log-box{background:#212121;color:#00e676;padding:10px;font-family:monospace;font-size:11px;height:150px;overflow-y:auto;border-radius:4px;}</style></head><body>";
  html += "<h1>Solar System</h1><div class='card'><p>Time: " + getTimeStringFull() + "</p>";
  html += "<p>Voltage: <b>" + String(v, 2) + " V</b> <span class='peak'>(Peak: " + String(peak_v, 2) + ")</span></p>";
  html += "<p>Current: <b>" + String(current_A, 3) + " A</b> <span class='peak'>(Peak: " + String(peak_c_display, 3) + ")</span></p>";
  html += "<p>Power: <b>" + String(power_W, 2) + " W</b> <span class='peak'>(Peak: " + String(peak_p_display, 2) + ")</span></p>";
  html += "<p>Relay: <span class='status'>" + String(relayState == HIGH ? "ACTIVE" : "INACTIVE") + "</span></p>";
  html += "<button class='btn-reset' onclick=\"location.href='/reset_peaks'\">Reset Peak Values</button></div>";
  html += "<h2>Control</h2><div class='card'><button class='btn-on' onclick=\"location.href='/toggle?state=1'\">FORCE ON</button>";
  html += "<button class='btn-off' onclick=\"location.href='/toggle?state=0'\">FORCE OFF</button></div>";
  html += "<h2>History</h2><div id='lb' class='log-box'>";
  for (const auto& log : eventLogs) html += "<div>" + log + "</div>";
  html += "</div><script>var b=document.getElementById('lb');b.scrollTop=b.scrollHeight;</script>";
  html += "<p style='text-align:center'><a href='/config'>Config Settings</a> | <a href='/update'>Firmware Update</a> | <a href='/'>Refresh</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("v_low")) voltage_low_cutoff_V = server.arg("v_low").toFloat();
  if (server.hasArg("v_high")) voltage_high_on_threshold_V = server.arg("v_high").toFloat();
  if (server.hasArg("c_high")) current_on_threshold_mA = server.arg("c_high").toFloat();
  if (server.hasArg("wake_h")) wake_h = server.arg("wake_h").toInt();
  if (server.hasArg("wake_m")) wake_m = server.arg("wake_m").toInt();
  preferences.begin("solar_relay", false);
  preferences.putFloat("v_low", voltage_low_cutoff_V);
  preferences.putFloat("v_high", voltage_high_on_threshold_V);
  preferences.putFloat("c_high", current_on_threshold_mA);
  preferences.putInt("wake_h", wake_h);
  preferences.putInt("wake_m", wake_m);
  preferences.end();
  addLog("Settings updated");
  server.sendHeader("Location", "/");
  server.send(303);
}

void checkAndControlRelay() {
  if (millis() < 5000) return;
  static unsigned long last_read = 0;
  static unsigned long current_interval = 10000;
  
  if (millis() - last_read < current_interval) return;
  last_read = millis();

  if (!ina219_found) {
    ina219_found = ina219.begin();
    if (!ina219_found) return;
  }
  
  setINA219Active();
  delay(65);
  float v = (ina219_found) ? ina219.getBusVoltage_V() : 0.0;
  float c = (ina219_found) ? ina219.getCurrent_mA() : 0.0;
  float p = (ina219_found) ? ina219.getPower_mW() : 0.0;
  setINA219PowerDown();

  if (v < 1.0) return;

  if (v > peak_v) peak_v = v;
  if (c > peak_c) peak_c = c;
  if (p > peak_p) peak_p = p;

  bool in_critical_zone = (abs(v - voltage_high_on_threshold_V) < 0.2) || (abs(v - voltage_low_cutoff_V) < 0.2);
  current_interval = in_critical_zone ? 2000 : 10000;

  int desired = last_stable_state;
  if (v >= voltage_high_on_threshold_V && c >= current_on_threshold_mA) desired = HIGH;
  else if (v <= voltage_low_cutoff_V) desired = LOW;

  if (desired != last_stable_state) {
    if (debounce_timer_start == 0) debounce_timer_start = millis();
    if (millis() - debounce_timer_start >= debounce_delay_ms) {
      digitalWrite(RELAY_PIN, desired);
      last_stable_state = desired;
      debounce_timer_start = 0;
      addLog("Relay -> " + String(desired == HIGH ? "ON" : "OFF"));
    }
  } else {
    debounce_timer_start = 0;
  }
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!is_online) {
      addLog("WiFi Online");
      is_online = true;
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      server.on("/", handleRoot);
      server.on("/config", handleConfigPage);
      server.on("/save", HTTP_POST, handleSave);
      server.on("/toggle", handleToggle);
      server.on("/reset_peaks", handleResetPeaks);
      server.on("/update", handleUpdatePage);
      server.on("/update_exec", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "REBOOTING...");
        delay(1000);
        ESP.restart();
      }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
        else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
        else if (upload.status == UPLOAD_FILE_END) Update.end(true);
      });
      server.begin();
    }
    if (!ntp_synced) {
      struct tm ti;
      if (getLocalTime(&ti) && ti.tm_year > 120) {
        addLog("NTP Synced");
        ntp_synced = true;
      }
    }
    server.handleClient();
  } else {
    if (is_online) {
      is_online = false; ntp_synced = false;
      addLog("WiFi Lost");
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
  last_stable_state = LOW;
  
  struct tm tm_reset;
  tm_reset.tm_year = 70; 
  tm_reset.tm_mon = 0;
  tm_reset.tm_mday = 1;
  tm_reset.tm_hour = 0;
  tm_reset.tm_min = 0;
  tm_reset.tm_sec = 0;
  time_t t_reset = mktime(&tm_reset);
  struct timeval tv = { .tv_sec = t_reset };
  settimeofday(&tv, NULL);

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
