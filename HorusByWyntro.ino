/**
 * PROJE ADI: HORUS BY WYNTRO
 * ROL: Firmware
 * DONANIM: ESP32 WROOM, 28BYJ-48 (ULN2003), TTP223
 *
 * AÇIKLAMA:
 * Bu kod, Horus IoT Saat Kurma Makinesi'nin (Watch Winder) ana yazılımıdır.
 * Web arayüzü ile kontrol edilir, ESP-NOW ile diğer cihazları görür,
 * TTP223 sensörü ile fiziksel kontrol sağlar.
 */

#include <AccelStepper.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_now.h>

// ===============================
// Motor Pin Tanımlamaları
// ===============================
// 28BYJ-48 için pin sırası: IN1-IN3-IN2-IN4 şeklinde olmalı (AccelStepper
// FULL4WIRE için)
#define MOTOR_PIN_1 19
#define MOTOR_PIN_2 18
#define MOTOR_PIN_3 5
#define MOTOR_PIN_4 17

// ===============================
// Sensör ve Buton Tanımlamaları
// ===============================
#define TOUCH_PIN 23 // TTP223 Dokunmatik Sensör
#define LED_PIN 2    // Built-in LED

// ===============================
// Sabitler ve Ayarlar
// ===============================
#define STEPS_PER_REVOLUTION 2048 // 28BYJ-48 adım sayısı (Yaklaşık)
#define JSON_CONFIG_FILE "/config.json"
#define GITHUB_VERSION_URL                                                     \
  "https://raw.githubusercontent.com/recaner35/HorusByWyntro/main/"            \
  "version.json"
#define FIRMWARE_VERSION "1.0.0"

// ===============================
// Nesneler
// ===============================
// Motor nesnesi (FULL4WIRE modu) - Pin sırası 1-3-2-4 ÖNEMLİ
AccelStepper stepper(AccelStepper::HALF4WIRE, MOTOR_PIN_1, MOTOR_PIN_3,
                     MOTOR_PIN_2, MOTOR_PIN_4);

// Web Sunucusu (Port 80)
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===============================
// Global Değişkenler (Durum)
// ===============================
// Kullanıcı Ayarları
struct Config {
  int tpd = 600;     // Günlük Tur Sayısı (600-1800)
  int duration = 15; // Bir tur süresi (sn) (10-20)
  int direction = 0; // 0: CW, 1: CCW, 2: Bi-Directional
  String hostname = "";
};
Config config;

// Çalışma Durumu
bool isRunning = false;     // Sistem genel olarak aktif mi?
bool isMotorMoving = false; // Motor şu an dönüyor mu?
unsigned long sessionStartTime = 0;
int turnsThisHour = 0;      // Bu saatlik dilimde atılan tur
int targetTurnsPerHour = 0; // Saat başı hedef tur
unsigned long lastTouchTime = 0;
bool touchState = false;

// Zamanlayıcılar
unsigned long lastHourCheck = 0;
unsigned long hourDuration = 3600000; // 1 Saat (ms)

// ESP-NOW Peer Listesi
struct PeerDevice {
  String mac;
  String name;
  unsigned long lastSeen;
};
std::vector<PeerDevice> peers;
#define PEER_TIMEOUT 60000 // 60s görmezsek sileriz

// ESP-NOW Paket Yapısı
typedef struct struct_message {
  char type[10]; // "DISCOVER", "STATUS", "CMD"
  char sender_mac[20];
  char sender_name[32];
  char payload[64]; // JSON komut veya durum
} struct_message;

struct_message myData;
struct_message incomingData;

// ===============================
// WiFi & Connection State
// ===============================
bool wifiScanning = false;
long lastWifiCheck = 0;
String wifiStatusStr = "disconnected";
bool tryConnect = false;
unsigned long connectStartTime = 0;
String myMacAddress;
bool shouldUpdate = false; // Flag for Auto OTA in loop

// ===============================
// FONKSİYON PROTOTİPLERİ
// ===============================
// ... helper functions ...

// OTA Helper: URL'den stream update
bool execOTA(String url, int command) {
  HTTPClient http;
  http.setInsecure(); // GitHub HTTPS için
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode == 200) {
    int contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength, command);

    if (canBegin) {
      Serial.println("OTA Basliyor: " + url);
      size_t written = Update.writeStream(http.getStream());

      if (written == contentLength) {
        Serial.println("Yazma basarili: " + String(written) + "/" +
                       String(contentLength));
      } else {
        Serial.println("Yazma hatasi: " + String(written) + "/" +
                       String(contentLength));
        return false;
      }

      if (Update.end()) {
        Serial.println("OTA Tamamlandi");
        if (Update.isFinished()) {
          return true;
        } else {
          Serial.println("OTA Bitti ama basarisiz (isFinished=false)");
          return false;
        }
      } else {
        Serial.println("OTA Hatasi: " + String(Update.getError()));
        return false;
      }
    } else {
      Serial.println("OTA Baslatilamadi (Yetersiz alan?)");
      return false;
    }
  } else {
    Serial.println("HTTP Hatasi: " + String(httpCode));
    return false;
  }
  http.end();
  return false;
}

