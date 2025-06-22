#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
#include <RTClib.h>
#include <DHT.h>
#include <SD.h>
#include <SPI.h>
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include <vector>

// WiFi credentials for Access Point
const char* ssid = "ESP32-AP";
const char* password = "12345678";

// Pin definitions
#define SOIL_MOISTURE_PIN 33
#define RELAY_PIN 27
#define BUTTON_PIN 13
#define I2C_ADDRESS 0x3C
#define RST_PIN -1

// Initialize OLED, RTC, and DHT objects
SSD1306AsciiWire oled;
RTC_DS3231 rtc;
DHT dht(26, DHT22);

// Web server on port 80
AsyncWebServer server(80);

// Global variables
float soilMoisture;
unsigned long lastButtonPressTime = 0;
unsigned long lastLogTime = 0;
unsigned long lastRestartTime = 0;
int relayStatePrev = HIGH;
int relaySwitch = 0;
int settingMoisture = 50;
bool buttonToggle = false;
bool displayActive = true;
bool autoMode = true;

File dataFile;

// Structure to store sensor data
struct SensorData {
  String timestamp;
  String temperature;
  String humidity;
  String soilMoisture;
};

// Vector to store sensor data log
std::vector<SensorData> dataLog;

// Read temperature from DHT sensor
String readDHTTemperature() {
  float t = dht.readTemperature();
  if (isnan(t)) {    
    Serial.println("Failed to read from DHT sensor!");
    return "--";
  }
  return String(t);
}

// Read humidity from DHT sensor
String readDHTHumidity() {
  float h = dht.readHumidity();
  if (isnan(h)) {
    Serial.println("Failed to read from DHT sensor!");
    return "--";
  }
  return String(h);
}

// Read soil moisture and map to percentage
String readSoilMoisture() {
  int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);
  float soilMoisturePercent = map(soilMoistureValue, 3680, 50, 0, 70);
  return String(soilMoisturePercent);
}

// Get current timestamp from RTC
String getTimeStamp() {
  DateTime now = rtc.now();
  char buffer[20];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buffer);
}

// Log sensor data to vector
void logSensorData() {
  SensorData newData;
  newData.timestamp = getTimeStamp();
  newData.temperature = readDHTTemperature();
  newData.humidity = readDHTHumidity();
  newData.soilMoisture = readSoilMoisture();
  dataLog.push_back(newData);
}

// Generate HTML for data log display
String generateDataLogHTML() {
  String html = "<style>";
  html += "table { border-collapse: separate; border-spacing: 0 10px; }";
  html += "td, th { padding: 10px; border: 1px solid #ddd; }";
  html += "</style>";
  html += "<table><tr><th>Timestamp</th><th>Temperature (Celsius)</th><th>Humidity (%)</th><th>Soil Moisture (%)</th></tr>";
  for (const auto& entry : dataLog) {
    html += "<tr><td>" + entry.timestamp + "</td><td>" + entry.temperature + "</td><td>" + entry.humidity + "</td><td>" + entry.soilMoisture + "</td></tr>";
  }
  html += "</table>";
  return html;
}

// Clear data log
void clearDataLog() {
  dataLog.clear();
}

// Increase soil moisture threshold
void increaseThreshold() {
  settingMoisture += 5;
  if (settingMoisture > 60) {
    settingMoisture = 0; // Reset to 0 if threshold exceeds 60
  }
}

// Toggle relay in manual mode
void toggleRelay() {
  if (!autoMode) {
    lastButtonPressTime = millis();
    buttonToggle = !buttonToggle;
    digitalWrite(RELAY_PIN, buttonToggle ? HIGH : LOW);
  }
}

// Toggle between auto and manual mode
String toggleMode() {
  autoMode = !autoMode;
  if (autoMode) {
    return "Auto";
  } else {
    digitalWrite(RELAY_PIN, LOW);
    return "Manual";
  }
}

