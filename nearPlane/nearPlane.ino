#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;
M5Canvas canvas(&M5.Display);

const int NUM_PAGES = 6;
unsigned long pollInterval = 2000;
const unsigned long SHORT_POLL_INTERVAL = 2000;
const unsigned long NO_AIRCRAFT_POLL_INTERVAL = 10000;
const unsigned long ERROR_POLL_INTERVAL = 60000;
const long RESET_HOLD_TIME_MS = 5000;

bool configMode = false;
String wifi_ssid, wifi_password, latitude, longitude, radius_km;
String api_url;
unsigned long lastPollTime = 0;
int currentPage = 0;
JsonDocument doc;
String lastSeenAircraftReg = "";

unsigned long btnB_press_start_time = 0;
bool isResetting = false;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>nearPlane Tracker Setup</title><style>body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;margin:0;background-color:#f5f5f7;color:#1d1d1f}.container{padding:25px;max-width:550px;margin:30px auto;background-color:#fff;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,.1)}h1{color:#1d1d1f;text-align:center;margin-bottom:25px;font-weight:600}form{display:flex;flex-direction:column}label{margin-bottom:8px;color:#6e6e73;font-weight:500}input{padding:14px;margin-bottom:20px;border:1px solid #d2d2d7;border-radius:8px;font-size:16px;transition:border-color .2s,box-shadow .2s}input:focus{border-color:#007aff;box-shadow:0 0 0 3px rgba(0,122,255,.25);outline:none}button{background-color:#007aff;color:#fff;padding:16px;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;transition:background-color .2s}button:hover{background-color:#0056b3}.footer{text-align:center;margin-top:20px;color:#86868b;font-size:12px}</style></head><body><div class="container"><h1>nearPlane ADSB Tracker Setup</h1><form action="/save" method="POST"><label for="ssid">WiFi Network (SSID)</label><input type="text" id="ssid" name="ssid" required><label for="password">WiFi Password</label><input type="text" id="password" name="password"><label for="lat">Your Latitude</label><input type="text" id="lat" name="lat" required placeholder="e.g., 41.015137"><label for="lon">Your Longitude</label><input type="text" id="lon" name="lon" required placeholder="e.g., 28.979530"><label for="radius">Scan Radius (km)</label><input type="number" id="radius" name="radius" value="50" required><button type="submit">Save & Reboot</button></form></div><div class="footer">M5StickC Plus 2 Edition</div></body></html>
)rawliteral";

void handleRoot();
void handleSave();
void handleNotFound();
void displayCurrentPage();
void startConfigMode();
void loadSettingsAndConnect();
void fetchAircraftData();
void playNewAircraftSound();
void handleButtons();
void drawResetScreen(int seconds_left);
void drawDegreeSymbol(int x, int y);


void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Speaker.setVolume(255);
  M5.Speaker.tone(2000, 100);
  M5.Display.setRotation(1);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  
  preferences.begin("adsb-config", false);
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
  handleButtons();
  if (isResetting) { return; }

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

void drawDegreeSymbol(int x, int y) {
    canvas.drawCircle(x, y, 2, TFT_WHITE);
}

void handleButtons() {
    if (!isResetting && M5.BtnA.wasPressed()) {
        currentPage = (currentPage + 1) % NUM_PAGES;
        displayCurrentPage();
    }

    if (M5.BtnB.wasPressed()) {
        btnB_press_start_time = millis();
        isResetting = true;
    }

    if (M5.BtnB.wasReleased()) {
        if (isResetting) {
            isResetting = false;
            displayCurrentPage();
        }
    }

    if (isResetting && M5.BtnB.isPressed()) {
        unsigned long press_duration = millis() - btnB_press_start_time;
        if (press_duration >= RESET_HOLD_TIME_MS) {
            canvas.fillScreen(TFT_ORANGE);
            canvas.setTextColor(TFT_WHITE);
            canvas.setFont(&fonts::FreeSansBold12pt7b);
            canvas.setTextDatum(MC_DATUM);
            canvas.drawString("Settings Reset!", 120, 67);
            canvas.pushSprite(0,0);
            preferences.clear();
            delay(2500);
            ESP.restart();
        } else {
            int seconds_left = ceil((RESET_HOLD_TIME_MS - press_duration) / 1000.0);
            drawResetScreen(seconds_left);
        }
    }
}

void drawResetScreen(int seconds_left) {
    canvas.fillScreen(TFT_RED);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.drawString("Release to cancel", 120, 40);
    canvas.setFont(&fonts::Font4	);
    canvas.drawString("Reset in " + String(seconds_left), 120, 90);
    canvas.pushSprite(0,0);
}

void playNewAircraftSound() {
    M5.Speaker.tone(1400, 60); delay(70); M5.Speaker.tone(1800, 90);
}