void checkAndPerformUpdate() {
  Serial.println("Guncelleme kontrol ediliyor...");
  HTTPClient http;
  http.setInsecure();
  http.begin(GITHUB_VERSION_URL);

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, payload);

    String newVersion = doc["version"];
    String currentVersion = FIRMWARE_VERSION;

    Serial.println("Mevcut: " + currentVersion + ", Yeni: " + newVersion);

    // Semver karsilastirmasi basitce string olarak farkliysa yapalim
    // (Daha karmasik bir kiyaslama gerekirse eklenebilir)
    if (newVersion != currentVersion) {
      Serial.println("Yeni surum bulundu! Guncelleniyor...");

      // Dosya URL'lerini olustur
      String baseUrl =
          "https://github.com/recaner35/HorusByWyntro/releases/download/" +
          newVersion + "/";
      String fsUrl = baseUrl + "HorusByWyntro.littlefs.bin";
      String fwUrl = baseUrl + "HorusByWyntro.ino.bin";

      // 1. LittleFS Guncelle
      Serial.println("LittleFS indiriliyor: " + fsUrl);
      if (execOTA(fsUrl, U_SPIFFS)) {
        Serial.println("LittleFS guncellendi.");
      } else {
        Serial.println("LittleFS guncelleme hatasi!");
        // FS hatasi kritik degil, devam edebiliriz veya durabiliriz.
      }

      // 2. Firmware Guncelle
      Serial.println("Firmware indiriliyor: " + fwUrl);
      if (execOTA(fwUrl, U_FLASH)) {
        Serial.println("Firmware guncellendi. Yeniden baslatiliyor...");
        ESP.restart();
      } else {
        Serial.println("Firmware guncelleme hatasi!");
      }

    } else {
      Serial.println("Zaten guncel.");
    }
  } else {
    Serial.println("Version dosyasi alinamadi");
  }
  http.end();
}

// ... inside initWebServer ...
// (Removed placeholders)

// ... inside loop ...
// (Removed placeholders)

// ===============================
// Kurulum (Setup)
// ===============================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nHORUS BY WYNTRO - Başlatılıyor...");

  if (!LittleFS.begin(true)) {
    Serial.println("HATA: LittleFS başlatılamadı!");
    return;
  }

  loadConfig();
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  initMotor();
  initWiFi();
  initESPNow();
  initWebServer();

  if (MDNS.begin(config.hostname.c_str())) {
    Serial.println("mDNS Başlatıldı: " + config.hostname + ".local");
  }

  updateSchedule();
  // Açılışta ben buradayım de
  broadcastDiscovery();
}

// ===============================
// Ana Döngü (Loop)
// ===============================
void loop() {
  if (shouldUpdate) {
    shouldUpdate = false;
    checkAndPerformUpdate();
  }

  ws.cleanupClients();
  handleWifiScan();
  handleWifiConnection();
  handlePhysicalControl();

  if (isRunning) {
    checkSchedule();
  } else {
    if (stepper.distanceToGo() == 0) {
      stepper.disableOutputs();
    }
  }

  if (isRunning && isMotorMoving) {
    stepper.run();
    if (stepper.distanceToGo() == 0) {
      isMotorMoving = false;
      turnsThisHour++;
      if (turnsThisHour >= targetTurnsPerHour) {
        stepper.disableOutputs();
      }
    }
  }

  // Peer Temizliği (Timeout olanları sil)
  static unsigned long lastPrune = 0;
  if (millis() - lastPrune > 10000) {
    lastPrune = millis();
    for (int i = peers.size() - 1; i >= 0; i--) {
      if (millis() - peers[i].lastSeen > PEER_TIMEOUT) {
        peers.erase(peers.begin() + i);
        // Listeyi arayüze güncelle
        String json = "{ \"peers\": [";
        for (int j = 0; j < peers.size(); j++) {
          if (j > 0)
            json += ",";
          json += "{\"mac\":\"" + peers[j].mac + "\",\"name\":\"" +
                  peers[j].name + "\"}";
        }
        json += "] }";
        ws.textAll(json);
      }
    }
  }
}

// ... unchanged functions ...

void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });
  server.on("/languages.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/languages.js", "application/javascript");
  });
  server.on("/api/wifi-scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!wifiScanning) {
      WiFi.scanNetworks(true);
      wifiScanning = true;
      request->send(202, "application/json", "{\"status\":\"scanning\"}");
    } else
      request->send(200, "application/json", "{\"status\":\"busy\"}");
  });
  server.on("/api/wifi-list", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getWifiListJson());
  });
  server.on("/api/wifi-connect", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) && request->hasParam("pass", true)) {
      WiFi.begin(request->getParam("ssid", true)->value().c_str(),
                 request->getParam("pass", true)->value().c_str());
      request->send(200, "application/json", "{\"status\":\"started\"}");
    } else
      request->send(400);
  });

  // Auto OTA Endpoint
  server.on("/api/ota-auto", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"started\"}");
    shouldUpdate = true;
  });

  // OTA Update
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/update.html", "text/html");
  });
  server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if (shouldReboot)
          ESP.restart();
      },
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
        if (!index)
          Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
        if (!Update.hasError())
          Update.write(data, len);
        if (final)
          Update.end(true);
      });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
}
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    // Tüm durumu gönder
    String json = "{\"running\":" + String(isRunning ? "true" : "false") +
                  ",\"tpd\":" + String(config.tpd) +
                  ",\"dur\":" + String(config.duration) +
                  ",\"dir\":" + String(config.direction) + "}";
    client->text(json);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {
      data[len] = 0;
      processCommand(String((char *)data));
    }
  }
}