// HTML for web interface
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Auto Watering Control Box</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 0;
            background-color: #F4F4F4;
        }
        .container {
            max-width: 360px;
            margin: auto;
            background-color: #FFFFFF;
            padding: 20px;
            border-radius: 12px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.1);
        }
        h1 {
            text-align: center;
            color: #4CAF50;
            margin-bottom: 20px;
        }
        .sensor-box {
            background-color: #F9F9F9;
            padding: 10px;
            margin-bottom: 10px;
            border-radius: 8px;
        }
        .sensor-label {
            font-size: 16px;
            color: #555;
        }
        .sensor-value {
            font-size: 24px;
            font-weight: bold;
            color: #333;
        }
        .btn {
            width: 100%;
            padding: 12px;
            border: none;
            border-radius: 8px;
            margin-top: 10px;
            font-size: 16px;
            cursor: pointer;
        }
        .btn-blue {
            background-color: #007BFF;
            color: white;
        }
        .btn-red {
            background-color: #DC3545;
            color: white;
        }
        .btn-green {
            background-color: #28A745;
            color: white;
        }
        table {
            width: 100%;
            border-collapse: collapse;
        }
        th, td {
            text-align: left;
            padding: 8px;
            border-bottom: 1px solid #ddd;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Auto Watering Control</h1>
        <div class="sensor-box">
            <div class="sensor-label">Temperature</div>
            <div class="sensor-value"><span id="temperature">%TEMPERATURE%</span> °C</div>
        </div>
        <div class="sensor-box">
            <div class="sensor-label">Humidity</div>
            <div class="sensor-value"><span id="humidity">%HUMIDITY%</span> %</div>
        </div>
        <div class="sensor-box">
            <div class="sensor-label">Soil Moisture</div>
            <div class="sensor-value"><span id="soilMoisture">%SOILMOISTURE%</span> %</div>
        </div>
        <div class="sensor-box">
            <div class="sensor-label">Soil Moisture Threshold</div>
            <div class="sensor-value"><span id="moistureThreshold">%MOISTURETHRESHOLD%</span> %</div>
        </div>
        <div class="sensor-box">
            <div class="sensor-label">Current Mode</div>
            <div class="sensor-value"><span id="currentMode">Auto</span></div>
        </div>
        <button class="btn btn-green" onclick="location.href='/dataLog'">View Data Log</button>
        <button class="btn btn-red" onclick="clearDataLogFunction()">Clear Data Log</button>
        <button class="btn btn-blue" onclick="increaseThresholdFunction()">Increase Threshold</button>
        <button class="btn btn-blue" onclick="toggleRelayFunction()">Toggle Relay</button>
        <button class="btn btn-green" onclick="toggleModeFunction()">Toggle Auto/Manual</button>
    </div>
    <script>
        function increaseThresholdFunction() {
            fetch('/increaseThreshold')
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                    updateSensorData();
                });
        }
        function clearDataLogFunction() {
            fetch('/clearDataLog')
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                    alert('Data log cleared');
                });
        }
        function toggleRelayFunction() {
            fetch('/toggleRelay')
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                });
        }
        function toggleModeFunction() {
            fetch('/toggleMode')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('currentMode').textContent = data;
                    console.log('Mode switched to: ' + data);
                });
        }
        function updateCurrentMode() {
            fetch('/currentMode')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('currentMode').textContent = data;
                });
        }
        document.addEventListener('DOMContentLoaded', updateCurrentMode);
        function updateSensorData() {
            fetch('/temperature')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('temperature').textContent = data;
                });
            fetch('/humidity')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('humidity').textContent = data;
                });
            fetch('/soilMoisture')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('soilMoisture').textContent = data;
                });
            fetch('/moistureThreshold')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('moistureThreshold').textContent = data;
                });
            updateCurrentMode();
        }
        updateSensorData();
        setInterval(updateSensorData, 1000);
    </script>
</body>
</html>
)rawliteral";

// Process template variables for web interface
String processor(const String& var) {
  if (var == "TEMPERATURE") {
    return String(dht.readTemperature());
  } else if (var == "HUMIDITY") {
    return String(dht.readHumidity());
  } else if (var == "SOILMOISTURE") {
    return String(readSoilMoisture());
  } else if (var == "MOISTURETHRESHOLD") {
    return String(settingMoisture);
  }
  return String();
}

// Read all sensor values
void readSensor(float &temperature, float &humidity, float &moisture) {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  moisture = map(analogRead(SOIL_MOISTURE_PIN), 3680, 50, 0, 70);
  if (moisture < 0) {
    moisture = 0;
  } else if (moisture > 70) {
    moisture = 70;
  }
}

