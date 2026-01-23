# Solar Relay Controller (ESP32 + INA219)

This project is an **ESP32-based solar relay controller** designed for **12V solar battery systems**. It monitors **voltage, current, and power** using an INA219 sensor, controls a relay automatically based on configurable thresholds, provides a **web-based dashboard**, supports **OTA firmware updates**, and enters **deep sleep at night** to minimize power usage.

---

## ✨ Features

* 🔋 **INA219 monitoring** (Voltage, Current, Power)

* 🔁 **Automatic relay control** with debounce protection

* 🌐 **Web dashboard** (mobile-friendly, no external JS/CSS)

* 🧾 **Event log with timestamps**

* 📈 **Peak voltage / current / power tracking**

* ⚡ **Total energy accumulation (Wh)**

* ⚙️ **Runtime configuration via web UI**

* 🌙 **Night-time deep sleep scheduling**

* 🕒 **NTP time sync (router-supported)**

* 🔄 **OTA firmware update via browser**

* 💾 **Persistent configuration using ESP32 Preferences (NVS)**

* ⚡ **Power-optimized INA219 active / power-down control**

* 🔋 **INA219 monitoring** (Voltage, Current, Power)

* 🔁 **Automatic relay control** with debounce protection

* 🌐 **Web dashboard** (mobile-friendly, no external JS/CSS)

* 🧾 **Event log with timestamps**

* 📈 **Peak voltage / current / power tracking**

* ⚙️ **Runtime configuration via web UI**

* 🌙 **Night-time deep sleep scheduling**

* 🕒 **NTP time sync (router-supported)**

* 🔄 **OTA firmware update via browser**

* 💾 **Persistent configuration using ESP32 Preferences (NVS)**

* ⚡ **Power-optimized INA219 active / power-down control**

---

## 🧩 Hardware Requirements

* ESP32 (tested on ESP32-C3 class boards)
* INA219 current & voltage sensor (I2C)
* Relay module (**active HIGH**)
* 12V solar battery system

### Pin Mapping

| Function      | GPIO   |
| ------------- | ------ |
| Relay Control | GPIO 5 |
| INA219 SDA    | GPIO 8 |
| INA219 SCL    | GPIO 9 |

INA219 I2C Address: `0x40`

---

## ⚙️ Default Configuration

| Setting              | Default Value  |
| -------------------- | -------------- |
| Low voltage cutoff   | **12.1 V**     |
| High voltage ON      | **13.2 V**     |
| Minimum ON current   | **150 mA**     |
| Wake-up time         | **08:00**      |
| Relay debounce delay | **60 seconds** |
| Night start time     | **19:00**      |

All values can be modified via the web configuration page.

---

## 🌐 Web Interface

After connecting to WiFi, open:

```
http://192.168.1.5/
```

### Available Pages

| Path      | Description         |
| --------- | ------------------- |
| `/`       | Dashboard           |
| `/config` | Configuration page  |
| `/update` | OTA firmware upload |

### Dashboard Displays

* Current date & time (NTP synced)

* Live voltage (V), current (A), power (W)

* **Total accumulated energy (Wh)**

* Peak voltage, current, and power

* Relay status (ACTIVE / INACTIVE)

* Manual relay control buttons

* Event log history

* Current date & time (NTP synced)

* Live voltage (V), current (A), power (W)

* Peak voltage, current, and power

* Relay status (ACTIVE / INACTIVE)

* Manual relay control buttons

* Event log history

---

## 🔁 Relay Control Logic

The relay is evaluated periodically using this logic:

```text
IF voltage >= High Threshold
AND current >= Current Threshold
→ Relay ON

IF voltage <= Low Cutoff
→ Relay OFF
```

Additional protections:

* 60-second debounce before switching
* Adaptive sampling near threshold values
* Manual override via web UI

---

## ⚡ Energy Calculation (Wh)

The system continuously integrates power over time to calculate total energy:

```text
Energy (Wh) += Power (W) × Time Interval (hours)
```

Energy is:

* Calculated in `checkAndControlRelay()`
* Resettable from the web UI
* Stored in RAM (resets on reboot)

This allows basic daily or session-based solar energy tracking.

The relay is evaluated periodically using this logic:

```text
IF voltage >= High Threshold
AND current >= Current Threshold
→ Relay ON

IF voltage <= Low Cutoff
→ Relay OFF
```

Additional protections:

* 60-second debounce before switching
* Adaptive sampling near threshold values
* Manual override via web UI

---

## 🌙 Deep Sleep Logic

The ESP32 enters deep sleep when **all conditions** below are met:

* NTP time is synchronized
* Current time is night (≥ 19:00 or before wake time)
* Relay is OFF
* Condition persists for ≥ 60 seconds

Wake-up occurs automatically at the configured **wake time**.

Before sleeping:

* INA219 is set to **power-down mode**
* Relay is forced OFF

---

## 📶 WiFi Behavior

* Static IP: `192.168.1.5`
* Auto-reconnect enabled
* Reconnect attempt every 30 seconds
* Reduced TX power for lower consumption
* NTP server: `192.168.1.1`

---

## 🔄 OTA Firmware Update

* Upload `.bin` file via `/update`
* Automatic reboot after successful upload
* Intended for **trusted LAN environments** (no authentication)

---

## ⚡ Power Optimization Techniques

* INA219 manually toggled between ACTIVE / POWER-DOWN
* Reduced WiFi TX power
* Adaptive sensor sampling interval
* Automatic night deep sleep

---

## ⚠️ Notes & Warnings

* No authentication on web interface (LAN use only)
* Relay module must be **active HIGH**
* Not intended for safety-critical or certified power systems

---

## 📄 License

MIT License

---

## 🙌 Author

DIY solar relay controller using ESP32 + INA219.

Feel free to fork, modify, and improve.
