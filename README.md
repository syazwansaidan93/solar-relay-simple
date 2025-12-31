# ESP32 Solar Relay Controller (INA219)

A lightweight **ESP32-based solar/battery relay controller** using the **INA219 current & voltage sensor**. The system automatically turns a relay **ON/OFF based on battery voltage thresholds**, provides a **web dashboard**, stores settings in **NVS**, and supports **deep sleep at night** to save power.

---

## Features

* Battery voltage & current monitoring via INA219
* External **RTC DS3231** support (timekeeping without internet)
* Automatic relay control with multi-condition logic

  * Relay ON when voltage ≥ high threshold **AND** current ≥ minimum load current
  * Relay OFF when voltage ≤ low cutoff
* Built-in web dashboard

  * Live voltage, current, power
  * Relay status (AUTO / MANUAL)
  * RTC temperature monitoring
  * Event history (last 15 events)
  * Adjustable voltage & current thresholds
* Manual relay override (Force ON / OFF)
* Persistent settings using ESP32 Preferences (NVS)
* NTP time synchronization with **RTC backup**
* Night-time deep sleep with smart delay (19:00 – 07:30)
* Low-power INA219 handling (power-down when idle)
* Offline-safe operation (relay logic works without WiFi)
* **Web-based OTA firmware update**

---

## Hardware Requirements

* ESP32 (tested with ESP32-C3)
* INA219 current & voltage sensor
* DS3231 RTC module
* Relay module (active HIGH)
* 12V battery / solar system

### Pin Configuration

| Component  | ESP32 Pin |
| ---------- | --------- |
| Component  | ESP32 Pin |
| ---------- | --------- |
| INA219 SDA | GPIO 8    |
| INA219 SCL | GPIO 9    |
| Relay IN   | GPIO 5    |

---

## Network Configuration

Default settings in the code:

* WiFi SSID: `wifi_slow`
* Static IP: `192.168.1.5`
* Gateway: `192.168.1.1`
* Subnet: `255.255.255.0`
* NTP Server: `192.168.1.1`
* Timezone: GMT +8 (Malaysia)

You may change these values directly in the source code.

---

## Control Logic

Relay behavior is determined by **both voltage and current** conditions.

| Condition                                     | Relay State            |
| --------------------------------------------- | ---------------------- |
| Voltage ≥ `v_high` **AND** Current ≥ `c_high` | ON                     |
| Voltage ≤ `v_low`                             | OFF                    |
| Otherwise                                     | Keep last stable state |

Default values:

* `v_low`: 12.1 V
* `v_high`: 13.2 V
* `c_high`: 150 mA

Relay switching is protected by a **60-second debounce delay** to avoid rapid toggling.

---

## Web Interface

After connecting to WiFi, open:

```
http://192.168.1.5/
```

### Web UI Displays

* Current date & time (NTP / RTC synced)
* RTC temperature sensor reading
* Battery voltage (V)
* Current (mA)
* Power (W)
* Relay state (ACTIVE / INACTIVE)
* Manual relay control buttons
* System event history
* Voltage & current threshold configuration
* **OTA firmware upload interface**

All settings are saved permanently to ESP32 flash memory.

---

## Power Saving Behavior

* INA219 enters power-down mode when idle
* ESP32 enters deep sleep **only after relay has been OFF for 1 hour at night**
* Night window: **19:00 – 07:30**
* Automatically wakes up at **7:30 AM**

---

## Offline Mode

If WiFi or NTP fails:

* Relay control continues working
* Voltage thresholds are still enforced
* Web server remains disabled
* ESP32 retries WiFi connection every 12 hours

---

## Libraries Used

* Adafruit_INA219
* RTClib (DS3231)
* WiFi
* WebServer
* Preferences
* ArduinoOTA
* Update
* Wire

---

## Typical Use Cases

* Solar battery protection
* Router or load auto cutoff
* Small off-grid solar systems
* DIY UPS or solar relay automation

---

## License

MIT License – free to use, modify, and distribute.
