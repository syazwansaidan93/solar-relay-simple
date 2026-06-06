#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <Update.h>
#include "esp_pm.h"

Adafruit_INA219 ina219;
bool ina219_found = false;

#define RELAY_PIN 5
#define SDA_PIN 8
#define SCL_PIN 9

const char* ssid = "wifi_slow2";
const char* ntpServer = "192.168.1.1";
const long gmtOffset_sec = 28800;
const int daylightOffset_sec = 0;

IPAddress local_IP(192, 168, 1, 6);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);
Preferences preferences;

float voltage_low_cutoff_V;
float voltage_high_on_threshold_V;

float peak_v = 0;

unsigned long relay_total_on_ms = 0;
unsigned long relay_last_activation_ms = 0;

unsigned long debounce_delay_ms = 60000;
unsigned long debounce_timer_start = 0;
int last_stable_state = LOW;

bool is_online = false;
bool ntp_synced = false;
unsigned long last_wifi_check = 0;
const unsigned long wifi_check_interval = 30000; 

bool daily_reset_done = false;

const int MAX_LOGS = 10;
String eventLogs[MAX_LOGS];
int logCount = 0;

void addLog(String msg) {
  struct tm timeinfo;
  String timestamp = "[No Time] ";
  if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
    char buff[12];
    strftime(buff, sizeof(buff), "%H:%M:%S", &timeinfo);
    timestamp = "[" + String(buff) + "] ";
  }
  String entry = timestamp + msg;
  if (logCount < MAX_LOGS) {
    eventLogs[logCount] = entry;
    logCount++;
  } else {
    for (int i = 0; i < MAX_LOGS - 1; i++) {
      eventLogs[i] = eventLogs[i + 1];
    }
    eventLogs[MAX_LOGS - 1] = entry;
  }
}

void updateRelayTiming(int newState) {
  unsigned long now = millis();
  if (newState == HIGH && last_stable_state == LOW) {
    relay_last_activation_ms = now;
  } else if (newState == LOW && last_stable_state == HIGH) {
    if (relay_last_activation_ms > 0) {
      relay_total_on_ms += (now - relay_last_activation_ms);
    }
    relay_last_activation_ms = 0;
  }
}

String getRelayOnTimeString() {
  unsigned long current_session = 0;
  if (digitalRead(RELAY_PIN) == HIGH && relay_last_activation_ms > 0) {
    current_session = millis() - relay_last_activation_ms;
  }
  unsigned long total_ms = relay_total_on_ms + current_session;
  unsigned long total_secs = total_ms / 1000;
  int hours = total_secs / 3600;
  int mins = (total_secs % 3600) / 60;
  return String(hours) + "h " + String(mins) + "m";
}

void resetStats() {
  peak_v = 0;
  relay_total_on_ms = 0;
  if (digitalRead(RELAY_PIN) == HIGH) relay_last_activation_ms = millis();
  else relay_last_activation_ms = 0;
}

void loadSettings() {
  preferences.begin("solar_relay", true);
  voltage_low_cutoff_V = preferences.getFloat("v_low", 12.1);
  voltage_high_on_threshold_V = preferences.getFloat("v_high", 13.2);
  preferences.end();
}

String getTimeStringFull() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return "Time Not Synced";
  char timeStringBuff[20];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void checkDailyReset() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
    if (timeinfo.tm_hour == 5 && timeinfo.tm_min == 0 && !daily_reset_done) {
      resetStats();
      addLog("Daily 5AM Reset");
      daily_reset_done = true;
    } else if (timeinfo.tm_hour != 5) {
      daily_reset_done = false;
    }
  }
}