void startConfigMode() {
  const char* ap_ssid = "nearPlane-ADSB-Tracker-Setup";
  canvas.fillScreen(TFT_BLUE);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.drawString("SETUP MODE", 120, 25);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(ML_DATUM);
  canvas.drawString("1. Connect to WiFi:", 15, 55);
  canvas.setTextColor(TFT_YELLOW);
  canvas.drawString(ap_ssid, 35, 75);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawString("2. Open browser to:", 15, 95);
  canvas.setTextColor(TFT_YELLOW);
  canvas.drawString("192.168.4.1", 35, 115);
  canvas.pushSprite(0,0);
  
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

  canvas.fillScreen(BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString("Connecting to:", 120, 45);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.drawString(wifi_ssid, 120, 65);
  canvas.drawRect(30, 85, 180, 10, TFT_WHITE);
  canvas.pushSprite(0,0);
  
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    canvas.fillRect(31, 86, (178 * attempts) / 30, 8, TFT_GREEN);
    canvas.pushSprite(0,0);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    canvas.fillScreen(TFT_RED);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("Connection Failed", 120, 45);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.drawString("Hold BtnB 5 sec to reset", 120, 90);
    canvas.pushSprite(0,0);
  } else {
    canvas.fillScreen(TFT_DARKGREEN);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("Connected", 120, 55);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.drawString("Waiting for data...", 120, 85);
    canvas.pushSprite(0,0);
    delay(2000);
  }
}

void handleRoot() { server.send_P(200, "text/html", index_html); }

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
  canvas.fillScreen(BLACK);
  canvas.setTextDatum(TL_DATUM);

  if (doc.isNull() || !doc.containsKey("ac") || doc["ac"].as<JsonArray>().size() == 0) {
      canvas.setTextColor(TFT_WHITE);
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(MC_DATUM);
      canvas.drawString("No aircraft nearby.", 120, 60);
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.drawString("Checking again soon...", 120, 90);
      canvas.pushSprite(0,0);
      return;
  }

  JsonObject aircraft = doc["ac"][0];
  String flight = aircraft["flight"] | "N/A";
  flight.trim();
  String reg = aircraft["r"] | "N/A";
  String type = aircraft["t"] | "N/A";
  String squawk = aircraft["squawk"] | "----";
  String emergency = aircraft["emergency"] | "none";

  if (emergency != "none" || squawk == "7700" || squawk == "7600" || squawk == "7500") {
    canvas.fillRect(0, 0, 240, 40, TFT_RED);
    canvas.setTextColor(TFT_WHITE);
  } else {
    canvas.setTextColor(TFT_YELLOW);
  }

  canvas.setFont(&fonts::Orbitron_Light_32);
  canvas.setTextDatum(TC_DATUM);
  canvas.drawString(flight, 120, 5);
  canvas.setTextColor(TFT_WHITE);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(TC_DATUM);
  canvas.drawString(reg + " (" + type + ")", 120, 45);
  canvas.drawLine(10, 65, 230, 65, TFT_DARKGREY);

  switch (currentPage) {
    case 0: { 
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.setTextDatum(TL_DATUM);
      canvas.drawString("ALT", 15, 75);
      canvas.drawString("GND SPD", 15, 95);
      canvas.drawString("DIST", 15, 115);

      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(TR_DATUM);
      canvas.drawString(String(aircraft["alt_baro"] | 0) + " ft", 225, 75);
      canvas.drawString(String(aircraft["gs"].as<float>(), 0) + " kt", 225, 95);
      
      String distStr = String(aircraft["dst"].as<float>(), 1) + "km /" + String(aircraft["dir"].as<float>(), 0);
      canvas.drawString(distStr, 218, 115);
      drawDegreeSymbol(222, 115 + 3);
      break;
    }
    case 1: { 
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.setTextDatum(TL_DATUM);
      canvas.drawString("HDG", 15, 75);
      canvas.drawString("V/S", 15, 95);
      canvas.drawString("ROLL", 15, 115);
      
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(TR_DATUM);
      canvas.drawString(String(aircraft["true_heading"].as<float>(), 0), 218, 75);
      drawDegreeSymbol(222, 75 + 3);

      canvas.drawString(String(aircraft["baro_rate"] | 0) + " ft/m", 225, 95);

      canvas.drawString(String(aircraft["roll"].as<float>(), 1), 218, 115);
      drawDegreeSymbol(222, 115 + 3);
      break;
    }
    case 2: { 
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.setTextDatum(TL_DATUM);
      canvas.drawString("MACH", 15, 75);
      canvas.drawString("IAS/TAS", 15, 95);
      canvas.drawString("GEOM ALT", 15, 115);

      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(TR_DATUM);
      canvas.drawString(String(aircraft["mach"].as<float>(), 3), 225, 75);
      canvas.drawString(String(aircraft["ias"]|0) + "/" + String(aircraft["tas"]|0) + "kt", 225, 95);
      canvas.drawString(String(aircraft["alt_geom"] | 0) + " ft", 225, 115);
      break;
    }
    case 3: { 
      String nav_modes_str = "";
      JsonArray nav_modes = aircraft["nav_modes"];
      for(JsonVariant v : nav_modes) nav_modes_str += v.as<String>() + " ";
      nav_modes_str.trim();

      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.setTextDatum(TL_DATUM);
      canvas.drawString("AP ALT", 15, 75);
      canvas.drawString("AP HDG", 15, 95);
      canvas.drawString("AP MODE", 15, 115);
      
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(TR_DATUM);
      canvas.drawString(String(aircraft["nav_altitude_mcp"] | 0) + " ft", 225, 75);
      canvas.drawString(String(aircraft["nav_heading"].as<float>(), 0), 218, 95);
      drawDegreeSymbol(222, 95 + 3);

      canvas.setFont(&fonts::FreeSansBold9pt7b);
      canvas.drawString(nav_modes_str, 225, 115);
      break;
    }
    case 4: { 
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.setTextDatum(TL_DATUM);
      canvas.drawString("SQK", 15, 75);
      canvas.drawString("ICAO", 15, 95);
      canvas.drawString("CAT", 15, 115);
      
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(TR_DATUM);
      canvas.drawString(squawk, 225, 75);
      canvas.drawString(aircraft["hex"] | "N/A", 225, 95);
      canvas.drawString(aircraft["category"] | "N/A", 225, 115);
      break;
    }
     case 5: { 
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.setTextDatum(TL_DATUM);
      canvas.drawString("OAT/TAT", 15, 75);
      canvas.drawString("WIND", 15, 95);
      canvas.drawString("RSSI", 15, 115);
      
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(TR_DATUM);
      
      int x_pos = 225;
      int y_pos = 75;
      String tat_val = String(aircraft["tat"]|0);
      String oat_val = String(aircraft["oat"]|0);
      canvas.drawString("C", x_pos, y_pos);
      x_pos -= canvas.textWidth("C");
      drawDegreeSymbol(x_pos, y_pos + 3);
      x_pos -= 4;
      canvas.drawString(tat_val, x_pos, y_pos);
      x_pos -= canvas.textWidth(tat_val);
      canvas.drawString("/", x_pos, y_pos);
      x_pos -= canvas.textWidth("/");
      canvas.drawString("C", x_pos, y_pos);
      x_pos -= canvas.textWidth("C");
      drawDegreeSymbol(x_pos, y_pos + 3);
      x_pos -= 4;
      canvas.drawString(oat_val, x_pos, y_pos);

      String windStr = String(aircraft["wd"]|0);
      String speedStr = " / " + String(aircraft["ws"]|0) + "kt";
      canvas.drawString(windStr, 225 - canvas.textWidth(speedStr), 95);
      drawDegreeSymbol(225 - canvas.textWidth(speedStr) + 4, 95 + 3);
      canvas.drawString(speedStr, 225, 95);

      canvas.drawString(String(aircraft["rssi"].as<float>(), 1) + " dBm", 225, 115);
      break;
    }
  }

  canvas.setTextColor(TFT_DARKGREY);
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(BC_DATUM); 
  canvas.drawString(String(currentPage + 1) + "/" + String(NUM_PAGES), 120, 134);
  
  canvas.pushSprite(0,0);
}

