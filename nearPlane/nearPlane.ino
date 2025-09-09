#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "assets.h"

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

bool configMode = false;
String wifi_ssid, wifi_password, latitude, longitude, radius_km;
String api_url;
unsigned long lastPollTime = 0;
int pollInterval = 10000;

const int NUM_PAGES = 2;
int currentPage = 0;
JsonDocument doc;
String lastSeenAircraftReg = "";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>ADSB Tracker Setup</title><style>body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;margin:0;background-color:#f5f5f7;color:#1d1d1f}.container{padding:25px;max-width:550px;margin:30px auto;background-color:#fff;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,.1)}h1{color:#1d1d1f;text-align:center;margin-bottom:25px;font-weight:600}form{display:flex;flex-direction:column}label{margin-bottom:8px;color:#6e6e73;font-weight:500}input{padding:14px;margin-bottom:20px;border:1px solid #d2d2d7;border-radius:8px;font-size:16px;transition:border-color .2s,box-shadow .2s}input:focus{border-color:#007aff;box-shadow:0 0 0 3px rgba(0,122,255,.25);outline:none}button{background-color:#007aff;color:#fff;padding:16px;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;transition:background-color .2s}button:hover{background-color:#0056b3}.footer{text-align:center;margin-top:20px;color:#86868b;font-size:12px}</style></head><body><div class="container"><h1>ADSB Tracker Setup</h1><form action="/save" method="POST"><label for="ssid">WiFi Network (SSID)</label><input type="text" id="ssid" name="ssid" required><label for="password">WiFi Password</label><input type="text" id="password" name="password"><label for="lat">Your Latitude</label><input type="text" id="lat" name="lat" required placeholder="e.g., 41.015137"><label for="lon">Your Longitude</label><input type="text" id="lon" name="lon" required placeholder="e.g., 28.979530"><label for="radius">Scan Radius (km)</label><input type="number" id="radius" name="radius" value="50" required><button type="submit">Save & Reboot</button></form></div><div class="footer">M5StickC Plus 2 Edition</div></body></html>
)rawliteral";

void handleRoot();
void handleSave();
void handleNotFound();
void displayCurrentPage();
void startConfigMode();
void loadSettingsAndConnect();
void fetchAircraftData();
void playNewAircraftSound();

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg); // DÜZELTME: Hatalı satır kaldırıldı.

  // Ses sorununu gidermek için ek kontrol
  M5.Speaker.setVolume(255); // Ses seviyesini maksimuma ayarla
  M5.Speaker.tone(2000, 100); // Başlangıçta test sesi çal

  M5.Display.setRotation(1);
  M5.Display.fillScreen(BLACK);
  preferences.begin("adsb-config", false);

  if (M5.BtnA.isPressed()) {
    M5.Display.fillScreen(TFT_ORANGE);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("SETTINGS RESET!", 120, 67);
    preferences.clear();
    delay(2500);
    ESP.restart();
  }

  wifi_ssid = preferences.getString("ssid", "");
  if (wifi_ssid == "") {
    configMode = true;
    startConfigMode();
  } else {
    configMode = false;
    loadSettingsAndConnect();
  }
}

void loop() {
  M5.update();

  if (M5.BtnA.wasHold()) {
    M5.Display.fillScreen(TFT_ORANGE);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("SETTINGS RESET!", 120, 67);
    preferences.clear();
    delay(2500);
    ESP.restart();
  }

  if (M5.BtnA.wasPressed()) {
    currentPage = (currentPage + 1) % NUM_PAGES;
    if (!doc.isNull() && doc.containsKey("ac")) {
      displayCurrentPage();
    }
  }

  if (configMode) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else {
    if (WiFi.status() == WL_CONNECTED && (millis() - lastPollTime > pollInterval)) {
      fetchAircraftData();
      lastPollTime = millis();
    }
  }
}

void playNewAircraftSound() {
    M5.Speaker.tone(1400, 60);
    delay(70);
    M5.Speaker.tone(1800, 90);
}

void startConfigMode() {
  const char* ap_ssid = "ADSB-Tracker-Setup";
  M5.Display.fillScreen(TFT_BLUE);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(MC_DATUM);

  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.drawString("SETUP MODE", 120, 25);

  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setTextDatum(ML_DATUM);
  M5.Display.drawString("1. Connect to WiFi:", 15, 55);
  M5.Display.setTextColor(TFT_YELLOW);
  M5.Display.drawString(ap_ssid, 35, 75);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString("2. Open browser to:", 15, 95);
  M5.Display.setTextColor(TFT_YELLOW);
  M5.Display.drawString("192.168.4.1", 35, 115);

  WiFi.softAP(ap_ssid);
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loadSettingsAndConnect() {
  wifi_password = preferences.getString("password", "");
  latitude = preferences.getString("lat", "0.0");
  longitude = preferences.getString("lon", "0.0");
  radius_km = preferences.getString("radius", "50");
  api_url = "https://api.adsb.lol/v2/closest/" + latitude + "/" + longitude + "/" + radius_km;

  M5.Display.fillScreen(BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.drawString("Connecting to...", 120, 45);
  M5.Display.setFont(&fonts::FreeSansBold9pt7b);
  M5.Display.drawString(wifi_ssid, 120, 65);
  M5.Display.drawRect(30, 85, 180, 10, TFT_WHITE);

  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    M5.Display.fillRect(31, 86, (178 * attempts) / 30, 8, TFT_GREEN);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.drawString("CONNECTION FAILED!", 120, 45);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.drawString("Hold BtnA to reset.", 120, 90);
  } else {
    M5.Display.fillScreen(TFT_DARKGREEN);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.drawString("CONNECTED!", 120, 67);
    delay(2000);
  }
}

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleSave() {
  preferences.putString("ssid", server.arg("ssid"));
  preferences.putString("password", server.arg("password"));
  preferences.putString("lat", server.arg("lat"));
  preferences.putString("lon", server.arg("lon"));
  preferences.putString("radius", server.arg("radius"));
  server.send(200, "text/html", "<h1>Settings Saved!</h1><p>Device will reboot in 3 seconds.</p>");
  delay(3000);
  ESP.restart();
}

void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1", true);
  server.send(302, "text/plain", "");
}

