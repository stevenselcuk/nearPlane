# nearPlane: M5StickC Plus 2 ADS-B Aircraft Tracker

  <!-- Projenizin çalıştığı bir fotoğrafı buraya koymanız şiddetle tavsiye edilir. -->

Turn your M5StickC Plus 2 into a pocket-sized, real-time ADS-B aircraft tracker. This application connects to your local WiFi, fetches data on the nearest aircraft from the `adsb.lol` public API, and displays key flight information on its screen.

It features a simple, one-time web setup for your WiFi and location credentials, making it easy to get started without hardcoding any personal information.

## Features

- **Real-time Data:** Displays the closest aircraft's flight number, registration, and aircraft type.
- **Multi-Page Display:** Toggles between two pages to show different sets of data:
    - **Page 1:** Altitude, Speed (IAS/GS), and Distance.
    - **Page 2:** Heading, Vertical Speed, and Squawk code.
- **Easy Web-Based Setup:** A captive portal for initial configuration. No need to edit the code for your WiFi credentials or location.
- **Audible Alert:** Plays a short tone when a new aircraft is detected as the closest one.
- **Settings Reset:** A simple button-hold combination allows you to clear saved settings and re-enter setup mode.
- **User-Friendly Interface:** Clean, readable display with custom fonts.

## Hardware Requirements

- **M5StickC Plus 2**

## Software & Library Dependencies

This project is built using the Arduino IDE. You will need to install the ESP32 board support and the following libraries through the Arduino Library Manager:

1.  **M5Unified:** The core library for M5Stack devices.
2.  **ArduinoJson:** For parsing the data from the API.

(The `WebServer`, `WiFi`, `DNSServer`, `HTTPClient`, and `Preferences` libraries are typically included with the ESP32 core installation.)

## Installation & First-Time Setup

1.  **Install Libraries:** Open the Arduino IDE, go to `Sketch > Include Library > Manage Libraries...` and install `M5Unified` and `ArduinoJson`.
2.  **Upload Code:** Open the project sketch, select "M5StickC Plus 2" as your board, choose the correct COM port, and click Upload.
3.  **Enter Setup Mode:** After the first upload (or after a settings reset), the device will automatically start in **Setup Mode**. The screen will turn blue and display instructions.

     <!-- Kurulum ekranının bir fotoğrafı harika olurdu -->

4.  **Connect and Configure:**
    - On your phone or computer, connect to the WiFi network named **`ADSB-Tracker-Setup`**.
    - A captive portal page should automatically open in your browser. If it doesn't, manually navigate to **`http://192.168.4.1`**.
    - Fill out the form:
        - **WiFi Network (SSID):** Your home WiFi network name.
        - **WiFi Password:** Your WiFi password.
        - **Your Latitude/Longitude:** Your current location coordinates. You can easily find these on Google Maps.
        - **Scan Radius (km):** The distance around you to search for aircraft (e.g., 50 km).
    - Click **"Save & Reboot"**.

The device will save your settings and restart. It will then automatically connect to your specified WiFi network and begin tracking aircraft.

## How to Use

Once connected, the device will start fetching data.

- **Main Display:** The top of the screen always shows the flight number (callsign), aircraft registration, and type. The bottom section displays detailed data.
- **Switch Pages (Button A - Short Press):** Briefly press the large button on the front (BtnA) to cycle between the two data pages.
- **Reset Settings (Button A - Long Press):** If you need to change your WiFi network, location, or other settings, simply **press and hold** the front button (BtnA) for a few seconds. The device will erase its saved settings and reboot into **Setup Mode**.

## How It Works

The device operates in two distinct modes:

1.  **Configuration Mode:** When no settings are saved, it creates a WiFi Access Point (AP). A DNS server and a web server are started to serve the configuration page. When you save your settings, they are stored in the device's non-volatile memory (NVS) using the `Preferences` library.

2.  **Tracker Mode:** On startup, it reads the saved settings and connects to your WiFi. It then periodically (every 10-30 seconds) sends an HTTP GET request to the `api.adsb.lol` endpoint with your coordinates and radius. The returned JSON data is parsed, and the information for the closest aircraft is displayed on the screen. The polling interval is adjusted automatically based on whether aircraft are found or if API errors occur.

## Troubleshooting

- **"CONNECTION FAILED!" screen:** This means the device could not connect to the WiFi network using the credentials you provided. Double-check your SSID and password. Press and hold **BtnA** to reset and try again.
- **"No aircraft nearby." screen:** There might be no ADS-B equipped aircraft within your specified radius, or the API might not have any data for your area at the moment. Try increasing the scan radius in the setup.
- **"API ERROR" or "JSON PARSE ERROR":** This usually indicates a problem with the internet connection or a temporary issue with the `adsb.lol` service. The device will automatically try again after a minute.
