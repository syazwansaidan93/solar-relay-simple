# Solar Relay Controller (ESP32 + INA219)

This project is an **ESP32-based solar power relay controller** with a built-in **web dashboard**, **automatic relay control**, **deep sleep scheduling**, and **OTA firmware update** support. It is designed for **solar battery systems** where relay activation depends on voltage and current thresholds, while minimizing power consumption during night hours.

---

## ✨ Features

* 🔋 **INA219 voltage, current & power monitoring**
* 🔁 **Automatic relay control** based on:

  * Battery voltage (low cutoff & high-on threshold)
  * Minimum charging current
* 🕒 **NTP time synchronization** (local router supported)
* 🌙 **Automatic deep sleep at night** to save power
* 🌐 **Web-based dashboard** (no external dependencies)
* 🛠 **Runtime configuration via web UI**
* 📈 **Peak voltage/current tracking**
* 🧾 **Event log history (ring buffer)**
* 🔄 **OTA firmware update via browser**
* 💾 **Persistent settings using NVS (Preferences)**

---

## 🧩 Hardware Requirements

* ESP32 (tested on ESP32-S3 class boards)
* INA219 current/voltage sensor (I2C)
* Relay module (active HIGH)
* Solar battery system (12V typical)

### Pin Mapping

| Function      | Pin    |
| ------------- | ------ |
| Relay control | GPIO 5 |
| INA219 SDA    | GPIO 8 |
| INA219 SCL    | GPIO 9 |

INA219 I2C address: `0x40`

---

## ⚙️ Default Configuration

| Setting                | Default    |
| ---------------------- | ---------- |
| Low voltage cutoff     | 12.1 V     |
| High voltage ON        | 13.2 V     |
| Min current to turn ON | 150 mA     |
| Wake-up time           | 08:00      |
| Debounce delay         | 60 seconds |
| Night sleep start      | 19:00      |

All values can be changed from the **web configuration page**.

---

## 🌐 Web Interface

Once connected to WiFi, open:

```
http://192.168.1.5/
```

### Pages

* `/` → Dashboard
* `/config` → Threshold & schedule configuration
* `/update` → OTA firmware upload

### Dashboard Shows

* Current date & time
* Last deep sleep time
* Voltage, current, relay status
* Peak voltage & current
* Manual relay control buttons
* Event log history

---

## 🔁 Relay Control Logic

Relay behavior is evaluated periodically:

```text
IF voltage >= HIGH threshold
AND current >= current threshold
→ Relay ON

IF voltage <= LOW cutoff
→ Relay OFF
```

Additional logic:

* Adaptive sampling rate near threshold
* 60-second debounce before relay state changes
* Manual override via web UI

---

## 🌙 Deep Sleep Logic

The ESP32 enters deep sleep when:

* NTP time is synced
* Time is night (≥ 19:00)
* Relay is OFF
* Condition persists for 5 minutes

It wakes automatically at the configured **wake-up time**.

INA219 is put into **power-down mode** before sleep to reduce consumption.

---

## 📶 WiFi Behavior

* Static IP: `192.168.1.5`
* Auto reconnect enabled
* Periodic reconnect attempts every 30s
* NTP server: `192.168.1.1`
* Reduced TX power for lower consumption

---

## 🔄 OTA Firmware Update

* Upload `.bin` file from `/update`
* Automatic reboot after successful update
* No authentication (intended for trusted LAN use)

---

## 🧠 Power Optimization Techniques

* INA219 manually switched between ACTIVE / POWER-DOWN
* Adaptive measurement intervals
* Deep sleep scheduling
* Reduced WiFi TX power

---

## ⚠️ Notes & Warnings

* No authentication on web UI (LAN-only usage recommended)
* Relay logic assumes **active HIGH relay module**
* Designed for **monitoring & control**, not safety-critical systems

---

## 📄 License

MIT License

---

## 🙌 Author

Created for DIY solar monitoring & relay automation using ESP32.

Feel free to fork, modify, and improve.
