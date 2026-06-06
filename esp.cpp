#include <Wire.h>
#include <INA226.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <Update.h>

INA226 ina(0x40);
bool ina226_found = false;

#define RELAY1_PIN 5
#define RELAY2_PIN 6
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

float v_low_1;
float v_high_1;
float c_high_1;

float v_low_2;
float v_high_2;

float peak_v = 0, peak_c = 0, peak_p = 0;
String peak_v_time = "N/A";
String peak_c_time = "N/A";
float total_Wh = 0;

unsigned long relay1_total_on_ms = 0;
unsigned long relay1_last_activation_ms = 0;
unsigned long relay2_total_on_ms = 0;
unsigned long relay2_last_activation_ms = 0;

unsigned long debounce1_delay_ms = 60000;
unsigned long debounce1_timer_start = 0;
int last_stable_state_1 = LOW;

unsigned long debounce2_delay_ms = 10000;
unsigned long debounce2_timer_start = 0;
int last_stable_state_2 = LOW;

float v_filtered = -1.0;
float c_filtered = -1.0;

bool is_online = false;
bool ntp_synced = false;
unsigned long last_wifi_check = 0;
const unsigned long wifi_check_interval = 30000;

bool daily_reset_done = false;

const int MAX_LOGS = 10;
String eventLogs[MAX_LOGS];
int logCount = 0;

float getBatteryPercentage(float v, float c_mA) {
  float low = v_low_1;
  float high = (c_mA > 50.0) ? 14.7 : 12.85;
  if (v <= low) return 0.0;
  if (v >= high) return 100.0;
  return ((v - low) / (high - low)) * 100.0;
}

String getTimeStringShort() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return "";
  char buff[12];
  strftime(buff, sizeof(buff), "%H:%M:%S", &timeinfo);
  return String(buff);
}

void addLog(String msg) {
  struct tm timeinfo;
  String timestamp = "[No Time] ";
  if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
    char buff[20];
    strftime(buff, sizeof(buff), "%d/%m %H:%M:%S", &timeinfo);
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

void updateRelayTiming(int relay, int newState) {
  unsigned long now = millis();
  if (relay == 1) {
    if (newState == HIGH && last_stable_state_1 == LOW) {
      relay1_last_activation_ms = now;
    } else if (newState == LOW && last_stable_state_1 == HIGH) {
      if (relay1_last_activation_ms > 0) {
        relay1_total_on_ms += (now - relay1_last_activation_ms);
      }
      relay1_last_activation_ms = 0;
    }
  } else if (relay == 2) {
    if (newState == HIGH && last_stable_state_2 == LOW) {
      relay2_last_activation_ms = now;
    } else if (newState == LOW && last_stable_state_2 == HIGH) {
      if (relay2_last_activation_ms > 0) {
        relay2_total_on_ms += (now - relay2_last_activation_ms);
      }
      relay2_last_activation_ms = 0;
    }
  }
}

String getRelayOnTimeString(int relay) {
  unsigned long total_ms = 0;
  if (relay == 1) {
    unsigned long current_session = 0;
    if (digitalRead(RELAY1_PIN) == HIGH && relay1_last_activation_ms > 0) {
      current_session = millis() - relay1_last_activation_ms;
    }
    total_ms = relay1_total_on_ms + current_session;
  } else if (relay == 2) {
    unsigned long current_session = 0;
    if (digitalRead(RELAY2_PIN) == HIGH && relay2_last_activation_ms > 0) {
      current_session = millis() - relay2_last_activation_ms;
    }
    total_ms = relay2_total_on_ms + current_session;
  }
  unsigned long total_secs = total_ms / 1000;
  int hours = total_secs / 3600;
  int mins = (total_secs % 3600) / 60;
  return String(hours) + "h " + String(mins) + "m";
}

void resetStats() {
  peak_v = 0; peak_c = 0; peak_p = 0; total_Wh = 0;
  peak_v_time = "N/A";
  peak_c_time = "N/A";
  relay1_total_on_ms = 0;
  if (digitalRead(RELAY1_PIN) == HIGH) relay1_last_activation_ms = millis();
  else relay1_last_activation_ms = 0;
  relay2_total_on_ms = 0;
  if (digitalRead(RELAY2_PIN) == HIGH) relay2_last_activation_ms = millis();
  else relay2_last_activation_ms = 0;
}

void loadSettings() {
  preferences.begin("solar_relay", true);
  v_low_1 = preferences.getFloat("v_low", 12.1);
  v_high_1 = preferences.getFloat("v_high", 13.2);
  c_high_1 = preferences.getFloat("c_high", 150.0);
  last_stable_state_1 = preferences.getInt("r_state", LOW);
  v_low_2 = preferences.getFloat("v_low_2", 12.1);
  v_high_2 = preferences.getFloat("v_high_2", 13.2);
  last_stable_state_2 = preferences.getInt("r_state_2", LOW);
  preferences.end();
}

void saveRelayState(int relay, int state) {
  preferences.begin("solar_relay", false);
  if (relay == 1) {
    last_stable_state_1 = state;
    preferences.putInt("r_state", state);
  } else if (relay == 2) {
    last_stable_state_2 = state;
    preferences.putInt("r_state_2", state);
  }
  preferences.end();
}

String getTimeStringFull() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || timeinfo.tm_year < 120) return "Time Not Synced";
  char timeStringBuff[25];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void checkDailyReset() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) {
    if (timeinfo.tm_hour == 6 && timeinfo.tm_min == 30 && !daily_reset_done) {
      resetStats();
      addLog("Daily 6:30AM Reset");
      daily_reset_done = true;
    } else if (timeinfo.tm_hour != 6 || timeinfo.tm_min != 30) {
      daily_reset_done = false;
    }
  }
}

