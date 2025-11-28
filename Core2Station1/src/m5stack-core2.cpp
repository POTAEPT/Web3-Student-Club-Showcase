#include <M5Unified.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <qrcode.h>
#include <HTTPClient.h>
#include <DNSServer.h> // Library สำหรับทำ Captive Portal

// --- CENTRALIZED CONFIGURATION ---
// IMPORTANT: ถ้า StickC ได้รับ IP Address อื่นที่ไม่ใช่ 192.168.4.2 
// ให้เปลี่ยนค่า STICKC_IP เป็น IP Address จริงที่ StickC ได้รับ
const char *SSID = "Web3Showcase_AP";
const char *PASSWORD = "12345678";
const char* STICKC_IP = "192.168.4.2"; 
const int STICKC_PORT = 88;

IPAddress localIP(192, 168, 4, 1); // AP IP Address (Core2)
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);
String capturedUsername = ""; 

DNSServer dnsServer;
const byte DNS_PORT = 53;

// --- HTML for Captive Portal ---
const char* CAPTIVE_HTML = R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Web3 Student Club Identity</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }
        input[type=text], button { padding: 10px; margin: 5px 0; border-radius: 5px; border: 1px solid #ccc; width: 80%; max-width: 300px; }
        button { background-color: #4CAF50; color: white; cursor: pointer; }
    </style>
</head>
<body>
    <h1>STATION 1: Identity Creation</h1>
    <form action="/submit" method="POST">
        <label for="username">Enter Your Username:</label><br>
        <input type="text" id="username" name="username" placeholder="Username (e.g. Satoshi_N)" required><br>
        <button type="submit">Submit</button>
    </form>
</body>
</html>
)raw";

// --- CORE FUNCTIONS ---

// 7a. Core2 ส่งข้อมูล Username ที่ได้รับมาไปยัง StickC-Plus2 ผ่าน Fixed IP
void sendUsernameToStickC(String username) {
    HTTPClient http;
    String url = "http://" + String(STICKC_IP) + ":" + String(STICKC_PORT) + "/set_username"; 

    http.begin(url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "username=" + username;

    int httpResponseCode = http.POST(postData);

    M5.Lcd.setCursor(5, 100);
    if (httpResponseCode > 0) {
        M5.Lcd.printf("Sent to StickC (Code: %d)", httpResponseCode);
    } else {
        M5.Lcd.printf("StickC Request Failed (Error: %s)", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
}

// 2. แสดง QR code สำหรับการเชื่อมต่อ Wi-Fi
void displayQRCode(const char* data) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextDatum(top_center);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setFont(&fonts::Font4); 
    M5.Lcd.drawString("1. Connect to Wi-Fi", M5.Lcd.width() / 2, 10); 

    int size = 180;
    int x = (M5.Lcd.width() - size) / 2;
    int y = 50;
    
    M5.Lcd.qrcode(data, x, y, size);
    
    M5.Lcd.setTextDatum(top_left);
    M5.Lcd.setCursor(5, 240);
    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.print("2. Enter Username in Browser");
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Lcd.setRotation(1); 

    // 0b. Core2 สร้าง WiFi Access Point (SoftAP mode)
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(localIP, gateway, subnet);
    WiFi.softAP(SSID, PASSWORD);
    
    // แสดง IP และ MAC Address ของ Core2
    M5.Lcd.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    M5.Lcd.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());

    // 2. สร้าง QR Code Data
    String qrData = "WIFI:S:" + String(SSID) + ";T:WPA;P:" + String(PASSWORD) + ";;";
    displayQRCode(qrData.c_str());

    // 3. ตั้งค่า DNS Server สำหรับ Captive Portal (ดักจับทุก request และส่งไปที่ localIP)
    dnsServer.start(DNS_PORT, "*", localIP);

    // --- WEB SERVER HANDLERS ---
    
    // C1. Redirect Handlers (บังคับให้ Pop-up ขึ้นบน iOS/Android)
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        // iOS/Android ใช้หน้านี้: Redirect ไปหน้าหลัก
        request->redirect("http://" + WiFi.softAPIP().toString() + "/");
    });
    
    // C2. Redirect Handlers (สำหรับ Windows)
    server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Windows ใช้หน้านี้: Redirect ไปหน้าหลัก
        request->redirect("http://" + WiFi.softAPIP().toString() + "/");
    });
    
    // A. Handle Root Path
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", CAPTIVE_HTML); 
    });

    // B. Handle Submission
    server.on("/submit", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("username", true)) {
            String username = request->getParam("username", true)->value();

            M5.Lcd.fillScreen(BLACK);
            M5.Lcd.setTextDatum(top_left);
            M5.Lcd.setFont(&fonts::Font2);
            M5.Lcd.printf("Username Received: %s", username.c_str());
            
            request->send(200, "text/plain", "Username '" + username + "' submitted successfully. Please wait for StickC update.");
            
            // 7a. ส่งข้อมูล Username ไปให้ StickC
            sendUsernameToStickC(username);
            
        } else {
            request->send(400, "text/plain", "Missing Username");
        }
    });

    // ดักจับทุก Request ที่หาไม่เจอ (onNotFound) แล้ว Redirect ไปหน้าหลัก (/)
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_GET) {
            request->redirect("http://" + WiFi.softAPIP().toString() + "/");
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });

    server.begin();
    M5.Lcd.println("\nHTTP Server Started.");
}

void loop() {
    // ต้องประมวลผล DNS ตลอดเวลาเพื่อให้ Captive Portal ทำงาน
    dnsServer.processNextRequest();
    M5.update();
}