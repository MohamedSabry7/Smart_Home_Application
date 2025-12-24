// ----------------------------------------------------
// Smart Home Prototype - ESP32 DOIT DEV KIT ESP-WROOM-32 Version
// ----------------------------------------------------
// This code is converted from Arduino + ESP8266 module to native ESP32
// The ESP32 has built-in Wi-Fi, so no external module is needed!

#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal.h> // Add this library

// Map the pins: rs=19, en=23, d4=18, d5=5, d6=4, d7=2
const int rs = 19, en = 23, d4 = 18, d5 = 5, d6 = 4, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


const int DIP_SWITCH_PIN = 32; // Pin for your physical switch
bool lampAppReq = false;       // To track the App's request separately

// --- Wi-Fi Configuration ---
// !! CHANGE THESE TO YOUR NETWORK DETAILS !!
const char* WIFI_SSID = "Seneca";
const char* WIFI_PASSWORD = "Se@@1234";
const int SERVER_PORT = 80;

// Create a WebServer on port 80
WebServer server(SERVER_PORT);

// --- Actuator/Sensor Pin Definitions (ESP32 GPIO) ---
// Using ESP32-friendly pins that don't conflict with boot/flash
const int LAMP_RELAY_PIN = 26;   // GPIO26 - safe output pin
const int PLUG_RELAY_PIN = 27;   // GPIO27 - safe output pin
const int DOOR_SENSOR_PIN = 14;  // GPIO14 - safe input with internal pullup
const int BUZZER_PIN = 25;       // GPIO25 - safe output pin

// --- NTC Thermistor Configuration ---
// ESP32 ADC1 pins: GPIO32, GPIO33, GPIO34, GPIO35, GPIO36, GPIO39
// Note: ADC2 pins cannot be used when Wi-Fi is active!
// !! IMPORTANT: Verify your NTC and resistor values !!
const int NTC_PIN = 34;          // GPIO34 (ADC1_CH6)
const float V_REF = 3.3;
const float NOMINAL_RESISTANCE = 100000;    // 100k Ohm NTC (at 25°C) - CHANGE if your NTC is different!
const float NOMINAL_TEMPERATURE = 25;       // 25C 
const int BETA_COEFFICIENT = 3950;          // B-value for NTC
const float REFERENCE_RESISTANCE = 100000;  // 100k Fixed resistor (confirmed by user)
const int ADC_RESOLUTION = 4096;            // ESP32 has 12-bit ADC (0-4095)

// Alarm Settings (now settable from App)
float alarmTempThreshold = 27.0;

// --- State Variables ---
bool lampRelayState = false;   // Current relay state (controlled by app)
bool plugState = false;
bool buzzerAppOverride = false;
bool previousDoorState = true; // HIGH=CLOSED, LOW=OPEN

// Variables for managing status updates
unsigned long lastStatusUpdateTime = 0;
const unsigned long STATUS_REPORT_INTERVAL_MS = 5000; // Report status every 5 seconds


// --- Function Prototypes ---
float readNTC();
String sendCurrentStatus(float temp, bool doorSensorReading);
void handleRoot();
void handleLampOn();
void handleLampOff();
void handleLampToggle();
void handlePlugOn();
void handlePlugOff();
void handleSetThreshold();
void handleAlarmOn();
void handleAlarmOff();
void handleStatus();
void handleNotFound();
void addCORSHeaders();
void updateLCD(float temp, bool doorOpen, bool lampOn);


void setup() {
    Serial.begin(115200);  // ESP32 native baud rate
    delay(1000);           // Give serial time to initialize
    
    Serial.println("\n\n============================================");
    Serial.println("Smart Home Prototype - ESP32 Version");
    Serial.println("============================================\n");

    // Initialize Actuator Pins and set them OFF (HIGH for Active LOW relays)

    pinMode(LAMP_RELAY_PIN, OUTPUT);
    pinMode(PLUG_RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(LAMP_RELAY_PIN, HIGH);   // Relay OFF (Active LOW)
    digitalWrite(PLUG_RELAY_PIN, HIGH);   // Relay OFF (Active LOW)
    digitalWrite(BUZZER_PIN, LOW);        // Buzzer OFF
    lcd.begin(16, 2);           // Initialize 16x2 LCD
    lcd.print("System Boot..."); // Initial message

    // Initialize Sensor Pins

    pinMode(DIP_SWITCH_PIN, INPUT_PULLUP); // Use internal resistor
    pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
    previousDoorState = digitalRead(DOOR_SENSOR_PIN);

    // Configure ADC for NTC reading
    analogReadResolution(12);  // 12-bit resolution (0-4095)
    analogSetAttenuation(ADC_11db);  // Full 3.3V range

    Serial.println("Connecting to Wi-Fi...");
    
    // Connect to Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n\n=== WI-FI CONNECTED SUCCESSFULLY! ===");
        Serial.print(">>> YOUR IP ADDRESS: ");
        Serial.println(WiFi.localIP());
        Serial.println("=====================================\n");
    } else {
        Serial.println("\n\n!!! FAILED TO CONNECT TO WI-FI !!!");
        Serial.println("Check your SSID and password.");
        Serial.println("Restarting in 5 seconds...");
        delay(5000);
        ESP.restart();
    }

    // Setup HTTP Server Routes
    server.on("/", handleRoot);
    server.on("/LAMP_ON", handleLampOn);
    server.on("/LAMP_OFF", handleLampOff);
    server.on("/LAMP_TOGGLE", handleLampToggle);
    server.on("/PLUG_ON", handlePlugOn);
    server.on("/PLUG_OFF", handlePlugOff);
    server.on("/ALARM_ON", handleAlarmOn);
    server.on("/ALARM_OFF", handleAlarmOff);
    server.on("/STATUS", handleStatus);
    server.onNotFound(handleNotFound);

    // Start the server
    server.begin();
    Serial.println("HTTP Server Started!");
    Serial.print("Access the device at: http://");
    Serial.println(WiFi.localIP());
}

