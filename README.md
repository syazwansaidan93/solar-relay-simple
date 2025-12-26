# ESP32 Solar Relay Controller (INA219)

A lightweight **ESP32-based solar/battery relay controller** using the **INA219 current & voltage sensor**. The system automatically turns a relay **ON/OFF based on battery voltage thresholds**, provides a **web dashboard**, stores settings in **NVS**, and supports **deep sleep at night** to save power.

---

## Features

- Battery voltage monitoring via INA219
- Automatic relay control
  - Relay ON when voltage ≥ high threshold
  - Relay OFF when voltage ≤ low cutoff
- Built-in web dashboard
  - Live voltage, current, power
  - Relay status
  - Event log (last 20 events)
  - Adjustable voltage thresholds
- Persistent settings using ESP32 Preferences (NVS)
- NTP time synchronization
- Night-time deep sleep (00:00 – 07:00)
- Low-power INA219 handling (power-down when idle)
- Offline-safe operation (relay logic works without WiFi)

---

## Hardware Requirements

- ESP32 (tested with ESP32-C3 / ESP32-S3)
- INA219 current & voltage sensor
- Relay module (active HIGH)
- 12V battery / solar system

### Pin Configuration

| Component  | ESP32 Pin |
| ---------- | --------- |
| INA219 SDA | GPIO 6    |
| INA219 SCL | GPIO 7    |
| Relay IN   | GPIO 5    |

---

## Network Configuration

Default settings in the code:

- WiFi SSID: `wifi_slow`
- Static IP: `192.168.1.5`
- Gateway: `192.168.1.1`
- Subnet: `255.255.255.0`
- NTP Server: `192.168.1.1`
- Timezone: GMT +8 (Malaysia)

You may change these values directly in the source code.

---

## Voltage Control Logic

| Condition          | Relay State     |
| ------------------ | --------------- |
| Voltage ≥ `v_high` | ON              |
| Voltage ≤ `v_low`  | OFF             |
| Between thresholds | Keep last state |

Default values:

- `v_low`: 12.1 V
- `v_high`: 13.2 V

Relay switching is protected by a **60-second debounce delay** to avoid rapid toggling.

---

## Web Interface

After connecting to WiFi, open:

```
http://192.168.1.5/
```

### Web UI Displays

- Current date & time (NTP synced)
- Battery voltage (V)
- Current (mA)
- Power (W)
- Relay status
- System event logs
- Voltage threshold configuration

All settings are saved permanently to ESP32 flash memory.

---

## Power Saving Behavior

- INA219 enters power-down mode when idle
- ESP32 enters deep sleep between **12:00 AM – 7:00 AM**
- Automatically wakes up at **7:00 AM**

---

## Offline Mode

If WiFi or NTP fails:

- Relay control continues working
- Voltage thresholds are still enforced
- Web server remains disabled
- ESP32 retries WiFi connection every 12 hours

---

## Libraries Used

- Adafruit\_INA219
- WiFi
- WebServer
- Preferences
- Wire

---

## Typical Use Cases

- Solar battery protection
- Router or load auto cutoff
- Small off-grid solar systems
- DIY UPS or solar relay automation

---

## License

MIT License – free to use, modify, and distribute.

