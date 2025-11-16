/**
 * ESP32 GoPro Time Sync
 * 
 * Automatically synchronizes GoPro camera time from DS3231 RTC module.
 * 
 * Hardware Requirements:
 * - ESP32 development board
 * - DS3231 RTC module (I2C: SDA=GPIO21, SCL=GPIO22)
 * - GoPro Hero 9/10/11 camera
 * 
 * Process:
 * 1. Read time from DS3231 RTC
 * 2. Connect to GoPro via BLE
 * 3. Read WiFi credentials and enable WiFi AP
 * 4. Connect to GoPro WiFi network
 * 5. Set time via HTTP API
 * 
 * API Endpoint: /gp/gpControl/command/setup/date_time (legacy format)
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <RTClib.h>

// GoPro WiFi AP BLE Characteristics
#define GOPRO_WIFI_SSID_UUID "b5f90002-aa8d-11e3-9046-0002a5d5c51b"
#define GOPRO_WIFI_PASSWORD_UUID "b5f90003-aa8d-11e3-9046-0002a5d5c51b"
#define GOPRO_WIFI_AP_ENABLE_UUID "b5f90004-aa8d-11e3-9046-0002a5d5c51b"
#define GOPRO_WIFI_AP_STATE_UUID "b5f90005-aa8d-11e3-9046-0002a5d5c51b"

// Configuration
#define SCAN_TIME_SECONDS 10
#define BLE_CONNECT_TIMEOUT_MS 15000
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define AP_READY_POLL_ATTEMPTS 25

// Buzzer Configuration
#define BUZZER_PIN 25           // GPIO pin for buzzer (change if needed)
#define BEEP_DURATION_MS 200    // Short beep duration

// Global variables
static NimBLEClient* pClient = nullptr;

// WiFi AP characteristics (only ones we actually use)
static NimBLERemoteCharacteristic* pWiFiSSIDChar = nullptr;
static NimBLERemoteCharacteristic* pWiFiPasswordChar = nullptr;
static NimBLERemoteCharacteristic* pWiFiAPEnableChar = nullptr;
static NimBLERemoteCharacteristic* pWiFiAPStateChar = nullptr;

static String goProSSID = "";
static String goProPassword = "";

// DS3231 RTC
static RTC_DS3231 rtc;

// Simple BLE client callback
class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("[BLE] Connected to GoPro");
    }

    void onDisconnect(NimBLEClient* pClient) {
        Serial.println("[BLE] Disconnected from GoPro");
    }
};

// Buzzer control - short beep for successful time sync
void beep() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(BEEP_DURATION_MS);
    digitalWrite(BUZZER_PIN, LOW);
}

// Get WiFi SSID from GoPro (by reading characteristic directly)
bool getWiFiSSID() {
    Serial.println("[BLE] Getting WiFi SSID...");
    
    if (pWiFiSSIDChar == nullptr || !pWiFiSSIDChar->canRead()) {
        Serial.println("[BLE] ERROR: WiFi SSID characteristic not available");
        return false;
    }
    
    std::string ssidValue = pWiFiSSIDChar->readValue();
    if (ssidValue.length() > 0) {
        goProSSID = String(ssidValue.c_str());
        Serial.printf("[BLE] WiFi SSID: %s\n", goProSSID.c_str());
        return true;
    }
    
    Serial.println("[BLE] ERROR: Failed to read WiFi SSID");
    return false;
}

// Get WiFi password from GoPro (by reading characteristic directly)
bool getWiFiPassword() {
    Serial.println("[BLE] Getting WiFi password...");
    
    if (pWiFiPasswordChar == nullptr || !pWiFiPasswordChar->canRead()) {
        Serial.println("[BLE] ERROR: WiFi Password characteristic not available");
        return false;
    }
    
    std::string passwordValue = pWiFiPasswordChar->readValue();
    if (passwordValue.length() > 0) {
        goProPassword = String(passwordValue.c_str());
        Serial.printf("[BLE] WiFi password: %s\n", goProPassword.c_str());
        return true;
    }
    
    Serial.println("[BLE] ERROR: Failed to read WiFi password");
    return false;
}

// Enable WiFi AP on GoPro (by writing to characteristic directly)
bool enableWiFiAP() {
    Serial.println("[BLE] Enabling WiFi AP...");
    
    if (pWiFiAPEnableChar == nullptr || !pWiFiAPEnableChar->canWrite()) {
        Serial.println("[BLE] ERROR: WiFi AP Enable characteristic not available");
        return false;
    }
    
    // Write 0x01 to enable the AP
    uint8_t enableValue = 0x01;
    if (pWiFiAPEnableChar->writeValue(&enableValue, 1, false)) {
        Serial.println("[BLE] WiFi AP enable command sent successfully");
        delay(1000); // Give the AP time to start
        return true;
    }
    
    Serial.println("[BLE] ERROR: Failed to write WiFi AP enable");
    return false;
}


// Check if AP mode is ready (by reading characteristic directly)
bool checkAPModeStatus() {
    Serial.println("[BLE] Checking AP mode status...");
    
    if (pWiFiAPStateChar == nullptr || !pWiFiAPStateChar->canRead()) {
        Serial.println("[BLE] WARNING: WiFi AP State characteristic not available");
        return false;
    }
    
    std::string stateValue = pWiFiAPStateChar->readValue();
    if (stateValue.length() > 0) {
        uint8_t apState = (uint8_t)stateValue[0];
        Serial.printf("[BLE] AP Mode status: 0x%02X\n", apState);
        
        // AP State values:
        // 0x00 = Disabled
        // 0x01 = Enabling/Starting
        // 0x03 = Enabled and broadcasting
        if (apState >= 0x03) {
            Serial.println("[BLE] AP is ready and broadcasting!");
            return true;
        } else if (apState == 0x01) {
            Serial.println("[BLE] AP is still starting...");
            return false;
        } else {
            Serial.println("[BLE] AP is disabled");
            return false;
        }
    }
    
    Serial.println("[BLE] ERROR: Failed to read AP state");
    return false;
}

// Wait for AP mode to become ready
bool waitForAPMode(int maxAttempts = AP_READY_POLL_ATTEMPTS) {
    Serial.println("[BLE] Waiting for AP mode to be ready...");
    
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        if (checkAPModeStatus()) {
            Serial.printf("[BLE] AP Mode is ready (poll #%d)\n", attempt);
            return true;
        }
        delay(200);
    }
    
    Serial.println("[BLE] ERROR: Timeout waiting for AP mode");
    return false;
}

// Scan for GoPro devices
NimBLEAddress* scanForGoPro() {
    Serial.println("[BLE] Scanning for GoPro devices...");
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    NimBLEScanResults results = pScan->start(SCAN_TIME_SECONDS, false);
    
    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        String deviceName = device.getName().c_str();
        
        // Look for devices starting with "GoPro"
        if (deviceName.startsWith("GoPro")) {
            Serial.printf("[BLE] Found GoPro: %s (%s)\n", 
                         deviceName.c_str(), 
                         device.getAddress().toString().c_str());
            
            static NimBLEAddress addr = device.getAddress();
            pScan->clearResults();
            return &addr;
        }
    }
    
    pScan->clearResults();
    Serial.println("[BLE] No GoPro devices found");
    return nullptr;
}

// Connect to GoPro via BLE
bool connectToGoPro(NimBLEAddress* pAddress) {
    Serial.printf("[BLE] Connecting to GoPro at %s...\n", pAddress->toString().c_str());
    
    if (pClient == nullptr) {
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(new MyClientCallback());
        pClient->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS / 1000);
    }
    
    if (!pClient->connect(*pAddress)) {
        Serial.println("[BLE] ERROR: Failed to connect");
        return false;
    }
    
    Serial.println("[BLE] Connected! Discovering services...");
    
    // Get all services first
    std::vector<NimBLERemoteService*>* pServices = pClient->getServices(true);
    if (pServices == nullptr || pServices->empty()) {
        Serial.println("[BLE] ERROR: No services found");
        return false;
    }
    
    Serial.printf("[BLE] Found %d services\n", pServices->size());
    
    // Find characteristics across all services
    for (auto pService : *pServices) {
        Serial.printf("[BLE] Checking service: %s\n", pService->getUUID().toString().c_str());
        
        std::vector<NimBLERemoteCharacteristic*>* pChars = pService->getCharacteristics(true);
        if (pChars != nullptr) {
            for (auto pChar : *pChars) {
                std::string uuidStd = pChar->getUUID().toString();
                String uuid = String(uuidStd.c_str());
                Serial.printf("[BLE]   - Characteristic: %s\n", uuid.c_str());
                
                // Only look for WiFi-related characteristics
                if (uuid.equalsIgnoreCase(GOPRO_WIFI_SSID_UUID)) {
                    pWiFiSSIDChar = pChar;
                    Serial.println("[BLE]     -> WiFi SSID");
                }
                else if (uuid.equalsIgnoreCase(GOPRO_WIFI_PASSWORD_UUID)) {
                    pWiFiPasswordChar = pChar;
                    Serial.println("[BLE]     -> WiFi Password");
                }
                else if (uuid.equalsIgnoreCase(GOPRO_WIFI_AP_ENABLE_UUID)) {
                    pWiFiAPEnableChar = pChar;
                    Serial.println("[BLE]     -> WiFi AP Enable");
                }
                else if (uuid.equalsIgnoreCase(GOPRO_WIFI_AP_STATE_UUID)) {
                    pWiFiAPStateChar = pChar;
                    Serial.println("[BLE]     -> WiFi AP State");
                }
            }
        }
    }
    
    // Check if we found all required WiFi characteristics
    if (pWiFiSSIDChar == nullptr || pWiFiPasswordChar == nullptr || 
        pWiFiAPEnableChar == nullptr || pWiFiAPStateChar == nullptr) {
        Serial.println("[BLE] ERROR: Missing required WiFi characteristics");
        Serial.printf("[BLE]   SSID: %s, Password: %s, Enable: %s, State: %s\n", 
                     pWiFiSSIDChar ? "OK" : "MISSING",
                     pWiFiPasswordChar ? "OK" : "MISSING",
                     pWiFiAPEnableChar ? "OK" : "MISSING",
                     pWiFiAPStateChar ? "OK" : "MISSING");
        return false;
    }
    
    Serial.println("[BLE] BLE connection established!");
    return true;
}

// Connect to GoPro WiFi AP
bool connectToGoProWiFi() {
    Serial.printf("[WiFi] Connecting to GoPro AP: %s...\n", goProSSID.c_str());
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(goProSSID.c_str(), goProPassword.c_str());
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime < WIFI_CONNECT_TIMEOUT_MS)) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("[WiFi] ERROR: Connection failed");
        return false;
    }
}

// Set date/time on GoPro via HTTP
bool setGoProDateTime() {
    Serial.println("[HTTP] Setting GoPro date/time...");
    
    // Get current time from DS3231 RTC
    DateTime now = rtc.now();
    
    Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
    
    // Use the legacy GoPro API endpoint with hex-encoded parameters
    // Format: /gp/gpControl/command/setup/date_time?p=%YY%MM%DD%HH%MM%SS
    char url[256];
    snprintf(url, sizeof(url),
             "http://10.5.5.9/gp/gpControl/command/setup/date_time?p=%%%02x%%%02x%%%02x%%%02x%%%02x%%%02x",
             now.year() % 100, now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    
    Serial.printf("[HTTP] URL: %s\n", url);
    
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200 || httpCode == 204) {
        Serial.println("[HTTP] Time synchronized successfully!");
        http.end();
        return true;
    } else {
        Serial.printf("[HTTP] ERROR: Request failed with code %d\n", httpCode);
        String payload = http.getString();
        if (payload.length() > 0) {
            Serial.printf("[HTTP] Response: %s\n", payload.c_str());
        }
        http.end();
        return false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n==================================");
    Serial.println("ESP32 GoPro Time Sync");
    Serial.println("==================================\n");
    
    // Initialize buzzer pin
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.printf("[BUZZER] Initialized on GPIO %d\n", BUZZER_PIN);
    
    // Initialize I2C for DS3231 RTC
    Serial.println("[RTC] Initializing DS3231 RTC...");
    Wire.begin();
    
    if (!rtc.begin()) {
        Serial.println("[RTC] ERROR: Couldn't find DS3231 RTC!");
        Serial.println("[RTC] Please check I2C connections (SDA=21, SCL=22)");
        Serial.println("Restarting in 30 seconds...");
        delay(30000);
        ESP.restart();
        return;
    }
    
    if (rtc.lostPower()) {
        Serial.println("[RTC] WARNING: RTC lost power, time may be incorrect!");
    }
    
    // Display current RTC time
    DateTime now = rtc.now();
    Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
    
    // Initialize BLE
    Serial.println("[BLE] Initializing BLE...");
    NimBLEDevice::init("ESP32-GoPro");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // Scan for GoPro
    NimBLEAddress* pGoProAddress = scanForGoPro();
    if (pGoProAddress == nullptr) {
        Serial.println("\n[ERROR] No GoPro found. Please ensure:");
        Serial.println("  1. GoPro is powered on");
        Serial.println("  2. GoPro Bluetooth is enabled");
        Serial.println("  3. GoPro is in pairing mode");
        Serial.println("\nRestarting in 30 seconds...");
        delay(30000);
        ESP.restart();
        return;
    }
    
    // Connect via BLE
    if (!connectToGoPro(pGoProAddress)) {
        Serial.println("\n[ERROR] Failed to connect to GoPro via BLE");
        Serial.println("Restarting in 30 seconds...");
        delay(30000);
        ESP.restart();
        return;
    }
    
    // Get WiFi credentials (using direct characteristic reads)
    Serial.println("\n[BLE] Reading WiFi credentials...");
    delay(500);  // Give connection time to stabilize
    
    if (!getWiFiSSID() || !getWiFiPassword()) {
        Serial.println("\n[ERROR] Failed to get WiFi credentials");
        pClient->disconnect();
        delay(30000);
        ESP.restart();
        return;
    }
    
    // Enable WiFi AP
    if (!enableWiFiAP()) {
        Serial.println("\n[ERROR] Failed to enable WiFi AP");
        pClient->disconnect();
        delay(30000);
        ESP.restart();
        return;
    }
    
    // Wait for AP to be ready
    if (!waitForAPMode()) {
        Serial.println("\n[ERROR] WiFi AP did not become ready");
        pClient->disconnect();
        delay(30000);
        ESP.restart();
        return;
    }
    
    Serial.println("\n[SUCCESS] GoPro WiFi AP is ready!");
    Serial.printf("  SSID: %s\n", goProSSID.c_str());
    Serial.printf("  Password: %s\n", goProPassword.c_str());
    
    // Disconnect BLE (we'll use WiFi now)
    Serial.println("\n[BLE] Disconnecting BLE...");
    pClient->disconnect();
    delay(1000);
    
    // Connect to GoPro WiFi
    if (!connectToGoProWiFi()) {
        Serial.println("\n[ERROR] Failed to connect to GoPro WiFi");
        delay(30000);
        ESP.restart();
        return;
    }
    
    // Set date/time via HTTP
    delay(1000); // Give WiFi connection time to stabilize
    if (setGoProDateTime()) {
        Serial.println("\n[SUCCESS] Date/time synchronized!");
        beep();  // Confirmation beep
    } else {
        Serial.println("\n[WARNING] Failed to set date/time via HTTP");
        Serial.println("The WiFi AP is still available for manual control.");
    }
    
    Serial.println("\n==================================");
    Serial.println("Setup complete!");
    Serial.println("==================================\n");
}

// Full reconnection routine (BLE + WiFi)
bool reconnectToGoPro() {
    Serial.println("\n[RECONNECT] Starting full reconnection routine...");
    
    // Disconnect everything first
    WiFi.disconnect();
    if (pClient && pClient->isConnected()) {
        pClient->disconnect();
    }
    delay(2000);
    
    // Step 1: Scan for GoPro
    Serial.println("[RECONNECT] Scanning for GoPro...");
    NimBLEAddress* pGoProAddress = scanForGoPro();
    if (pGoProAddress == nullptr) {
        Serial.println("[RECONNECT] GoPro not found (may be powered off)");
        return false;
    }
    
    // Step 2: Connect via BLE
    Serial.println("[RECONNECT] Connecting via BLE...");
    if (!connectToGoPro(pGoProAddress)) {
        Serial.println("[RECONNECT] BLE connection failed");
        return false;
    }
    
    // Step 3: Read WiFi credentials (in case they changed)
    if (!getWiFiSSID() || !getWiFiPassword()) {
        Serial.println("[RECONNECT] Failed to get WiFi credentials");
        pClient->disconnect();
        return false;
    }
    
    // Step 4: Enable WiFi AP
    if (!enableWiFiAP()) {
        Serial.println("[RECONNECT] Failed to enable WiFi AP");
        pClient->disconnect();
        return false;
    }
    
    // Step 5: Wait for AP to be ready
    if (!waitForAPMode()) {
        Serial.println("[RECONNECT] WiFi AP did not become ready");
        pClient->disconnect();
        return false;
    }
    
    Serial.println("[RECONNECT] GoPro WiFi AP is ready!");
    
    // Step 6: Disconnect BLE
    pClient->disconnect();
    delay(1000);
    
    // Step 7: Connect to GoPro WiFi
    if (!connectToGoProWiFi()) {
        Serial.println("[RECONNECT] Failed to connect to GoPro WiFi");
        return false;
    }
    
    Serial.println("[RECONNECT] Successfully reconnected!");
    return true;
}

void loop() {
    static bool wasConnected = true;
    static unsigned long lastReconnectAttempt = 0;
    static unsigned long lastSync = 0;
    
    // Check WiFi connection status
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    
    // Detect WiFi disconnect (GoPro powered off)
    if (wasConnected && !isConnected) {
        Serial.println("\n========================================");
        Serial.println("[ALERT] WiFi disconnected!");
        Serial.println("GoPro may have powered off or restarted");
        Serial.println("========================================");
        wasConnected = false;
    }
    
    // If disconnected, try full reconnection every 30 seconds
    if (!isConnected && (millis() - lastReconnectAttempt > 30000)) {
        lastReconnectAttempt = millis();
        
        if (reconnectToGoPro()) {
            wasConnected = true;
            
            // Sync time immediately after reconnection
            Serial.println("\n[SYNC] Synchronizing time after reconnection...");
            if (setGoProDateTime()) {
                Serial.println("[SUCCESS] Time synchronized!");
                beep();  // Confirmation beep
                lastSync = millis();
            } else {
                Serial.println("[WARNING] Time sync failed, but connection is established");
            }
        } else {
            Serial.println("\n[INFO] Reconnection failed, will retry in 30 seconds...");
        }
    }
    
    // If connected, perform periodic time sync every hour
    if (isConnected && (millis() - lastSync > 3600000)) { // 1 hour
        Serial.println("\n[INFO] Performing periodic time sync...");
        if (setGoProDateTime()) {
            Serial.println("[SUCCESS] Periodic time sync complete!");
            beep();  // Confirmation beep
            lastSync = millis();
        } else {
            Serial.println("[WARNING] Periodic time sync failed");
        }
    }
    
    delay(1000);
}



