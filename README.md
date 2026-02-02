# Solar Relay Controller (ESP32 + INA219)

This project is a **smart solar relay controller** using **ESP32-C3** and **INA219 current/voltage sensor**. It automatically turns a relay ON/OFF based on battery voltage, charging current, and time schedule, with a built-in web dashboard and OTA firmware update.

---

## Features

* Real-time monitoring:

  * Voltage (V)
  * Current (A)
  * Power (W)
  * Energy (Wh)

* Smart relay logic:

  * Turns ON when battery is sufficiently charged
  * Turns OFF when battery drops too low
  * Debounced switching (stable state protection)

* Maintenance mode:

  * Every **15th day of month** forces relay OFF (full charge day)

* Night deep sleep:

  * Automatically sleeps at night
  * Wakes up at configurable time

* Web dashboard:

  * Live stats
  * Manual relay control
  * Event logs
  * Config page

* OTA firmware update via browser

---

## Hardware

| Component    | Description              |
| ------------ | ------------------------ |
| ESP32-C3     | Main controller          |
| INA219       | Voltage & current sensor |
| Relay Module | Controls load            |
| Battery      | Lead-acid / Li-ion       |
| Solar MPPT   | Battery charger          |

### Default Pins

```
Relay   -> GPIO 5
SDA     -> GPIO 8
SCL     -> GPIO 9
INA219  -> I2C (0x40)
```

---

## Network

Static IP:

```
IP:      192.168.1.5
Gateway: 192.168.1.1
Subnet:  255.255.255.0
```

Web interface:

```
http://192.168.1.5
```

---

## Relay Logic

Relay turns **ON** when:

* Voltage >= `High Threshold`
* Current >= `ON Current`

Relay turns **OFF** when:

* Voltage <= `Low Cutoff`

All switching uses **60 seconds debounce**.

---

## Deep Sleep Logic

Device sleeps when:

* After 7pm
* Relay is OFF
* Sleeps until configured wake time

This saves power at night.

---

## Maintenance Day

On every **15th of month**:

* Relay forced OFF
* Manual control disabled

Used for battery equalization / full charge.

---

## Web Pages

| URL       | Function     |
| --------- | ------------ |
| `/`       | Dashboard    |
| `/config` | Settings     |
| `/update` | OTA firmware |

---

## Configurable Settings

| Setting        | Default |
| -------------- | ------- |
| Low Cutoff     | 12.1V   |
| High Threshold | 13.2V   |
| ON Current     | 150mA   |
| Wake Hour      | 08:00   |

Stored in **ESP32 Preferences (NVS)**.

---

## Energy Tracking

Energy is calculated as:

```
Wh += Power(W) * Time(hours)
```

Resets manually from dashboard.

---

## OTA Update

Open:

```
http://192.168.1.5/update
```

Upload compiled `.bin` file.

Auto reboot after update.

---

## Power Optimizations

* CPU locked at 80 MHz
* INA219 powered down between reads
* WiFi TX power limited
* Deep sleep at night

---

## Flashing

Compile using:

* Arduino IDE
* ESP32 board package
* Target: **ESP32-C3**

Libraries required:

* Adafruit INA219
* WiFi
* WebServer

---

## Use Case

Perfect for:

* Solar battery load control
* Router power automation
* DIY solar UPS
* Off-grid monitoring

---

## Author

Built for long-term **24/7 solar automation** with zero cloud dependency.

Runs fully on local network.

---

## License

MIT / Free for personal projects.
