#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WebServer.h> // ใช้ WebServer มาตรฐาน
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <esp_gap_ble_api.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "config.h"      
#include "Participant.h" 

WebServer server(80);    // ประกาศ Server
Participant participant;
String alertMsg = "Ready";

// ตัวแปร Flag เพื่อบอกให้ Loop หลักวาดหน้าจอ
volatile bool needUpdateUI = false;

// --- Data Structure ---
typedef struct struct_message {
    char type[10];
    char username[50];  
    int status;
} struct_message;

struct_message incomingData;

// --- [BLE] ตัวแปร Global ---
BLEServer *pServer = NULL;
BLEAdvertising *pAdvertising = NULL;
bool shouldRestartBLE = false;
String newBLEName = "";

// ------------------------------------------------------
// 📥 ESP-NOW Callback
// ------------------------------------------------------
void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incomingDataPtr, int len) {
    
    // (สำหรับ Auth / Set User)
    if (len == sizeof(struct_message)) {
        memcpy(&incomingData, incomingDataPtr, sizeof(struct_message));
        
        if (strcmp(incomingData.type, "AUTH") == 0) {
            participant.isAuthenticated = true;
            alertMsg = "Auth by Echo"; 
            M5.Speaker.tone(4000, 200);
            needUpdateUI = true; 
        }
        else if (strcmp(incomingData.type, "USER") == 0) {
            participant.reset();
            participant.Username = String(incomingData.username);
            alertMsg = "ID Received";
            newBLEName = participant.Username;
            shouldRestartBLE = true;
            M5.Speaker.tone(2000, 200);
            needUpdateUI = true;
        }
    }
    
    // (สำหรับตัดเงิน)
    // Matrix ส่งมาแค่ตัวเลข 49, 50, 51...
    else if (len == 1) { 
        // ประกาศตัวแปรใช้แค่ในนี้ (ไม่ต้องไปประกาศข้างบน)
        uint8_t orderCode = incomingDataPtr[0]; 
        int cost = 0;

        // เช็คว่าเป็นออเดอร์ไหน แล้วกำหนดราคา
        if (orderCode == 49)      cost = 2; // (Choice 1)
        else if (orderCode == 50) cost = 3; // (Choice 2)
        else if (orderCode == 51) cost = 5; // (Choice 3)
        else if (orderCode == 52) cost = 2; // (Choice 4)

        Serial.printf("[ESP-NOW] Order Code: %d -> Cost: %d\n", orderCode, cost);

        // Logic ตัดเงิน (ใช้ตัวแปร participant ที่มีอยู่แล้ว)
        if (cost > 0) {
            if (participant.CCoin_Balance >= cost) {
                participant.CCoin_Balance -= cost;
                alertMsg = "Paid -" + String(cost);
                M5.Speaker.tone(1000, 200); 
            } else {
                alertMsg = "Not Enough!";
                M5.Speaker.tone(200, 500);
            }
            needUpdateUI = true; 
        }
    }
}

// ------------------------------------------------------
// BLE Functions
// ------------------------------------------------------
void startBLE(String name) {
    if (pAdvertising == NULL) {
        BLEDevice::init(name.c_str());
        pServer = BLEDevice::createServer();
        pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06); 
        pAdvertising->setMinPreferred(0x12);
    } else {
        pAdvertising->stop();
    }

    esp_ble_gap_set_device_name(name.c_str());

    BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
    oAdvertisementData.setFlags(0x06);
    oAdvertisementData.setCompleteServices(BLEUUID("1234")); 
    pAdvertising->setAdvertisementData(oAdvertisementData);

    BLEAdvertisementData oScanResponseData = BLEAdvertisementData();
    oScanResponseData.setName(name.c_str());
    pAdvertising->setScanResponseData(oScanResponseData);

    pAdvertising->start();
}

// ------------------------------------------------------
// 🎨 UI Function
// ------------------------------------------------------
void drawUI() {
    M5.Lcd.fillScreen(BLACK);
    
    // Battery Status
    int bat = M5.Power.getBatteryLevel();
    M5.Lcd.setTextSize(1);
    M5.Lcd.setFont(&fonts::Font2);
    
    M5.Lcd.setCursor(180, 5);
    if (bat > 20) M5.Lcd.setTextColor(GREEN, BLACK);
    else M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.printf("%d%%", bat);

    // Info: Username
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setFont(&fonts::Font4); 
    
    M5.Lcd.setCursor(5, 30);
    String displayName = (participant.Username.length() > 0) ? participant.Username : "-";
    M5.Lcd.printf("U: %s", displayName.c_str());

    // Info: Auth Status
    M5.Lcd.setCursor(5, 60);
    String st = participant.isAuthenticated ? "YES" : "NO";
    if (participant.isAuthenticated) M5.Lcd.setTextColor(GREEN, BLACK);
    else M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.printf("Auth: %s", st.c_str());

    // Info: Coin
    M5.Lcd.setCursor(5, 90);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.printf("Coin: %d", participant.CCoin_Balance);
    
    // Info: Alert Message
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(5, 115);
    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.printf("%s", alertMsg.c_str());
}

