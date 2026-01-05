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
#define FIRMWARE_VERSION "1.0.21"

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

// ===============================
// FONKSİYON PROTOTİPLERİ
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
void performOTA();
void handleWifiScan();
void handleWifiConnection();
String getWifiListJson();
void initESPNow();
void broadcastDiscovery();
void broadcastStatus();
void sendToPeer(String targetMac, String command, String action);

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

// ===============================
// ESP-NOW Callback
// ===============================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingDataPtr, int len) {
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));

  String type = String(incomingData.type);
  String senderMac = String(incomingData.sender_mac);
  String senderName = String(incomingData.sender_name);

  // Peer Ekle/Güncelle
  bool found = false;
  for (auto &p : peers) {
    if (p.mac == senderMac) {
      p.lastSeen = millis();
      p.name = senderName;
      found = true;
      break;
    }
  }
  if (!found) {
    PeerDevice newPeer;
    newPeer.mac = senderMac;
    newPeer.name = senderName;
    newPeer.lastSeen = millis();
    peers.push_back(newPeer);

    // ESP-NOW Peer Listesine Ekle (İletişim için gerekli)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_add_peer(&peerInfo);
    }
  }

  // Mesaj Tipi İşleme
  if (type == "CMD") {
    // Bana komut geldi (Örn: "start", "stop")
    String payload = String(incomingData.payload);
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payload);
    String action = doc["action"];

    if (action == "start")
      isRunning = true;
    if (action == "stop")
      isRunning = false;

    // Durumu geri bildir
    broadcastStatus(); // Tüm ağa durum bildirimi yap
    // Arayüzü güncelle
    String resp = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
    ws.textAll(resp);
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Gönderim durumu (Opsiyonel debug)
}

void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // Broadcast Peer Ekle (FF:FF:FF:FF:FF:FF)
  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void broadcastDiscovery() {
  strcpy(myData.type, "DISCOVER");
  strcpy(myData.sender_mac, myMacAddress.c_str());
  strcpy(myData.sender_name, config.hostname.c_str());
  strcpy(myData.payload, "");

  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
}

void broadcastStatus() {
  strcpy(myData.type, "STATUS");
  strcpy(myData.sender_mac, myMacAddress.c_str());
  strcpy(myData.sender_name, config.hostname.c_str());
  // Durum JSON
  String status = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
  status.toCharArray(myData.payload, 64);

  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
}

void sendToPeer(String targetMac, String command, String action) {
  // Mac String -> Hex Array
  uint8_t peerAddr[6];
  sscanf(targetMac.c_str(), "%x:%x:%x:%x:%x:%x", &peerAddr[0], &peerAddr[1],
         &peerAddr[2], &peerAddr[3], &peerAddr[4], &peerAddr[5]);

  strcpy(myData.type, "CMD");
  strcpy(myData.sender_mac, myMacAddress.c_str());
  strcpy(myData.sender_name, config.hostname.c_str());

  String payload = "{\"action\":\"" + action + "\"}";
  payload.toCharArray(myData.payload, 64);

  esp_now_send(peerAddr, (uint8_t *)&myData, sizeof(myData));
}

// ===============================
// Yardımcı Fonksiyonlar ve Web
// ===============================
// .. (initMotor, startMotorTurn, updateSchedule, checkSchedule aynı kalacak) ..
// YER TASARRUFU İÇİN KISALTILDI, ÖNCEKİ KODUN AYNISI KORUNACAK
// ANCAK processCommand genişletilecek

void processCommand(String jsonStr) {
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonStr);

  if (error)
    return;

  String type = doc["type"];

  if (type == "command") {
    String action = doc["action"];
    if (action == "start")
      isRunning = true;
    if (action == "stop")
      isRunning = false;
    updateSchedule();
    String resp = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
    ws.textAll(resp);

    // Değişikliği ESP-NOW ile duyur (Opsiyonel, senkronize grup için)
    // broadcastStatus();

  } else if (type == "settings") {
    if (doc.containsKey("tpd"))
      config.tpd = doc["tpd"];
    if (doc.containsKey("dur"))
      config.duration = doc["dur"];
    if (doc.containsKey("dir"))
      config.direction = doc["dir"];
    saveConfig();
    updateSchedule();
    String resp = "{\"tpd\":" + String(config.tpd) +
                  ",\"dur\":" + String(config.duration) +
                  ",\"dir\":" + String(config.direction) + "}";
    ws.textAll(resp);

  } else if (type == "check_peers") {
    // Keşif yayını yap
    broadcastDiscovery();
    // Mevcut listeyi hemen gönder
    String json = "{ \"peers\": [";
    for (int i = 0; i < peers.size(); i++) {
      if (i > 0)
        json += ",";
      json += "{\"mac\":\"" + peers[i].mac + "\",\"name\":\"" + peers[i].name +
              "\"}";
    }
    json += "] }";
    ws.textAll(json);

  } else if (type == "peer_command") {
    String target = doc["target"];
    String action = doc["action"];
    sendToPeer(target, "command", action);
  }
}