void handleApi() {
  float v = (ina226_found) ? ina.getBusVoltage() : 0.0;
  float c_mA = (ina226_found) ? ina.getCurrent_mA() : 0.0;
  float p_mW = (ina226_found) ? ina.getPower_mW() : 0.0;

  float b_pct = getBatteryPercentage(v, c_mA);

  String json = "{";
  json += "\"voltage\":" + String(v, 2) + ",";
  json += "\"current_ma\":" + String(c_mA, 1) + ",";
  json += "\"power_mw\":" + String(p_mW, 1) + ",";
  json += "\"peak_v\":" + String(peak_v, 2) + ",";
  json += "\"peak_v_time\":\"" + peak_v_time + "\",";
  json += "\"peak_c_ma\":" + String(peak_c, 1) + ",";
  json += "\"peak_c_time\":\"" + peak_c_time + "\",";
  json += "\"peak_p_mw\":" + String(peak_p, 1) + ",";
  json += "\"energy_wh\":" + String(total_Wh, 3) + ",";
  json += "\"battery_pct\":" + String(b_pct, 1) + ",";
  json += "\"is_charging\":" + String((c_mA > 50.0) ? 1 : 0) + ",";
  json += "\"relay\":" + String(digitalRead(RELAY1_PIN)) + ",";
  json += "\"uptime_relay\":\"" + getRelayOnTimeString(1) + "\",";
  json += "\"relay2\":" + String(digitalRead(RELAY2_PIN)) + ",";
  json += "\"uptime_relay2\":\"" + getRelayOnTimeString(2) + "\",";
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
  if (server.hasArg("relay") && server.hasArg("state")) {
    int r = server.arg("relay").toInt();
    int s = server.arg("state").toInt();
    if (r == 1) {
      updateRelayTiming(1, s);
      digitalWrite(RELAY1_PIN, s);
      saveRelayState(1, s);
      addLog("Manual R1 -> " + String(s == HIGH ? "ON" : "OFF"));
    } else if (r == 2) {
      updateRelayTiming(2, s);
      digitalWrite(RELAY2_PIN, s);
      saveRelayState(2, s);
      addLog("Manual R2 -> " + String(s == HIGH ? "ON" : "OFF"));
    }
  }
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
  
  html += "<h3>Relay 1 Configuration</h3>";
  html += "Low Cutoff (V): <input type='number' step='0.1' name='v_low' value='" + String(v_low_1, 1) + "'>";
  html += "High Threshold (V): <input type='number' step='0.1' name='v_high' value='" + String(v_high_1, 1) + "'>";
  html += "ON Current (mA): <input type='number' step='1' name='c_high' value='" + String(c_high_1, 0) + "'>";
  
  html += "<h3>Relay 2 Configuration</h3>";
  html += "Low Cutoff (V): <input type='number' step='0.1' name='v_low_2' value='" + String(v_low_2, 1) + "'>";
  html += "High Threshold (V): <input type='number' step='0.1' name='v_high_2' value='" + String(v_high_2, 1) + "'>";
  
  html += "<button type='submit'>Save Changes</button></form></div>";
  html += "<p style='text-align:center'><a href='/'>Back Home</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleRoot() {
  float v = (ina226_found) ? ina.getBusVoltage() : 0.0;
  float c_mA = (ina226_found) ? ina.getCurrent_mA() : 0.0;
  float p_mW = (ina226_found) ? ina.getPower_mW() : 0.0;
  
  float current_A = c_mA / 1000.0;
  float power_W = p_mW / 1000.0;
  float peak_c_display = peak_c / 1000.0;
  float peak_p_display = peak_p / 1000.0;
  
  int relayState1 = digitalRead(RELAY1_PIN);
  int relayState2 = digitalRead(RELAY2_PIN);
  float b_pct = getBatteryPercentage(v, c_mA);
  bool is_charging = (c_mA > 50.0);
  
  String barColor = "#2e7d32";
  if (is_charging) barColor = "#0288d1";
  else if (b_pct < 25.0) barColor = "#c62828";
  else if (b_pct < 60.0) barColor = "#ef6c00";

  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;padding:15px;max-width:450px;margin:auto;background:#f4f4f4;}";
  html += ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:15px;}";
  html += ".status{font-weight:bold;}";
  html += "button{width:100%;padding:12px;color:white;border:none;border-radius:4px;cursor:pointer;margin-bottom:5px;}";
  html += ".btn-off{background:#c62828;} .btn-on{background:#2e7d32;} .btn-reset{background:#757575; font-size:12px; padding:8px;}";
  html += ".peak{color:#d32f2f; font-size: 0.85em;}";
  html += ".log-box{background:#212121;color:#00e676;padding:10px;font-family:monospace;font-size:11px;height:150px;overflow-y:auto;border-radius:4px;}</style></head><body>";
  html += "<h1>Solar System</h1>";
  html += "<div class='card'><p>Time: <span id='val-time'>" + getTimeStringFull() + "</span></p>";
  
  String batDisplay = String(b_pct, 0) + "%";
  if (is_charging) batDisplay += " ⚡";
  html += "<p>Battery Capacity: <b><span id='val-bat-pct'>" + batDisplay + "</span></b></p>";
  html += "<div style='background:#e0e0e0;border-radius:4px;height:14px;width:100%;margin-bottom:15px;overflow:hidden;'>";
  html += "<div id='val-bat-bar' style='background:" + barColor + ";height:100%;width:" + String(b_pct, 0) + "%;transition:width 0.3s;'></div>";
  html += "</div>";

  html += "<p>Voltage: <b><span id='val-v'>" + String(v, 2) + "</span> V</b> <span class='peak'>(Peak: <span id='val-peak-v'>" + String(peak_v, 2) + "</span> @ <span id='val-peak-v-time'>" + peak_v_time + "</span>)</span></p>";
  html += "<p>Current: <b><span id='val-c'>" + String(current_A, 2) + "</span> A</b> <span class='peak'>(Peak: <span id='val-peak-c'>" + String(peak_c_display, 2) + "</span> @ <span id='val-peak-c-time'>" + peak_c_time + "</span>)</span></p>";
  html += "<p>Power: <b><span id='val-p'>" + String(power_W, 2) + "</span> W</b> <span class='peak'>(Peak: <span id='val-peak-p'>" + String(peak_p_display, 2) + "</span>)</span></p>";
  html += "<p>Energy: <b><span id='val-wh'>" + String(total_Wh, 3) + "</span> Wh</b></p>";
  
  String rStatusText1 = relayState1 == HIGH ? "ACTIVE" : "INACTIVE";
  String rStatusColor1 = relayState1 == HIGH ? "#2e7d32" : "#c62828";
  html += "<p>Relay 1 Status: <span id='val-relay-status' class='status' style='color:" + rStatusColor1 + ";'>" + rStatusText1 + "</span></p>";
  html += "<p>Relay 1 On-Time: <b><span id='val-relay-time'>" + getRelayOnTimeString(1) + "</span></b></p>";

  String rStatusText2 = relayState2 == HIGH ? "ACTIVE" : "INACTIVE";
  String rStatusColor2 = relayState2 == HIGH ? "#2e7d32" : "#c62828";
  html += "<p>Relay 2 Status: <span id='val-relay2-status' class='status' style='color:" + rStatusColor2 + ";'>" + rStatusText2 + "</span></p>";
  html += "<p>Relay 2 On-Time: <b><span id='val-relay2-time'>" + getRelayOnTimeString(2) + "</span></b></p></div>";

  html += "<h2>Relay 1 Control</h2><div class='card'>";
  html += "<button class='btn-on' onclick=\"location.href='/toggle?relay=1&state=1'\">FORCE ON</button>";
  html += "<button class='btn-off' onclick=\"location.href='/toggle?relay=1&state=0'\">FORCE OFF</button></div>";

  html += "<h2>Relay 2 Control</h2><div class='card'>";
  html += "<button class='btn-on' onclick=\"location.href='/toggle?relay=2&state=1'\">FORCE ON</button>";
  html += "<button class='btn-off' onclick=\"location.href='/toggle?relay=2&state=0'\">FORCE OFF</button></div>";

  html += "<h2>History</h2><div id='lb' class='log-box'>";
  for (int i = 0; i < logCount; i++) html += "<div>" + eventLogs[i] + "</div>";
  html += "</div>";
  
  html += "<script>";
  html += "var b=document.getElementById('lb');b.scrollTop=b.scrollHeight;";
  html += "function updateData(){";
  html += "  fetch('/api/data')";
  html += "    .then(function(r){return r.json();})";
  html += "    .then(function(data){";
  html += "      document.getElementById('val-time').innerText = data.timestamp;";
  html += "      document.getElementById('val-bat-pct').innerText = data.battery_pct.toFixed(0) + (data.is_charging === 1 ? '% ⚡' : '%');";
  html += "      var bar = document.getElementById('val-bat-bar');";
  html += "      bar.style.width = data.battery_pct.toFixed(0) + '%';";
  html += "      var color = '#2e7d32';";
  html += "      if(data.is_charging === 1) color = '#0288d1';";
  html += "      else if(data.battery_pct < 25.0) color = '#c62828';";
  html += "      else if(data.battery_pct < 60.0) color = '#ef6c00';";
  html += "      bar.style.backgroundColor = color;";
  html += "      document.getElementById('val-v').innerText = data.voltage.toFixed(2);";
  html += "      document.getElementById('val-peak-v').innerText = data.peak_v.toFixed(2);";
  html += "      document.getElementById('val-peak-v-time').innerText = data.peak_v_time;";
  html += "      document.getElementById('val-c').innerText = (data.current_ma / 1000.0).toFixed(2);";
  html += "      document.getElementById('val-peak-c').innerText = (data.peak_c_ma / 1000.0).toFixed(2);";
  html += "      document.getElementById('val-peak-c-time').innerText = data.peak_c_time;";
  html += "      document.getElementById('val-p').innerText = (data.power_mw / 1000.0).toFixed(2);";
  html += "      document.getElementById('val-peak-p').innerText = (data.peak_p_mw / 1000.0).toFixed(2);";
  html += "      document.getElementById('val-wh').innerText = data.energy_wh.toFixed(3);";
  
  html += "      var rStatus = document.getElementById('val-relay-status');";
  html += "      rStatus.innerText = data.relay === 1 ? 'ACTIVE' : 'INACTIVE';";
  html += "      rStatus.style.color = data.relay === 1 ? '#2e7d32' : '#c62828';";
  html += "      document.getElementById('val-relay-time').innerText = data.uptime_relay;";
  
  html += "      var rStatus2 = document.getElementById('val-relay2-status');";
  html += "      rStatus2.innerText = data.relay2 === 1 ? 'ACTIVE' : 'INACTIVE';";
  html += "      rStatus2.style.color = data.relay2 === 1 ? '#2e7d32' : '#c62828';";
  html += "      document.getElementById('val-relay2-time').innerText = data.uptime_relay2;";
  
  html += "      if (data.logs) {";
  html += "        var lb = document.getElementById('lb');";
  html += "        lb.innerHTML = '';";
  html += "        data.logs.forEach(function(log){";
  html += "          var d = document.createElement('div');";
  html += "          d.innerText = log;";
  html += "          lb.appendChild(d);";
  html += "        });";
  html += "        lb.scrollTop = lb.scrollHeight;";
  html += "      }";
  html += "    });";
  html += "}";
  html += "setInterval(updateData, 2000);";
  html += "</script>";
  
  html += "<p style='text-align:center'><a href='/config'>Config</a> | <a href='/api/data'>API</a> | <a href='/update'>Update</a> | <a href='/'>Refresh</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("v_low")) v_low_1 = server.arg("v_low").toFloat();
  if (server.hasArg("v_high")) v_high_1 = server.arg("v_high").toFloat();
  if (server.hasArg("c_high")) c_high_1 = server.arg("c_high").toFloat();
  if (server.hasArg("v_low_2")) v_low_2 = server.arg("v_low_2").toFloat();
  if (server.hasArg("v_high_2")) v_high_2 = server.arg("v_high_2").toFloat();
  
  preferences.begin("solar_relay", false);
  preferences.putFloat("v_low", v_low_1);
  preferences.putFloat("v_high", v_high_1);
  preferences.putFloat("c_high", c_high_1);
  preferences.putFloat("v_low_2", v_low_2);
  preferences.putFloat("v_high_2", v_high_2);
  preferences.end();
  addLog("Settings updated");
  server.sendHeader("Location", "/");
  server.send(303);
}

