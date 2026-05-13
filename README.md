ESP32-C3 Solar Energy Monitor & Relay ControllerA high-efficiency, IoT-enabled solar monitoring system built for the ESP32-C3. This project provides real-time tracking of voltage, current, and power consumption using an INA219 sensor, featuring automated relay logic for load protection and a modern web dashboard.🚀 FeaturesReal-Time Monitoring: Tracks Voltage (V), Current (A), and Power (W).Automated Control: Logic-driven relay switching based on configurable voltage cutoffs and current thresholds.Deep Sleep Integration: Smart power management that enters deep sleep during night hours to conserve energy.Live Web Dashboard: A responsive, Tailwind CSS-powered dashboard that updates every 1 second.CORS Enabled API: Native JSON endpoint with Access-Control-Allow-Origin headers for easy integration with external web apps.Persistence: Uses ESP32 Preferences storage to save configuration settings across reboots.OTA Updates: Built-in web interface for wireless firmware updates.🛠 Hardware RequirementsMicrocontroller: ESP32-C3 (e.g., SuperMini or Xiao form factor).Sensor: INA219 High-Side DC Current Sensor.Relay: 5V or 3.3V Relay Module (controlled via GPIO 5).Power: High-efficiency buck converter (e.g., Mini 560 or MP1495) for 12V to 5V/3.3V conversion.Wiring DiagramComponentESP32-C3 PinINA219 SDAGPIO 8INA219 SCLGPIO 9Relay SignalGPIO 5I2C Power3.3V💻 Software Setup1. Firmware InstallationOpen solar_controller.ino in the Arduino IDE.Install required libraries:Adafruit INA219Update the ssid and ntpServer (or gateway IP) in the code.Upload to your ESP32-C3.2. Dashboard DeploymentOpen dashboard.html.Ensure the API_URL constant matches your ESP32's static IP (default is 192.168.1.5).Open the file in any modern web browser.📊 API ReferenceThe device serves a JSON object at http://<device-ip>/api/data:JSON{
  "voltage": 12.55,
  "current_ma": 450.2,
  "power_mw": 5650.0,
  "peak_v": 14.20,
  "peak_c_ma": 1200.0,
  "peak_p_mw": 15000.0,
  "energy_wh": 12.450,
  "relay": 1,
  "uptime_relay": "4h 20m",
  "timestamp": "2026-05-13 23:45:00"
}
⚙️ ConfigurationThe system can be configured via the built-in web portal at http://<device-ip>/config:Low Cutoff (V): The voltage at which the relay will disconnect to protect the battery.High Threshold (V): The voltage required to reconnect the relay.Wake Time: Scheduled time for the device to exit deep sleep mode.🛡 LicenseMIT License. Feel free to use and modify for your personal solar projects.