// ... Diğer fonksiyonlar (setup, initWiFi vb) önceki blokta güncellendiği
// haliyle kalacak ... Tekrar tam kod yazmak yerine sadece değişen mantığı
// ekledim. Ancak "initWiFi" içinde Mac Adresi alma kısmını global değişkene
// yazalım.

void initWiFi() {
  WiFi.mode(WIFI_AP_STA);
  // ... (Efuse okuma kodu aynı) ...
  // Sadece bu satırı ekle:
  myMacAddress = WiFi.macAddress();
  // ...

  // Önceki kodun aynısı...
  uint64_t chipid = ESP.getEfuseMac();
  char macBuf[18];
  sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)(chipid >> 0),
          (uint8_t)(chipid >> 8), (uint8_t)(chipid >> 16),
          (uint8_t)(chipid >> 24), (uint8_t)(chipid >> 32),
          (uint8_t)(chipid >> 40));
  myMacAddress = String(macBuf);    // Düzeltme: ChipID little endian olabilir,
                                    // WiFi.macAddress() en temizi.
  myMacAddress = WiFi.macAddress(); // Standart kütüphaneyi kullanalım

  String apName = "horus-" + myMacAddress.substring(12, 14) +
                  myMacAddress.substring(15, 17);
  WiFi.softAP(apName.c_str());

  if (config.hostname == "")
    config.hostname = apName;
  WiFi.setHostname(config.hostname.c_str());
  WiFi.begin();
}

// ... updateSchedule, checkSchedule, initMotor, handlePhysicalControl
// ...Aynı... Derleyici için eksik olmasın diye kopyalıyorum (Tam dosya yazma
// modu)
void initMotor() {
  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);
}
void startMotorTurn() {
  float speed = (float)STEPS_PER_REVOLUTION / (float)config.duration;
  stepper.setMaxSpeed(speed);
  long targetPos = STEPS_PER_REVOLUTION;
  if (config.direction == 1)
    targetPos = -STEPS_PER_REVOLUTION;
  else if (config.direction == 2 && turnsThisHour % 2 != 0)
    targetPos = -STEPS_PER_REVOLUTION;
  stepper.move(targetPos);
  stepper.enableOutputs();
  isMotorMoving = true;
}
void updateSchedule() {
  targetTurnsPerHour = config.tpd / 24;
  if (targetTurnsPerHour < 1)
    targetTurnsPerHour = 1;
}
void checkSchedule() {
  unsigned long now = millis();
  if (now - lastHourCheck > hourDuration) {
    lastHourCheck = now;
    turnsThisHour = 0;
  }
  if (turnsThisHour < targetTurnsPerHour && !isMotorMoving) {
    startMotorTurn();
  }
}
void handlePhysicalControl() {
  int reading = digitalRead(TOUCH_PIN);
  if (reading == HIGH && (millis() - lastTouchTime > 500)) {
    lastTouchTime = millis();
    isRunning = !isRunning;
    String json = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
    ws.textAll(json);
    broadcastStatus(); // Durum değişti, yayınla
  }
}
void loadConfig() {
  if (LittleFS.exists(JSON_CONFIG_FILE)) {
    File file = LittleFS.open(JSON_CONFIG_FILE, "r");
    StaticJsonDocument<512> doc;
    deserializeJson(doc, file);
    config.tpd = doc["tpd"] | 600;
    config.duration = doc["dur"] | 15;
    config.direction = doc["dir"] | 0;
    config.hostname = doc["name"] | "";
    file.close();
  }
}
void saveConfig() {
  File file = LittleFS.open(JSON_CONFIG_FILE, "w");
  StaticJsonDocument<512> doc;
  doc["tpd"] = config.tpd;
  doc["dur"] = config.duration;
  doc["dir"] = config.direction;
  doc["name"] = config.hostname;
  serializeJson(doc, file);
  file.close();
}
void handleWifiScan() {
  if (wifiScanning) {
    int n = WiFi.scanComplete();
    if (n >= 0 || n == -2)
      wifiScanning = false;
  }
}
void handleWifiConnection() {
  // Basit bağlantı durumu takibi
  if (WiFi.status() == WL_CONNECTED && wifiStatusStr != "connected")
    wifiStatusStr = "connected";
  else if (WiFi.status() != WL_CONNECTED && !tryConnect)
    wifiStatusStr = "disconnected";
}
String getWifiListJson() {
  int n = WiFi.scanComplete();
  if (n == -2)
    return "[]";
  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i > 0)
      json += ",";
    json +=
        "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
        ",\"secure\":" +
        String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") +
        "}";
  }
  json += "]";
  WiFi.scanDelete();
  return json;
}
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
