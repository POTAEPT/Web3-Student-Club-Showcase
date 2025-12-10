#include <M5Unified.h>
#include <esp_now.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <string.h>
#include "config.h"
#include "Participant.h"

WebServer server(80);

// --- 1. CONFIGURATION: MAC ADDRESSES ---
uint8_t matrixAddress[]   = {0x4C, 0x75, 0x25, 0xAD, 0xB5, 0xBC};
uint8_t core2Address[]    = {0x2C, 0xBC, 0xBB, 0x82, 0x91, 0xA8};
uint8_t stickc1Address[]  = {0x00, 0x4B, 0x12, 0xC4, 0x2D, 0xF8};
uint8_t stickc2Address[]  = {0x00, 0x4b, 0x12, 0xC4, 0x35, 0x48};

// --- 2. DATA STRUCTURE ---
typedef struct struct_message {
    char type[10];      
    char username[50];  
    int status;
} struct_message;

struct_message incomingDataBuffer; // ตัวแปรรับข้อมูลชั่วคราว
char activeUser[50];               // [ADDED] ตัวแปรเก็บชื่อผู้ใช้ที่จะส่งต่อ

// --- 3. SOUND SEQUENCER VARIABLES ---
const int shortBeepDuration = 200;
const int longBeepDuration  = 700;
const int beepFreq          = 1319; 
const int pauseDuration     = 100;  

enum SoundState { 
    IDLE, 
    BEEP1_START, BEEP1_WAIT, BEEP1_PAUSE,
    BEEP2_START, BEEP2_WAIT, BEEP2_PAUSE,
    BEEP3_START, BEEP3_WAIT, 
    DONE,        
    RESET_WAIT   
};

SoundState currentSoundState = IDLE;
unsigned long stateChangeTime = 0;

// ------------------------------------------------------
// 📤 ESP-NOW SENDER
// ------------------------------------------------------
void sendRequestToAll(const char* type, const char* username, int status) {
    struct_message msg;
    strcpy(msg.type, type);
    strcpy(msg.username, username); // ส่งชื่อที่ระบุออกไป
    msg.status = status;

    esp_now_send(core2Address, (uint8_t *) &msg, sizeof(msg));
    esp_now_send(stickc1Address, (uint8_t *) &msg, sizeof(msg));
    esp_now_send(stickc2Address, (uint8_t *) &msg, sizeof(msg));

    uint8_t matrixData = 3;
    esp_now_send(matrixAddress, &matrixData, sizeof(matrixData));

    Serial.printf("[ESP-NOW] Sent '%s' for User: %s\n", type, username);
}

// ------------------------------------------------------
// 📥 ESP-NOW RECEIVER
// ------------------------------------------------------
void OnDataRecv(const esp_now_recv_info_t * info, const uint8_t *dataPtr, int len) {
    if (len == sizeof(struct_message)) {
        memcpy(&incomingDataBuffer, dataPtr, sizeof(struct_message));

        Serial.printf("[RECV] Type: %s | User: %s\n", incomingDataBuffer.type, incomingDataBuffer.username);

        // --- TRIGGER LOGIC ---
        if (strcmp(incomingDataBuffer.type, "TRIGGER") == 0 && currentSoundState == IDLE) {
            
            // [IMPORTANT] เก็บชื่อผู้ใช้ไว้ส่งต่อตอนเสียงจบ
            strcpy(activeUser, incomingDataBuffer.username);
            
            currentSoundState = BEEP1_START; 
            Serial.printf(">> SEQUENCE START for: %s <<\n", activeUser);
        }
    }
}