void handleApi() {
  float v = (ina219_found) ? ina219.getBusVoltage_V() : 0.0;

  String json = "{";
  json += "\"voltage\":" + String(v, 2) + ",";
  json += "\"peak_v\":" + String(peak_v, 2) + ",";
  json += "\"relay\":" + String(digitalRead(RELAY_PIN)) + ",";
  json += "\"uptime_relay\":\"" + getRelayOnTimeString() + "\",";
  json += "\"timestamp\":\"" + getTimeStringFull() + "\",";
  json += "\"logs\":[";
  for (int i = 0; i < logCount; i++) {
    json += "\"" + eventLogs[i] + "\"";
    if (i < logCount - 1) json += ",";
  }
  json += "]";
  json += "}";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleToggle() {
  if (server.hasArg("state")) {
    int s = server.arg("state").toInt();
    updateRelayTiming(s);
    digitalWrite(RELAY_PIN, s);
    last_stable_state = s;
    addLog("Manual -> " + String(s == HIGH ? "ON" : "OFF"));
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleResetPeaks() {
  resetStats();
  addLog("Stats Reset");
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
  html += "<button type='submit'>Save Changes</button></form></div>";
  html += "<p style='text-align:center'><a href='/'>Back Home</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleRoot() {
  float v = (ina219_found) ? ina219.getBusVoltage_V() : 0.0;
  int relayState = digitalRead(RELAY_PIN);
  
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;padding:15px;max-width:450px;margin:auto;background:#f4f4f4;}";
  html += ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:15px;}";
  html += ".status{font-weight:bold;}";
  html += "button{width:100%;padding:12px;background:#1976d2;color:white;border:none;border-radius:4px;cursor:pointer;margin-bottom:5px;}";
  html += ".btn-off{background:#c62828;} .btn-on{background:#2e7d32;}";
  html += ".peak{color:#d32f2f; font-size: 0.85em;}";
  html += ".log-box{background:#212121;color:#00e676;padding:10px;font-family:monospace;font-size:11px;height:150px;overflow-y:auto;border-radius:4px;}</style></head><body>";
  html += "<h1>Solar System</h1>";
  html += "<div class='card'><p>Time: <span id='time'>" + getTimeStringFull() + "</span></p>";
  html += "<p>Voltage: <b><span id='voltage'>" + String(v, 2) + " V</span></b> <span class='peak' id='peak'>(Peak: " + String(peak_v, 2) + ")</span></p>";
  html += "<p>Relay Status: <span class='status' id='status' style='color:" + String(relayState == HIGH ? "#2e7d32" : "#c62828") + ";'>" + String(relayState == HIGH ? "ACTIVE" : "INACTIVE") + "</span></p>";
  html += "<p>Relay On-Time: <b><span id='on-time'>" + getRelayOnTimeString() + "</span></b></p></div>";
  html += "<h2>Control</h2><div class='card'>";
  html += "<button class='btn-on' onclick=\"location.href='/toggle?state=1'\">FORCE ON</button>";
  html += "<button class='btn-off' onclick=\"location.href='/toggle?state=0'\">FORCE OFF</button></div>";
  html += "<h2>History</h2><div id='lb' class='log-box'>";
  for (int i = 0; i < logCount; i++) html += "<div>" + eventLogs[i] + "</div>";
  html += "</div>";
  html += "<script>";
  html += "function updateData(){";
  html += "  fetch('/api/data')";
  html += "    .then(r => r.json())";
  html += "    .then(data => {";
  html += "      document.getElementById('time').innerText = data.timestamp;";
  html += "      document.getElementById('voltage').innerText = data.voltage.toFixed(2) + ' V';";
  html += "      document.getElementById('peak').innerText = '(Peak: ' + data.peak_v.toFixed(2) + ')';";
  html += "      var st = document.getElementById('status');";
  html += "      if(data.relay == 1){";
  html += "        st.innerText = 'ACTIVE';";
  html += "        st.style.color = '#2e7d32';";
  html += "      }else{";
  html += "        st.innerText = 'INACTIVE';";
  html += "        st.style.color = '#c62828';";
  html += "      }";
  html += "      document.getElementById('on-time').innerText = data.uptime_relay;";
  html += "      var lb = document.getElementById('lb');";
  html += "      lb.innerHTML = '';";
  html += "      data.logs.forEach(log => {";
  html += "        var d = document.createElement('div');";
  html += "        d.innerText = log;";
  html += "        lb.appendChild(d);";
  html += "      });";
  html += "      lb.scrollTop = lb.scrollHeight;";
  html += "    });";
  html += "}";
  html += "setInterval(updateData, 3000);";
  html += "window.onload = function(){";
  html += "  var lb = document.getElementById('lb');";
  html += "  lb.scrollTop = lb.scrollHeight;";
  html += "};";
  html += "</script>";
  html += "<p style='text-align:center'><a href='/config'>Config</a> | <a href='/api/data'>API</a> | <a href='/update'>Update</a> | <a href='/'>Refresh</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("v_low")) voltage_low_cutoff_V = server.arg("v_low").toFloat();
  if (server.hasArg("v_high")) voltage_high_on_threshold_V = server.arg("v_high").toFloat();
  preferences.begin("solar_relay", false);
  preferences.putFloat("v_low", voltage_low_cutoff_V);
  preferences.putFloat("v_high", voltage_high_on_threshold_V);
  preferences.end();
  addLog("Settings updated");
  server.sendHeader("Location", "/");
  server.send(303);
}

void checkAndControlRelay() {
  if (millis() < 5000) return;
  static unsigned long last_read = 0;
  static unsigned long current_interval = 10000;
  unsigned long now_ms = millis();
  if (now_ms - last_read < current_interval) return;
  
  last_read = now_ms;

  if (!ina219_found) {
    ina219_found = ina219.begin();
    if (ina219_found) {
      Wire.beginTransmission(0x40);
      Wire.write(0x00);
      Wire.write(0x80);
      Wire.write(0x00);
      Wire.endTransmission();
      delay(5);
      ina219.begin();
    }
    if (!ina219_found) return;
  }
  
  float v = (ina219_found) ? ina219.getBusVoltage_V() : 0.0;

  if (v < 1.0) return;
  if (v > peak_v) peak_v = v;

  bool in_critical_zone = (abs(v - voltage_high_on_threshold_V) < 0.2) || (abs(v - voltage_low_cutoff_V) < 0.2);
  current_interval = in_critical_zone ? 2000 : 10000;

  int desired = last_stable_state;
  if (v <= voltage_low_cutoff_V) desired = HIGH;
  else if (v >= voltage_high_on_threshold_V) desired = LOW;

  if (desired != last_stable_state) {
    if (debounce_timer_start == 0) debounce_timer_start = millis();
    if (millis() - debounce_timer_start >= debounce_delay_ms) {
      updateRelayTiming(desired);
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
      server.on("/api/data", handleApi);
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
  esp_pm_config_esp32c3_t pm_config = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 80,
    .light_sleep_enable = false
  };
  esp_pm_configure(&pm_config);

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
  if (ina219_found) {
    Wire.beginTransmission(0x40);
    Wire.write(0x00);
    Wire.write(0x80);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(5);
    ina219.begin();
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
  if (ntp_synced) checkDailyReset();
  delay(20);
}