// Initialize hardware and server
void setup() {
  Wire.begin();
  Wire.setClock(400000L);
  Serial.begin(9600);
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  dht.begin();

  // Initialize RTC and set to compile time if needed
  rtc.begin();
  if (!rtc.begin() || rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Set up WiFi Access Point
  WiFi.softAP(ssid, password);
  Serial.println(WiFi.localIP());

  // Define server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readDHTTemperature().c_str());
  });
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readDHTHumidity().c_str());
  });
  server.on("/soilMoisture", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readSoilMoisture().c_str());
  });
  server.on("/dataLog", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", generateDataLogHTML());
  });
  server.on("/clearDataLog", HTTP_GET, [](AsyncWebServerRequest *request){
    clearDataLog();
    request->send(200, "text/plain", "Data log cleared");
  });
  server.on("/increaseThreshold", HTTP_GET, [](AsyncWebServerRequest *request){
    increaseThreshold();
    request->send(200, "text/plain", "Threshold increased");
  });
  server.on("/toggleRelay", HTTP_GET, [](AsyncWebServerRequest *request){
    toggleRelay();
    request->send(200, "text/plain", "Relay toggled");
  });
  server.on("/moistureThreshold", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(settingMoisture).c_str());
  });
  server.on("/toggleMode", HTTP_GET, [](AsyncWebServerRequest *request){
    String mode = toggleMode();
    request->send(200, "text/plain", mode);
  });
  server.on("/currentMode", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", autoMode ? "Auto" : "Manual");
  });

  server.begin();

  // Create task for periodic data logging
  xTaskCreatePinnedToCore(
    [] (void* parameter) {
      for (;;) {
        logSensorData();
        vTaskDelay(10000 / portTICK_PERIOD_MS);
      }
    },
    "DataLogger",
    4096,
    NULL,
    1,
    NULL,
    0
  );

  // Initialize OLED display
  #if RST_PIN >= 0
    oled.begin(&SH1106_128x64, I2C_ADDRESS, RST_PIN);
  #else
    oled.begin(&SH1106_128x64, I2C_ADDRESS);
  #endif

  oled.setFont(Adafruit5x7);
  oled.set1X();
  oled.clear();
  oled.println("NTU Soil Moisture");
  oled.println("Sensing and Control");
  oled.println("System");
  oled.println();
  oled.println("Press button to");
  oled.println("control watering");

  // Initialize SD card
  if (!SD.begin(5)) {
    Serial.println("SD initialization failed!");
    return;
  }
  Serial.println("SD initialization done.");
}

// Log sensor data to SD card
void logData(DateTime now, float temperature, float humidity, float moisture) {
  dataFile = SD.open("/data.log", FILE_APPEND);
  if (dataFile) {
    dataFile.print(now.year(), DEC);
    dataFile.print('/');
    dataFile.print(now.month(), DEC);
    dataFile.print('/');
    dataFile.print(now.day(), DEC);
    dataFile.print(" ");
    dataFile.print(now.hour(), DEC);
    dataFile.print(':');
    dataFile.print(now.minute(), DEC);
    dataFile.print(':');
    dataFile.print(now.second(), DEC);
    dataFile.print(", ");
    dataFile.print(temperature, 1);
    dataFile.print(", ");
    dataFile.print(humidity, 0);
    dataFile.print(", ");
    dataFile.print(moisture, 2);
    dataFile.print(", ");
    dataFile.println(relaySwitch);
    dataFile.flush();
    dataFile.close();
    Serial.println("Logged Data");
  } else {
    Serial.println("Error opening data file");
  }
}

// Main loop for sensor reading and control
void loop() {
  float temperature, humidity;
  readSensor(temperature, humidity, soilMoisture);
  DateTime now = rtc.now();

  // Handle button press and relay state changes
  unsigned long currentTime = millis();
  int buttonState = digitalRead(BUTTON_PIN);
  int relayState = digitalRead(RELAY_PIN);
  if ((relayState == LOW && relayStatePrev == HIGH) || (relayState == HIGH && relayStatePrev == LOW)) {
    logData(now, temperature, humidity, soilMoisture);
    Serial.println("Data logged");
  }
  relayStatePrev = relayState;

  // Automatic watering control
  if (autoMode) {
    if (soilMoisture < settingMoisture) {
      digitalWrite(RELAY_PIN, HIGH);
      relaySwitch = 1;
    }
    if (soilMoisture > settingMoisture + 2) {
      digitalWrite(RELAY_PIN, LOW);
      relaySwitch = 0;
    }
  }

  // Handle button press for threshold adjustment
  if (buttonState == LOW) {
    lastButtonPressTime = currentTime;
    displayActive = true;
    settingMoisture += 5;
    if (settingMoisture > 60) {
      settingMoisture = 0;
    }
    delay(300); // Debounce delay
  }

  // Log data every 15 minutes
  if (currentTime - lastLogTime >= 900000) {
    lastLogTime = currentTime;
    logData(now, temperature, humidity, soilMoisture);
    Serial.println("Data logged");
  }

  // Update OLED display
  if (displayActive) {
    oled.clear();
    oled.println("Time: " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()));
    oled.println("Temp: " + String(temperature, 1) + " Celsius");
    oled.println("Humidity: " + String(humidity, 0) + " %");
    oled.println("Soil Moisture: " + String(soilMoisture, 2) + " %");
    oled.println("Threshold: " + String(settingMoisture) + " %");
  }
  
  delay(1000);
}