void displayCurrentPage() {
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextDatum(TL_DATUM);

  if (doc.isNull() || !doc.containsKey("ac") || doc["ac"].as<JsonArray>().size() == 0) {
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setFont(&fonts::FreeSansBold12pt7b);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.drawString("No aircraft nearby.", 120, 67);
      return;
  }

  JsonObject closest = doc["ac"][0];
  String flight = closest["flight"] | "N/A";
  flight.trim();
  String reg = closest["r"] | "N/A";
  String type = closest["t"] | "N/A";

  M5.Display.setTextColor(TFT_YELLOW);
  M5.Display.setFont(&fonts::Orbitron_Light_32);
  M5.Display.setTextDatum(TC_DATUM);
  M5.Display.drawString(flight, 120, 5);

  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setTextDatum(TC_DATUM);
  M5.Display.drawString(reg + " (" + type + ")", 120, 45);

  M5.Display.drawLine(10, 65, 230, 65, TFT_DARKGREY);
  M5.Display.setTextDatum(TL_DATUM);

  if (currentPage == 0) {
    int alt_baro = closest["alt_baro"] | 0;
    int ias = closest["ias"] | 0;
    float gs = closest["gs"] | 0.0;
    float dist = closest["dst"] | 0.0;

    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.drawString("ALT", 15, 75);
    M5.Display.drawString((ias > 0) ? "IAS" : "GS", 15, 95);
    M5.Display.drawString("DIST", 15, 115);

    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(TR_DATUM);
    M5.Display.drawString(String(alt_baro) + " ft", 225, 75);
    M5.Display.drawString((ias > 0) ? String(ias) + " kt" : String(gs, 0) + " kt", 225, 95);
    M5.Display.drawString(String(dist, 1) + " km", 225, 115);
  } else if (currentPage == 1) {
    float heading = closest["true_heading"] | 0.0;
    int baro_rate = closest["baro_rate"] | 0;
    String squawk = closest["squawk"] | "----";

    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.drawString("HDG", 15, 75);
    M5.Display.drawString("V/S", 15, 95);
    M5.Display.drawString("SQUAWK", 15, 115);

    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(TR_DATUM);
    M5.Display.drawString(String(heading, 0) + " deg", 225, 75);
    M5.Display.drawString(String(baro_rate) + " ft/m", 225, 95);
    M5.Display.drawString(squawk, 225, 115);
  }

  M5.Display.setTextColor(TFT_DARKGREY);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextDatum(TL_DATUM); 
  M5.Display.drawString("P" + String(currentPage + 1), 5, 5);
}

void fetchAircraftData() {
  if (WiFi.status() != WL_CONNECTED) {
      return;
  }
  
  HTTPClient http;
  http.begin(api_url);
  http.setTimeout(8000);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    doc.clear();
    DeserializationError error = deserializeJson(doc, http.getStream());

    if (error) {
      M5.Display.fillScreen(TFT_DARKCYAN);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(MC_DATUM);
      M5.Display.setFont(&fonts::FreeSansBold12pt7b);
      M5.Display.drawString("JSON PARSE ERROR", 120, 67);
      pollInterval = 60000;
    } else if (doc.containsKey("ac") && doc["ac"].as<JsonArray>().size() > 0) {
      String currentAircraftReg = doc["ac"][0]["r"] | "N/A";
      if (currentAircraftReg != "N/A" && currentAircraftReg != lastSeenAircraftReg) {
          playNewAircraftSound();
      }
      lastSeenAircraftReg = currentAircraftReg;
      displayCurrentPage();
      pollInterval = 10000;
    } else {
      lastSeenAircraftReg = "";
      doc.clear();
      displayCurrentPage();
      pollInterval = 30000;
    }
  } else {
    doc.clear();
    lastSeenAircraftReg = "";
    M5.Display.fillScreen(TFT_MAROON);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.drawString("API ERROR", 120, 50);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.drawString("HTTP Code: " + String(httpCode), 120, 80);
    pollInterval = 60000;
  }
  http.end();
}