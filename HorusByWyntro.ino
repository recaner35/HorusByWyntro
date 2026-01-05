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
#include <NetworkClientSecure.h> // Eklendi: Core 3.x SSL için
#include <Update.h>
#include <WiFi.h>
#include <esp_now.h>


// ===============================
// Motor Pin Tanımlamaları
// ===============================
// ... (defines remain same)

// ... (globals remain same)

// ===============================
// FONKSİYON PROTOTİPLERİ (Full List)
// ===============================
void loadConfig();
void saveConfig();
void initWiFi();
void initMotor();
void initWebServer();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);
void processCommand(String jsonStr);
void updateSchedule();
void handlePhysicalControl();
void checkSchedule();
void startMotorTurn(); // Missing
void handleWifiScan();
void handleWifiConnection();
String getWifiListJson();
void initESPNow();
void broadcastDiscovery();
void broadcastStatus();
void sendToPeer(String targetMac, String command, String action);
bool execOTA(String url, int command);
void checkAndPerformUpdate();

// ===============================
// OTA Helper Functions
// ===============================

// OTA Helper: URL'den stream update
bool execOTA(String url, int command) {
  NetworkClientSecure client; // Düzeltme: Secure Client ayrı tanımlanmalı
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url); // Düzeltme: Client parametre olarak verilmeli
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

  NetworkClientSecure client; // Düzeltme
  client.setInsecure();

  HTTPClient http;
  http.begin(client, GITHUB_VERSION_URL); // Düzeltme

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, payload);

    String newVersion = doc["version"];
    String currentVersion = FIRMWARE_VERSION;

    Serial.println("Mevcut: " + currentVersion + ", Yeni: " + newVersion);

    if (newVersion != currentVersion) {
      Serial.println("Yeni surum bulundu! Guncelleniyor...");

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
// server.on("/api/ota-auto", HTTP_POST, [](AsyncWebServerRequest *request) {
//    request->send(200, "application/json", "{\"status\":\"started\"}");
//    shouldUpdate = true;
// });

// Global flag
// bool shouldUpdate = false; // Already defined in globals section

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
