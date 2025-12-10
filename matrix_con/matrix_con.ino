#include <M5Atom.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_now.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include "config.h"
#include "Participant.h"

// --- CONFIGURATION ---
WebServer server(80);
const int SERVER_PORT = 80; 
uint8_t echoAddress[] = {0x90, 0x15, 0x06, 0xFD, 0xF2, 0xF8}; 

// ปรับระยะตรงนี้ (ยิ่งลบเยอะ ยิ่งไกล)
const int RSSI_THRESHOLD = -30; 
BLEUUID targetUUID = BLEUUID("1234");
int checkIcon[] = { 15, 21, 17, 13, 9 };

BLEScan* pBLEScan;
Participant currentUser; 
bool hasTriggered = false;

// --- DATA STRUCTURE ---
typedef struct struct_message {
    char type[10];      
    char username[50];  
    int status;
} struct_message;

struct_message myData; 

// --- CALLBACKS ---
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // Debug sending status if needed
}

void OnDataRecv(const esp_now_recv_info_t * info, const uint8_t *incomingData, int len) {
    if (len > 0 && *incomingData == 3) {
        currentUser.isAuthenticated = true; 
        currentUser.alertText = "Confirmed by Echo";
        Serial.println(">>> VERIFIED CONFIRMED BY ECHO! <<<");
    }
}

// --- HTTP FUNCTION ---
void sendAuthStartToStickC() {
    if(WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String myMac = WiFi.macAddress();
        String url = "http://" + IP_STICKC.toString() + ":" + String(SERVER_PORT) + "/auth_start?sender_mac=" + myMac;
        
        Serial.printf("[HTTP] Requesting: %s\n", url.c_str());

        http.begin(url);
        http.setConnectTimeout(3000); 
        int httpCode = http.GET();
        http.end();
    }
}

void setup() {
    M5.begin(true, false, true); 
    delay(100);
    Serial.begin(115200);
    Serial.println("\n\n--- M5Atom Scanner Started ---");

    // 1. WiFi Setup
    WiFi.mode(WIFI_STA);
    WiFi.config(IP_ATOM_MATRIX, IP_STATION1_AP, NETMASK, IP_STATION1_AP);
    WiFi.begin(AP_SSID, AP_PASSWORD);
    
    M5.dis.fillpix(0xFF0000); 
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected!");

    // 2. ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init Failed!");
        ESP.restart();
    }
    
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    esp_now_peer_info_t peerInfo = {}; 
    memcpy(peerInfo.peer_addr, echoAddress, 6);
    peerInfo.channel = 1; 
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("[ESP-NOW] Failed to add Echo peer");
    }

    // 3. BLE Setup
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan(); 
    pBLEScan->setActiveScan(true); 
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    currentUser.reset();

    server.on(ENDPOINT_HEARTBEAT, [](){
        server.send(200, "text/plain", "OK");
    });

    server.on(ENDPOINT_RESET_GLOBAL, [](){
        server.send(200, "text/plain", "RESETTING");
        delay(100);
        ESP.restart(); // สั่งรีบูตตัวเอง
    });

    server.begin();
    
    M5.dis.fillpix(0x0000FF); // Ready (Blue)
    Serial.println("[SYSTEM] Ready. Start Scanning...");


}

void loop() {
    M5.update();
    server.handleClient();
    
    // 1. ถ้าได้รับยืนยันแล้ว จบงาน
    if (currentUser.isAuthenticated) {
        M5.dis.clear();
        for (int i = 0; i < 5; i++) M5.dis.drawpix(checkIcon[i], 0x00FF00); 
        Serial.println("[SYSTEM] Task Complete. Halting.");
        while(true) { 
            M5.update(); 
            delay(100); 
            if (M5.Btn.wasPressed()) ESP.restart();
        } 
    }

    // 2. สแกนหาอุปกรณ์
    if (!hasTriggered) {
        BLEScanResults *foundDevices = pBLEScan->start(1, false); 
        bool foundTarget = false;
        
        for (int i = 0; i < foundDevices->getCount(); i++) {
            BLEAdvertisedDevice device = foundDevices->getDevice(i);
            
            if (device.haveServiceUUID() && device.isAdvertisingService(targetUUID)) {
                 int rssi = device.getRSSI();
                 if (rssi > RSSI_THRESHOLD) {
                    foundTarget = true;
                    
                    // --- [ADDED] ดึงชื่อ Username มา Debug ---
                    String bleName = "Unknown";
                    if (device.haveName()) {
                        bleName = device.getName().c_str();
                    }
                    
                    // บันทึกชื่อลง Object เพื่อเตรียมส่ง
                    currentUser.Username = bleName;
                    
                    // Debug ออกมาดูชัดๆ
                    Serial.println("\n--------------------------------");
                    Serial.printf("[BLE] TARGET FOUND!\n");
                    Serial.printf("[BLE] Name (Username): %s\n", bleName.c_str());
                    Serial.printf("[BLE] RSSI: %d\n", rssi);
                    Serial.println("--------------------------------\n");
                 }
                 break; 
            }
        }
        
        // 3. เจอเป้าหมาย -> ส่งข้อมูล
        if (foundTarget) {
            hasTriggered = true; 
            M5.dis.fillpix(0xFFFF00); // Processing (Yellow)

            // --- A. เตรียมข้อมูล ESP-NOW ---
            strcpy(myData.type, "TRIGGER");
            // เอาชื่อที่สแกนได้ใส่ลงไปในแพ็คเกจ
            strncpy(myData.username, currentUser.Username.c_str(), sizeof(myData.username) - 1); 
            myData.status = 1;

            // --- B. ส่งไปที่ Echo ---
            esp_now_send(echoAddress, (uint8_t *) &myData, sizeof(myData));
            Serial.printf("[ESP-NOW] Sent TRIGGER with User: %s\n", myData.username);
            
            // --- C. แจ้งเตือน HTTP ---
            sendAuthStartToStickC();
            
        } 

        pBLEScan->clearResults(); 
    }
}