void loop() {
    // 1. Handle incoming HTTP requests
    server.handleClient();

    // 2. Read Sensors
    float currentTemp = readNTC();
    bool currentDoorState = digitalRead(DOOR_SENSOR_PIN);
    bool currentDipState = digitalRead(DIP_SWITCH_PIN); // Read physical switch
    static unsigned long lastLCDUpdate = 0;

    bool finalLampState = lampRelayState ^ currentDipState; 
    digitalWrite(LAMP_RELAY_PIN, finalLampState ? LOW : HIGH);
    
    // 3. Temperature Alarm Logic (using settable threshold)
    bool shouldAlarm = currentTemp > alarmTempThreshold;
    if (shouldAlarm || buzzerAppOverride) {
        digitalWrite(BUZZER_PIN, HIGH); // Physical Alarm ON
    } else {
        digitalWrite(BUZZER_PIN, LOW);  // Physical Alarm OFF
    }

    // 4. Door Status Change Alert (inverted: HIGH = OPEN, LOW = CLOSED for reed switch)
    if (currentDoorState != previousDoorState) {
        Serial.print(">>> DOOR STATUS CHANGE: ");
        Serial.println((currentDoorState == HIGH) ? "OPENED" : "CLOSED");
        previousDoorState = currentDoorState;
    }

    // 5. Periodic Status Reporting
    if (millis() - lastStatusUpdateTime >= STATUS_REPORT_INTERVAL_MS) {
        Serial.print("STATUS UPDATE: ");
        Serial.print(currentTemp, 2);
        Serial.print(" C. Door: ");
        Serial.print((currentDoorState == HIGH) ? "OPENED" : "CLOSED");
        Serial.print(" | Threshold: ");
        Serial.print(alarmTempThreshold, 1);
        Serial.println(" C");

        lastStatusUpdateTime = millis();
    }
    
    //if (millis() - lastLCDUpdate > 1000) {
        updateLCD(currentTemp, currentDoorState, finalLampState);
        //lastLCDUpdate = millis();
    //}
    delay(10);  // Small delay for stability
}


// --- NTC Thermistor Function ---
float readNTC() {
    // 1. Read the raw ADC value (multiple samples for stability)
    long adc_sum = 0;
    const int NUM_SAMPLES = 20;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        adc_sum += analogRead(NTC_PIN);
        delay(2);
    }
    int adc_reading = adc_sum / NUM_SAMPLES;

    // Debug: Print raw ADC value occasionally
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 5000) {
        Serial.print("[DEBUG] Raw ADC: ");
        Serial.print(adc_reading);
        Serial.print(" / 4095");
        lastDebugTime = millis();
    }

    // 2. Convert ADC reading to Resistance (R_thermistor)
    // User's Circuit: 3.3V --- [100k Fixed Resistor] --- GPIO34 --- [NTC] --- GND
    // NTC is on BOTTOM (between ADC pin and GND)
    // Formula: R_ntc = R_ref × ADC / (ADC_max - ADC)
    float resistance;
    if (adc_reading <= 10) {
        resistance = 100;  // Very low resistance (extremely hot) - avoid div by zero
    } else if (adc_reading >= ADC_RESOLUTION - 10) {
        resistance = NOMINAL_RESISTANCE * 100;  // Very high resistance (open circuit)
    } else {
        // NTC on BOTTOM of voltage divider - CORRECT formula for user's wiring
        resistance = REFERENCE_RESISTANCE * ((float)adc_reading / (ADC_RESOLUTION - adc_reading));
    }

    // Debug: Print resistance
    if (millis() - lastDebugTime < 100) {
        Serial.print(", R = ");
        Serial.print(resistance / 1000, 1);
        Serial.println(" kOhm");
    }

    // 3. Apply Steinhart-Hart Equation (Simplified Beta Model)
    // 1/T = 1/T0 + (1/B) * ln(R/R0)
    float steinhart;
    steinhart = resistance / NOMINAL_RESISTANCE;     // (R/Ro)
    steinhart = log(steinhart);                      // ln(R/Ro)
    steinhart /= BETA_COEFFICIENT;                   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                     // Invert to get Kelvin

    // 4. Convert to Celsius
    float temp_C = steinhart - 273.15;

    return temp_C;
}