// ------------------------------------------------------
// SETUP
// ------------------------------------------------------
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Lcd.setRotation(3);
    
    Serial.begin(115200);
    
    // UI เริ่มต้น
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.setCursor(10, 50);
    M5.Lcd.print("Connecting WiFi...");

    // WiFi Setup
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(AP_SSID, AP_PASSWORD);
    
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
        delay(500);
        retry++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.config(IP_STICKC, IP_STATION1_AP, NETMASK, IP_STATION1_AP);
        Serial.println("\nWiFi Connected!");
    } else {
        Serial.println("\n[WIFI] Connect Failed!");
    }

    // ESP-NOW Init
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init Failed");
    } else {
        esp_now_register_recv_cb(OnDataRecv);
    }

    // --- Web Server Endpoints ---

    // 1. Endpoint สำหรับ Matrix แจ้งว่าเริ่ม Auth
    server.on("/auth_start", [](){
        alertMsg = "AUTH... In Progress"; 
        needUpdateUI = true;
        server.send(200, "text/plain", "ACK");
    });
    
    // 2. Endpoint สำหรับ Core Basic (Heartbeat)
    server.on(ENDPOINT_HEARTBEAT, [](){
        server.send(200, "text/plain", "OK");
    });

    // 3. Endpoint สำหรับ Core Basic (Reset)
    server.on(ENDPOINT_RESET_GLOBAL, [](){
        participant.reset();
        alertMsg = "Ready";
        newBLEName = "GUEST";
        shouldRestartBLE = true;
        needUpdateUI = true;
        
        server.send(200, "text/plain", "RESETTING");
        delay(100);
        ESP.restart(); // รีบูต
    });

    // 4. Endpoint เช็คแบตเตอรี่ (แก้ให้ถูก Syntax)
    server.on("/api/battery", [](){
        int batLevel = M5.Power.getBatteryLevel();
        server.send(200, "text/plain", String(batLevel));
    });

    // 5. Endpoint ตั้งค่า User
    server.on(ENDPOINT_SET_USER, [](){
        if (server.hasArg("username")) {
            String val = server.arg("username");
            participant.reset();
            participant.Username = val;
            alertMsg = "ID Received";
            newBLEName = val;
            shouldRestartBLE = true; 
            needUpdateUI = true;
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "Missing username");
        }
    });

    server.on("/earn_coin", [](){
        // Paper ส่งตัวแปรชื่อ "amount" มา
        if (server.hasArg("amount")) {
            int earned = server.arg("amount").toInt();
            
            if (earned > 0) {
                participant.CCoin_Balance += earned; // เพิ่มเงินเข้ากระเป๋า
                
                // แจ้งเตือนบนหน้าจอ
                alertMsg = "Earn +" + String(earned);
                
                // เสียงแจ้งเตือน (ติ๊ด-ติ๊ด)
                M5.Speaker.tone(2000, 100); 
                delay(100);
                M5.Speaker.tone(3000, 100);
                
                needUpdateUI = true; // สั่งอัปเดตหน้าจอทันที
                
                server.send(200, "text/plain", "OK"); // ตอบกลับ Paper ว่าได้รับแล้ว
                Serial.printf("Earned via HTTP: %d\n", earned);
            } else {
                server.send(400, "text/plain", "Invalid Amount");
            }
        } else {
            server.send(400, "text/plain", "Missing amount parameter");
        }
    });

    server.begin();
    startBLE("GUEST");
    drawUI(); 
}

// ------------------------------------------------------
// LOOP
// ------------------------------------------------------
void loop() {
    M5.update();
    server.handleClient(); // [สำคัญ] รับคำสั่ง Web

    if (needUpdateUI) {
        drawUI();
        needUpdateUI = false;
    }

    if (shouldRestartBLE) {
        startBLE(newBLEName);
        shouldRestartBLE = false;
    }

    // Refresh หน้าจอทุก 10 วิ (อัปเดตแบต)
    static long lastBat = 0;
    if (millis() - lastBat > 10000) {
        drawUI();
        lastBat = millis();
    }
}