void checkAndControlRelay() {
  if (millis() < 5000) return;
  static unsigned long last_read = 0;
  static unsigned long current_interval = 2000;
  unsigned long now_ms = millis();
  if (now_ms - last_read < current_interval) return;
  
  float time_diff_hours = (now_ms - last_read) / 3600000.0;
  last_read = now_ms;

  if (!ina226_found) {
    ina226_found = ina.begin();
    if (ina226_found) {
      ina.reset();
      ina.setMaxCurrentShunt(8.0, 0.010);
    } else {
      return;
    }
  }
  
  float v = (ina226_found) ? ina.getBusVoltage() : 0.0;
  float c = (ina226_found) ? ina.getCurrent_mA() : 0.0;
  float p = (ina226_found) ? ina.getPower_mW() : 0.0;

  if (v < 1.0) return;
  
  if (v > peak_v) {
    peak_v = v;
    String t = getTimeStringShort();
    peak_v_time = t.length() > 0 ? t : "N/A";
  }
  if (c > peak_c) {
    peak_c = c;
    String t = getTimeStringShort();
    peak_c_time = t.length() > 0 ? t : "N/A";
  }
  if (p > peak_p) peak_p = p;
  
  total_Wh += (p / 1000.0) * time_diff_hours;

  if (v_filtered < 0.0) {
    v_filtered = v;
    c_filtered = c;
  } else {
    v_filtered = (v_filtered * 0.8) + (v * 0.2);
    c_filtered = (c_filtered * 0.8) + (c * 0.2);
  }

  bool in_critical_zone = (abs(v_filtered - v_high_1) < 0.2) || (abs(v_filtered - v_low_1) < 0.2) || (abs(v_filtered - v_high_2) < 0.2) || (abs(v_filtered - v_low_2) < 0.2);
  current_interval = in_critical_zone ? 1000 : 2000;

  int desired_1 = last_stable_state_1;
  if (v_filtered <= v_low_1) desired_1 = HIGH;
  else if (v_filtered >= v_high_1 && c_filtered >= c_high_1) desired_1 = LOW;

  if (desired_1 != last_stable_state_1) {
    if (debounce1_timer_start == 0) debounce1_timer_start = millis();
    if (millis() - debounce1_timer_start >= debounce1_delay_ms) {
      updateRelayTiming(1, desired_1);
      digitalWrite(RELAY1_PIN, desired_1);
      saveRelayState(1, desired_1);
      debounce1_timer_start = 0;
      addLog("Relay1 -> " + String(desired_1 == HIGH ? "ON" : "OFF"));
    }
  } else {
    debounce1_timer_start = 0;
  }

  int desired_2 = last_stable_state_2;
  if (v_filtered <= v_low_2) desired_2 = LOW;
  else if (v_filtered >= v_high_2) desired_2 = HIGH;

  if (desired_2 != last_stable_state_2) {
    if (debounce2_timer_start == 0) debounce2_timer_start = millis();
    if (millis() - debounce2_timer_start >= debounce2_delay_ms) {
      updateRelayTiming(2, desired_2);
      digitalWrite(RELAY2_PIN, desired_2);
      saveRelayState(2, desired_2);
      debounce2_timer_start = 0;
      addLog("Relay2 -> " + String(desired_2 == HIGH ? "ON" : "OFF"));
    }
  } else {
    debounce2_timer_start = 0;
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
  loadSettings();

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, last_stable_state_1);
  digitalWrite(RELAY2_PIN, last_stable_state_2);
  
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
  
  ina226_found = ina.begin();
  if (ina226_found) {
    ina.reset();
    ina.setMaxCurrentShunt(8.0, 0.010);
  }
  
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