void updateLCD(float temp, bool doorOpen, bool lampOn) {
    // Line 1: Temperature
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(temp, 1);
    lcd.print((char)223); // Degree symbol
    lcd.print("C    ");

    // Line 2: Door (D) and Lamp (L)
    lcd.setCursor(0, 1);
    // Door logic: In your code HIGH=OPEN
    lcd.print("D:");
    lcd.print(doorOpen ? "OPEN " : "CLSD ");
    
    lcd.print("| L:");
    lcd.print(lampOn ? "ON  " : "OFF ");
}


// --- HTTP Request Handler Functions ---

void addCORSHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleRoot() {
    addCORSHeaders();
    String html = "<html><head><title>ESP32 Smart Home</title></head>";
    html += "<body><h1>ESP32 Smart Home Server</h1>";
    html += "<p>IP Address: " + WiFi.localIP().toString() + "</p>";
    html += "<p>Use /STATUS to get current status</p>";
    html += "<p>Commands: /LAMP_ON, /LAMP_OFF, /LAMP_TOGGLE, /PLUG_ON, /PLUG_OFF, /ALARM_ON, /ALARM_OFF</p>";
    html += "<p>Set threshold: /SET_THRESHOLD:XX.X</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleLampOn() {
    lampRelayState = true;
    //digitalWrite(LAMP_RELAY_PIN, LOW);  // Active LOW relay ON
    Serial.println("> Command received: LAMP_ON");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handleLampOff() {
    lampRelayState = false;
    //digitalWrite(LAMP_RELAY_PIN, HIGH);  // Active LOW relay OFF
    Serial.println("> Command received: LAMP_OFF");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handleLampToggle() {
    lampRelayState = !lampRelayState;
    digitalWrite(LAMP_RELAY_PIN, lampRelayState ? LOW : HIGH);
    Serial.print("> Command received: LAMP_TOGGLE -> ");
    Serial.println(lampRelayState ? "ON" : "OFF");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handlePlugOn() {
    plugState = true;
    digitalWrite(PLUG_RELAY_PIN, LOW);  // Active LOW relay ON
    Serial.println("> Command received: PLUG_ON");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handlePlugOff() {
    plugState = false;
    digitalWrite(PLUG_RELAY_PIN, HIGH);  // Active LOW relay OFF
    Serial.println("> Command received: PLUG_OFF");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handleAlarmOn() {
    buzzerAppOverride = true;
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("> Command received: ALARM_ON");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handleAlarmOff() {
    buzzerAppOverride = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("> Command received: ALARM_OFF");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handleStatus() {
    Serial.println("> Status poll received");
    
    addCORSHeaders();
    server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
}

void handleNotFound() {
    String uri = server.uri();
    
    // Check for SET_THRESHOLD command
    if (uri.startsWith("/SET_THRESHOLD:")) {
        String thresholdStr = uri.substring(15);
        float newThreshold = thresholdStr.toFloat();
        if (newThreshold > 0 && newThreshold < 100) {
            alarmTempThreshold = newThreshold;
            Serial.print("> Threshold set to: ");
            Serial.println(alarmTempThreshold);
        }
        addCORSHeaders();
        server.send(200, "text/plain", sendCurrentStatus(readNTC(), digitalRead(DOOR_SENSOR_PIN)));
        return;
    }
    
    addCORSHeaders();
    server.send(404, "text/plain", "Not Found");
}


// --- Status Response Function ---
String sendCurrentStatus(float temp, bool doorSensorReading) {
    // Door sensor: HIGH = OPEN (magnet away), LOW = CLOSED (magnet near)
    String doorStatusStr = (doorSensorReading == HIGH) ? "OPEN" : "CLOSED";
    String lampStatusStr = lampRelayState ? "ON" : "OFF";
    String plugStatusStr = plugState ? "ON" : "OFF";
    String alarmStatusStr = (temp > alarmTempThreshold || buzzerAppOverride) ? "ALARM" : "SAFE";

    // Format: "TEMP:XX.XX,DOOR:STATUS,LAMP:STATUS,PLUG:STATUS,ALARM:STATUS,THRESHOLD:XX.X"
    String statusMessage = "";
    statusMessage += "TEMP:";
    statusMessage += String(temp, 2);
    statusMessage += ",DOOR:";
    statusMessage += doorStatusStr;
    statusMessage += ",LAMP:";
    statusMessage += lampStatusStr;
    statusMessage += ",PLUG:";
    statusMessage += plugStatusStr;
    statusMessage += ",ALARM:";
    statusMessage += alarmStatusStr;
    statusMessage += ",THRESHOLD:";
    statusMessage += String(alarmTempThreshold, 1);

    return statusMessage;
}
