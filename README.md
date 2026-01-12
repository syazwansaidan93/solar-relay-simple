# Solar Relay Controller with INA219 (ESP32)

This project is an **ESP32-based solar power relay controller** designed to monitor **voltage, current, and power** using the **INA219 sensor**, automatically control a relay based on configurable thresholds, and provide a **web-based dashboard** with logging, configuration, OTA firmware updates, and deep sleep power saving.

The system is suitable for **solar battery systems**, **router/IoT power control**, or any low-power automation that requires intelligent ON/OFF switching based on real-time electrical measurements.

---

## ✨ Features

* 📊 **Real-time monitoring**

  * Bus Voltage (V)
  * Current (mA)
  * Power (W)
  * Peak value tracking (Voltage, Current, Power)

* 🔌 **Automatic Relay Control**

  * Turns **ON** when voltage and current exceed configured thresholds
  * Turns **OFF** when voltage drops below low cutoff
  * Software debounce (default: 60 seconds) to avoid relay chatter

* 🌐 **Web Dashboard**

  * Live readings
  * Manual relay ON/OFF control
  * Configuration page
  * Event history log (last 15 events)
  * Peak reset button

* ⚙️ **Persistent Configuration**

  * Stored using ESP32 `Preferences`
  * Survives reboot and power loss

* ⏱ **NTP Time Synchronization**

  * Uses local NTP server
  * Timestamped logs

* 🌙 **Night Mode Deep Sleep**

  * Automatically enters deep sleep at night (19:00 – 08:00)
  * Only when relay is OFF
  * Wakes automatically at 8:00 AM

* 🔄 **OTA Firmware Update**

  * Upload `.bin` file directly from the web UI

* 🔋 **Low Power Optimization**

  * INA219 is powered down between readings
  * WiFi TX power reduced

---

## 🧰 Hardware Requirements

* ESP32 (tested with ESP32-C3 / ESP32-S3 style pin mapping)
* INA219 current & voltage sensor
* Relay module (active HIGH)
* Solar panel + battery system (or any DC source)
* WiFi network

---

## 🔌 Pin Configuration

| Function           | GPIO   |
| ------------------ | ------ |
| Relay Control      | GPIO 5 |
| I2C SDA            | GPIO 8 |
| I2C SCL            | GPIO 9 |
| INA219 I2C Address | `0x40` |

---

## 🌍 Network Configuration

* **WiFi mode**: Station (STA)
* **Static IP**: `192.168.1.5`
* **Gateway**: `192.168.1.1`
* **Subnet**: `255.255.255.0`
* **NTP Server**: `192.168.1.1`
* **Timezone**: GMT +8 (Malaysia)

> You can modify these values directly in the source code if required.

---

## ⚙️ Configurable Parameters

These can be changed via the web UI:

| Parameter              | Description         | Default |
| ---------------------- | ------------------- | ------- |
| Low Voltage Cutoff     | Relay OFF threshold | 12.1 V  |
| High Voltage Threshold | Relay ON threshold  | 13.2 V  |
| Minimum Current for ON | Prevents false ON   | 150 mA  |

All settings are stored in **non-volatile memory (NVS)**.

---

## 🖥 Web Interface Endpoints

| Path              | Function            |
| ----------------- | ------------------- |
| `/`               | Main dashboard      |
| `/toggle?state=1` | Force relay ON      |
| `/toggle?state=0` | Force relay OFF     |
| `/save`           | Save configuration  |
| `/reset_peaks`    | Reset peak values   |
| `/update`         | OTA firmware upload |

---

## 🔁 Relay Control Logic

```text
IF voltage >= HIGH_THRESHOLD AND current >= CURRENT_THRESHOLD
    → Relay ON
ELSE IF voltage <= LOW_CUTOFF
    → Relay OFF
```

* Relay state must remain stable for **60 seconds** before switching
* Manual control overrides automatic logic

---

## 🌙 Deep Sleep Logic

* Active only when:

  * WiFi connected
  * NTP time synced
  * Night hours (19:00 – 08:00)
  * Relay is OFF for 30 minutes

* ESP32 sleeps until **08:00 AM**

* INA219 is powered down before sleep

---

## 📜 Event Logging

* Logs stored in RAM (last 15 entries)
* Includes:

  * WiFi status changes
  * Relay ON/OFF events
  * Manual actions
  * Configuration updates
  * Sleep events

---

## 🚀 Build & Upload

1. Install required libraries:

   * `Adafruit INA219`
   * ESP32 board support

2. Select correct board in Arduino IDE

3. Compile and upload via USB

4. Access dashboard:

   ```
   http://192.168.1.5/
   ```

---

## ⚠️ Notes & Recommendations

* Ensure relay module logic level matches ESP32 (3.3V)
* INA219 shunt rating must match expected current
* Always test thresholds before connecting critical loads

---

## 📄 License

This project is provided **as-is** for educational and personal use.
Modify and adapt freely for your own solar or automation projects.

---

## 🙌 Credits

Developed by **Syazwan Saidan**

ESP32 • INA219 • Solar Power Automation