// ------------------------------------------------------
// SETUP
// ------------------------------------------------------
void setup() {
    auto cfg = M5.config();
    
    // --- 1. ตั้งค่าพื้นฐาน ---
    cfg.serial_baudrate = 115200; 
    
    // สำคัญมาก: Atom Echo ต้องเปิด output_power เพื่อจ่ายไฟให้ลำโพง (GPIO21/25)
    cfg.output_power = true;      
    
    // *** ลบบรรทัด cfg.external_speaker.atomic_echo = true; ออกครับ ***
    // ให้ M5Unified ตรวจสอบ Board อัตโนมัติ (มันฉลาดพอจะรู้ว่าเป็น Atom Echo)

    M5.begin(cfg);
    
    // --- 2. ตั้งค่าเสียง ---
    M5.Speaker.begin(); // สั่งเริ่มระบบเสียงให้ชัวร์
    M5.Speaker.setVolume(200);
    
    // --- 3. TEST SOUND (ทดสอบทันทีที่เปิดเครื่อง) ---
    // ถ้าบรรทัดนี้ไม่ดัง แสดงว่าฮาร์ดแวร์มีปัญหา หรือไฟไม่พอ
    Serial.println("Testing Speaker...");
    M5.Speaker.tone(1000, 500); 
    delay(1000); // รอฟังเสียง
    M5.Speaker.tone(2000, 500);
    
    // --- 4. ตั้งค่าจอและ WiFi (เหมือนเดิม) ---
    M5.Display.fillScreen(0x0000FF); 
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    Serial.println("\n--- M5Atom Echo Started ---");
    Serial.print("MAC: "); Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        ESP.restart();
    }

    esp_now_register_recv_cb(OnDataRecv);

    // ... (ส่วน Add Peer และ Server เหมือนเดิม) ...
    auto addPeer = [](const uint8_t* addr) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, addr, 6);
        peerInfo.channel = 1; 
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            Serial.println("Failed to add peer");
        }
    };

    addPeer(core2Address);
    addPeer(matrixAddress);
    addPeer(stickc1Address);
    addPeer(stickc2Address);

    server.begin();
}

// ------------------------------------------------------
// MAIN LOOP
// ------------------------------------------------------
void loop() {
    M5.update();
    server.handleClient();
    unsigned long currentTime = millis();

    if (currentSoundState != IDLE) {
    switch (currentSoundState) {
        
        // --- เสียงที่ 1 (สั้น) ---
        case BEEP1_START:
            M5.Display.fillScreen(0xFFFF00);
            M5.Speaker.tone(beepFreq); // สั่งดังค้างไว้เลย ไม่ต้องใส่ duration
            stateChangeTime = currentTime;
            currentSoundState = BEEP1_WAIT;
            break;

        case BEEP1_WAIT:
            if (currentTime - stateChangeTime >= shortBeepDuration) {
                M5.Speaker.stop(); // *** สั่งหยุดเสียงเองเมื่อครบเวลา ***
                stateChangeTime = currentTime;
                currentSoundState = BEEP1_PAUSE;
            }
            break;

        case BEEP1_PAUSE:
            if (currentTime - stateChangeTime >= pauseDuration) {
                currentSoundState = BEEP2_START;
            }
            break;
        
        // --- เสียงที่ 2 (สั้น) ---
        case BEEP2_START:
            M5.Speaker.tone(beepFreq); // สั่งดัง
            stateChangeTime = currentTime;
            currentSoundState = BEEP2_WAIT;
            break;

        case BEEP2_WAIT:
            if (currentTime - stateChangeTime >= shortBeepDuration) {
                M5.Speaker.stop(); // *** สั่งหยุด ***
                stateChangeTime = currentTime;
                currentSoundState = BEEP2_PAUSE;
            }
            break;

        case BEEP2_PAUSE:
            if (currentTime - stateChangeTime >= pauseDuration) {
                currentSoundState = BEEP3_START;
            }
            break;

        // --- เสียงที่ 3 (ยาว) ---
        case BEEP3_START:
            M5.Speaker.tone(beepFreq); // สั่งดัง
            stateChangeTime = currentTime;
            currentSoundState = BEEP3_WAIT;
            break;
            
        case BEEP3_WAIT:
            if (currentTime - stateChangeTime >= longBeepDuration) {
                M5.Speaker.stop(); // *** สั่งหยุด ***
                currentSoundState = DONE;
            }
            break;

        case DONE:
            M5.Display.fillScreen(0x00FF00); 
            
            // ส่งข้อมูลออกไป
            sendRequestToAll("AUTH", activeUser, 1);
            
            stateChangeTime = currentTime;
            currentSoundState = RESET_WAIT; 
            break;

        case RESET_WAIT:
            if (currentTime - stateChangeTime >= 2000) {
                M5.Display.fillScreen(0x0000FF); 
                currentSoundState = IDLE;
                strcpy(activeUser, ""); 
                Serial.println(">> READY <<");
            }
            break;
    }
}

    // Manual Trigger
    if (M5.BtnA.wasPressed() && currentSoundState == IDLE) {
        Serial.println("Manual Button Trigger!");
        strcpy(activeUser, "Manual_User"); // ตั้งชื่อสมมติกรณีทดสอบกดปุ่มเอง
        currentSoundState = BEEP1_START;
    }
}