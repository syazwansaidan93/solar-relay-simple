# Solar Monitor Live Dashboard

A lightweight, responsive web interface for real-time monitoring of your ESP32-C3 solar energy system. This dashboard provides a clean "glassmorphism" UI to track vital statistics and system health at a glance.

## ✨ Key Features

* **Real-Time Polling:** Fetches data from the ESP32 API every `1,000ms` (1 second).
* **Metric Conversion:** Automatically converts raw milliwatts (`mW`) and milliamperes (`mA`) from the sensor into standard Watts (`W`) and Amperes (`A`).
* **Visual Status Indicators:**

  * **Live Pulse:** A pulsing green indicator confirms the dashboard is actively refreshing.
  * **Relay Badge:** High-visibility status badge (`Active/Inactive`) with dynamic color coding.
* **Peak Tracking:** Displays daily peak voltage, current, and power directly under current readings for performance benchmarking.
* **Zero Dependencies:** Uses Tailwind CSS via CDN; no local installation or heavy JavaScript frameworks required.

## 🛠 Setup & Usage

### 1. Configure the API Endpoint

Open `dashboard.html` in a text editor and locate the `API_URL` constant in the `<script>` section:

```javascript
const API_URL = 'http://192.168.1.5/api/data';
```

Replace `192.168.1.5` with the actual IP address assigned to your ESP32-C3 on your local network.

### 2. Launching

Simply open the `dashboard.html` file in any modern web browser (Chrome, Firefox, Edge, or Safari).

> Note: Your viewing device (PC/Phone) must be connected to the same local network as the ESP32-C3.

## 🖥 Interface Overview

| Section           | Description                                                                |
| ----------------- | -------------------------------------------------------------------------- |
| Primary Metrics   | Large cards displaying current Voltage, Amperage, and Wattage.             |
| Peak Values       | Red sub-text showing the highest recorded value since the last reset.      |
| Relay Status      | Displays whether the load is currently engaged (ON) or disconnected (OFF). |
| System Info       | Shows cumulative Energy (Wh) and the device's internal NTP-synced clock.   |
| Connection Status | A header badge that turns red if the dashboard cannot reach the ESP32.     |

## 🔧 Troubleshooting

### "Offline / Error" Status

* Ensure the ESP32 is powered on and connected to WiFi.
* Verify the IP address in the script matches the device IP.
* Check that you have flashed the firmware with the CORS Header fix (`Access-Control-Allow-Origin: *`).

### Values show `--`

This indicates the fetch is failing or the JSON response is malformed.

Open the Browser Console (`F12`) to check for specific error messages.

## 🎨 Styling

The dashboard utilizes Tailwind CSS for layout and Glassmorphism effects. It is fully mobile-responsive, stacking the metric cards vertically on smaller screens for easy viewing on a smartphone.