void fetchAircraftData() {
  if (WiFi.status() != WL_CONNECTED) { return; }
  
  HTTPClient http;
  http.begin(api_url);
  http.setTimeout(5000);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    doc.clear();
    DeserializationError error = deserializeJson(doc, http.getStream());
    if (error) {
      canvas.fillScreen(TFT_DARKCYAN);
      canvas.setTextColor(TFT_WHITE);
      canvas.setTextDatum(MC_DATUM);
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.drawString("JSON PARSE ERROR", 120, 67);
      canvas.pushSprite(0,0);
      pollInterval = ERROR_POLL_INTERVAL;
    } else if (doc.containsKey("ac") && doc["ac"].as<JsonArray>().size() > 0) {
      String currentAircraftReg = doc["ac"][0]["r"] | "N/A";
      if (currentAircraftReg != "N/A" && currentAircraftReg != lastSeenAircraftReg) {
          playNewAircraftSound();
      }
      lastSeenAircraftReg = currentAircraftReg;
      displayCurrentPage();
      pollInterval = SHORT_POLL_INTERVAL;
    } else {
      lastSeenAircraftReg = "";
      doc.clear();
      displayCurrentPage();
      pollInterval = NO_AIRCRAFT_POLL_INTERVAL;
    }
  } else {
    doc.clear();
    lastSeenAircraftReg = "";
    canvas.fillScreen(TFT_MAROON);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("API ERROR", 120, 50);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.drawString("HTTP Code: " + String(httpCode), 120, 80);
    canvas.pushSprite(0,0);
    pollInterval = ERROR_POLL_INTERVAL;
  }
  http.end();
}