/**
 * PROJE ADI: HORUS BY WYNTRO
 * ROL: Firmware
 * DONANIM: ESP32 WROOM, 28BYJ-48 (ULN2003), TTP223
 * TASARIM: Caner Kocacık
 * DÜZELTME: OTA güncelleme iyileştirmesi ve temizlik
 */

// 1. KUTUPHANE DEGISTIRILDI (SPIFFS -> LittleFS)
#include <AccelStepper.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <vector>

// ===== FORWARD DECLARATIONS =====
bool connectToSavedWiFi();

String wifiScanResult = "[]";

void startSetupMode();
void initWebServer();
void initWiFi();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);
bool turnInProgress = false;

Preferences prefs;

bool setupMode = false;
bool skipSetup = false;
bool captiveMode = true;
const char *SETUP_AP_SSID = "Horus-Setup";
// const char* SETUP_AP_PASS = "ByWyntro3545"; // Setup modu şifresiz daha
// stabil çalışır

// ===============================
// Motor Pin Tanımlamaları
// ===============================
#define MOTOR_PIN_1 19
#define MOTOR_PIN_2 18
#define MOTOR_PIN_3 5
#define MOTOR_PIN_4 17

// ===============================
// Sensör ve Buton Tanımlamaları
// ===============================
#define TOUCH_PIN 23

// ===============================
// Sabitler ve Ayarlar
// ===============================
#define STEPS_PER_REVOLUTION 4096
#define JSON_CONFIG_FILE "/config.json"
#define GITHUB_VERSION_URL                                                     \
  "https://raw.githubusercontent.com/recaner35/HorusByWyntro/main/"            \
  "version.json"

#define FIRMWARE_VERSION "1.0.377"
#define PEER_FILE "/peers.json"

// ===============================
// Nesneler
// ===============================
AccelStepper stepper(AccelStepper::HALF4WIRE, MOTOR_PIN_1, MOTOR_PIN_3,
                     MOTOR_PIN_2, MOTOR_PIN_4);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===============================
// Global Değişkenler (Durum)
// ===============================
struct Config {
  int tpd = 900;
  int duration = 10;
  int direction = 2;
  String hostname = "";
  bool espNowEnabled = false;
};
Config config;

DNSServer dnsServer;
bool isRunning = false;
bool isMotorMoving = false;
bool isEspNowActive = false;
unsigned long sessionStartTime = 0;
int turnsThisHour = 0;
int targetTurnsPerHour = 0;
unsigned long lastTouchTime = 0;
bool touchState = false;
String deviceSuffix = "";

unsigned long lastHourCheck = 0;
unsigned long hourDuration = 3600000;
unsigned long restartTimer = 0; // Güvenli restart için zamanlayıcı
bool shouldRestartFlag = false; // Restart bayrağı

struct PeerDevice {
  String mac;
  String name;
  unsigned long lastSeen;
  int tpd = 900;
  int duration = 10;
  int direction = 2;
  bool isRunning = false;
};
std::vector<PeerDevice> peers;
#define PEER_TIMEOUT 60000

typedef struct struct_message {
  char type[10];
  char sender_mac[20];
  char sender_name[32];
  char payload[64];
} struct_message;

struct_message myData;
struct_message incomingData;

String wifiStatusStr = "disconnected";
bool tryConnect = false;
unsigned long connectStartTime = 0;
String myMacAddress;
bool shouldUpdate = false;
String otaStatus = "idle";
unsigned long scanStartTime = 0;

// ===============================
// FONKSİYON PROTOTİPLERİ
// ===============================
void loadConfig();
void saveConfig();
void initMotor();
void processCommand(String jsonStr);
void updateSchedule();
void handlePhysicalControl();
void checkSchedule();
void startMotorTurn();
void handleWifiConnection();
String getWifiListJson();
void initESPNow();
void restorePeers();
void broadcastDiscovery();
void broadcastStatus();
void sendToPeer(String targetMac, String payloadJson);
void loadPeers();
void savePeers();
bool execOTA(String url, int command);
void checkAndPerformUpdate();
String slugify(String text);

// ===============================
// Slugify Helper
// ===============================
String slugify(String text) {
  String out = "";
  text.toLowerCase();

  for (int i = 0; i < text.length(); i++) {
    char c = text[i];
    switch ((unsigned char)c) {
    case 0xC3:
      continue;
    default:
      if (c >= 'a' && c <= 'z')
        out += c;
      else if (c >= '0' && c <= '9')
        out += c;
      else if (c == ' ' || c == '-')
        out += '-';
      break;
    }
  }

  // Türkçe karakter dönüşümleri
  text.replace("ş", "s");
  text.replace("Ş", "s");
  text.replace("ı", "i");
  text.replace("İ", "i");
  text.replace("ğ", "g");
  text.replace("Ğ", "g");
  text.replace("ü", "u");
  text.replace("Ü", "u");
  text.replace("ö", "o");
  text.replace("Ö", "o");
  text.replace("ç", "c");
  text.replace("Ç", "c");

  out = "";
  for (int i = 0; i < text.length(); i++) {
    char c = text[i];
    if (isalnum(c)) {
      out += (char)tolower(c);
    } else if (c == ' ' || c == '-' || c == '_') {
      out += '-';
    }
  }

  while (out.indexOf("--") >= 0) {
    out.replace("--", "-");
  }

  if (out.startsWith("-"))
    out = out.substring(1);
  if (out.endsWith("-"))
    out = out.substring(0, out.length() - 1);

  if (out.length() == 0)
    return "horus";
  return out;
}

// ===============================
// OTA Helper Functions
// ===============================
bool execOTA(String url, int command) {
  // OTA öncesi her şeyi sıfırla
  Update.abort();

  // 🔥 RADIKAL RAM KAZANIMI: WiFi dışındaki tüm servisleri kapat
  if (isEspNowActive) {
    esp_now_deinit();
    isEspNowActive = false;
    Serial.println(F("[OTA] ESP-NOW durduruldu."));
  }

  ws.closeAll();
  ws.enable(false);
  server.end(); // Web sunucusunu tamamen kapat (RAM boşaltır)
  MDNS.end();   // mDNS servislerini kapat

  // Belleğin toparlanması için sistem görevlerine zaman tanı
  delay(500);
  Serial.printf("[OTA] Temizlik sonrasi Heap: %d bytes\n", ESP.getFreeHeap());

  NetworkClientSecure client;
  client.setInsecure();

  // Core 3.x için SSL tampon belleğini minimumda tutmaya çalış
  client.setHandshakeTimeout(30);

  HTTPClient http;
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(60000);

  Serial.println(F("[OTA] HTTP GET..."));
  int httpCode = http.GET();

  if (httpCode == 200) {
    int contentLength = http.getSize();
    Serial.printf("[OTA] Dosya: %d, Heap: %d\n", contentLength,
                  ESP.getFreeHeap());

    if (contentLength <= 0) {
      Serial.println(F("[OTA] Hata: Boyut 0!"));
      http.end();
      return false;
    }

    // OTA başlatma
    if (Update.begin(contentLength, command)) {
      Serial.println(F("[OTA] Yazma basliyor..."));
      size_t written = Update.writeStream(http.getStream());

      if (written == contentLength) {
        if (Update.end()) {
          if (Update.isFinished()) {
            Serial.println(F("[OTA] Basarili!"));
            http.end();
            return true;
          }
        }
      } else {
        Serial.printf("[OTA] Yazma eksik: %d/%d\n", written, contentLength);
      }
    } else {
      Serial.printf("[OTA] Hata Kodu: %d\n", Update.getError());
      if (Update.getError() == 0) {
        Serial.println(
            F("[OTA] Hala bellek yetersiz! SSL cok fazla RAM kullaniyor."));
      }
    }
  } else {
    Serial.printf("[OTA] HTTP Kod: %d\n", httpCode);
  }

  Update.abort();
  http.end();
  return false;
}

void checkAndPerformUpdate() {
  Serial.println("Guncelleme kontrol ediliyor...");
  otaStatus = "started";

  NetworkClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, GITHUB_VERSION_URL);

  int httpCode = http.GET();
  bool updated = false;

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, payload);

    String newVersion = doc["version"];
    String currentVersion = FIRMWARE_VERSION;

    Serial.println("Mevcut: " + currentVersion + ", Yeni: " + newVersion);

    if (newVersion != currentVersion) {
      Serial.println("Yeni surum bulundu! Guncelleniyor...");
      otaStatus = "updating";

      String baseUrl =
          "https://github.com/recaner35/HorusByWyntro/releases/download/" +
          newVersion + "/";
      String fsUrl = baseUrl + "HorusByWyntro.littlefs.bin"; // İsim düzeltildi
      String fwUrl = baseUrl + "HorusByWyntro.ino.bin";

      Serial.println("LittleFS indiriliyor: " + fsUrl);
      if (execOTA(fsUrl,
                  U_SPIFFS)) { // LittleFS olsa bile komut U_SPIFFS kalabilir
        Serial.println("LittleFS guncellendi.");
      } else {
        Serial.println("LittleFS guncelleme hatasi!");
      }

      Serial.println("Firmware indiriliyor: " + fwUrl);
      if (execOTA(fwUrl, U_FLASH)) {
        Serial.println("Firmware guncellendi. Yeniden baslatiliyor...");
        ESP.restart();
        updated = true;
      } else {
        Serial.println("Firmware guncelleme hatasi!");
        otaStatus = "error";
      }

    } else {
      Serial.println("Zaten guncel.");
      otaStatus = "up_to_date";
    }
  } else {
    Serial.println("Version dosyasi alinamadi");
    otaStatus = "error";
  }
  http.end();
}

// ===============================
// Kurulum (Setup)
// ===============================
void setup() {
  Serial.begin(115200);

  // 2. ISTEK: Seri Monitörde Versiyon Gösterimi
  Serial.println("\n\n==================================");
  Serial.println("HORUS FIRMWARE STARTING");
  Serial.print("VERSION: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("==================================\n");

  // 3. DUZELTME: LittleFS Başlatılıyor (SPIFFS yerine)
  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS Mount Failed"));
    return;
  }
  Serial.println(F("LittleFS OK"));

  // Config ve Suffix Yükle
  loadConfig();
  updateSchedule();
  uint64_t chipid = ESP.getEfuseMac();
  char suffixBuf[5];
  sprintf(suffixBuf, "%04X", (uint16_t)(chipid & 0xFFFF));
  deviceSuffix = String(suffixBuf);

  Serial.println("Device Suffix: " + deviceSuffix);

  // Pinleri Ayarla
  pinMode(TOUCH_PIN, INPUT);

  // Motor Kurulumu
  initMotor();
  stepper.setMaxSpeed(600);
  stepper.setAcceleration(200);

  // Wi-Fi Bağlantısını Dene veya Setup Moduna Geç
  // Önce 'skip' tercihini kontrol et
  Preferences p;
  p.begin("setup", true); // Read-only mode
  bool skip = p.getBool("skip", false);
  p.end();

  // Wi-Fi Bağlantısını Dene veya Setup Moduna Geç
  // Eğer skip=true ise bağlantı başarısız olsa bile setup moduna girme
  if (!skip && !connectToSavedWiFi()) {
    setupMode = true;   // SSID kararı için önce mode set edilmeli
    captiveMode = true; // Setup modunda captive portal aktif
    initWiFi();         // AP ve mDNS burada başlatılacak
    startSetupMode();
  } else {
    setupMode = false;
    captiveMode = false; // Bağlantı başarılıysa captive portalı kapat
    initWiFi();
    initESPNow();
  }

  // Web Sunucuyu Başlat
  initWebServer();

  server.begin();
}

// ===============================
// Ana Döngü (Loop)
// ===============================
void loop() {
  if (setupMode) {
    dnsServer.processNextRequest();
  }

  // OTA Update Kontrolü
  if (shouldUpdate) {
    checkAndPerformUpdate();
    shouldUpdate = false;
  }

  ws.cleanupClients();
  if (turnInProgress && stepper.distanceToGo() == 0) {
    stepper.disableOutputs();
    isMotorMoving = false;
    turnInProgress = false;
    turnsThisHour++;
  }
  checkSchedule();
  handlePhysicalControl();
  stepper.run();

  // Güvenli Restart Kontrolü
  if (shouldRestartFlag && millis() > restartTimer) {
    saveConfig();
    delay(100);
    ESP.restart();
  }

  // ÖNEMLİ: Watchdog beslemesi için minik bir gecikme
  delay(1);
}

// ===============================
// Yardımcı Fonksiyonlar
// ===============================

void initMotor() {
  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);
}

void startMotorTurn() {
  if (turnInProgress)
    return;

  float speed = (float)STEPS_PER_REVOLUTION / config.duration;
  stepper.setMaxSpeed(speed);

  long targetPos = STEPS_PER_REVOLUTION;
  if (config.direction == 1)
    targetPos = -STEPS_PER_REVOLUTION;
  else if (config.direction == 2 && turnsThisHour % 2 != 0)
    targetPos = -STEPS_PER_REVOLUTION;

  stepper.moveTo(stepper.currentPosition() + targetPos);
  stepper.enableOutputs();

  isMotorMoving = true;
  turnInProgress = true;
}

void updateSchedule() {
  targetTurnsPerHour = config.tpd / 24;
  if (targetTurnsPerHour < 1)
    targetTurnsPerHour = 1;
}

void checkSchedule() {
  if (!isRunning)
    return;

  unsigned long now = millis();

  // Saat reseti
  if (now - lastHourCheck >= hourDuration) {
    lastHourCheck = now;
    turnsThisHour = 0;
  }

  // Saatlik hedef dolmadıysa ve motor boşsa → bir tur başlat
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
    broadcastStatus();
  }
}

void loadConfig() {
  // 4. DUZELTME: LittleFS kullanılıyor
  if (LittleFS.exists(JSON_CONFIG_FILE)) {
    File file = LittleFS.open(JSON_CONFIG_FILE, "r");
    StaticJsonDocument<512> doc;
    deserializeJson(doc, file);
    config.tpd = doc["tpd"] | 900;
    config.duration = doc["dur"] | 10;
    config.direction = doc["dir"] | 2;
    config.hostname = doc["name"] | "";
    config.espNowEnabled = doc["espnow"] | false;
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
  doc["espnow"] = config.espNowEnabled;
  serializeJson(doc, file);
  file.close();
}

void handleWifiConnection() {
  if (WiFi.status() == WL_CONNECTED && wifiStatusStr != "connected")
    wifiStatusStr = "connected";
  else if (WiFi.status() != WL_CONNECTED && !tryConnect)
    wifiStatusStr = "disconnected";
}

// ===============================
// ESP-NOW Logic
// ===============================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingDataPtr,
                int len) {
  const uint8_t *mac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingDataPtr, int len) {
#endif
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));

  uint8_t myRawMac[6];
  WiFi.macAddress(myRawMac);
  if (memcmp(mac, myRawMac, 6) == 0)
    return;

  String type = String(incomingData.type);
  String senderMac = String(incomingData.sender_mac);
  String senderName = String(incomingData.sender_name);

  Serial.println("ESP-NOW Paket Alındı: " + type);

  bool found = false;
  bool needsUpdate = false;

  StaticJsonDocument<128> doc_payload;
  deserializeJson(doc_payload, incomingData.payload);

  for (auto &p : peers) {
    if (p.mac == senderMac) {
      p.lastSeen = millis();
      if (p.name != senderName) {
        p.name = senderName;
        needsUpdate = true;
        savePeers();
      }

      if (doc_payload.containsKey("r"))
        p.isRunning = doc_payload["r"];
      if (doc_payload.containsKey("t"))
        p.tpd = doc_payload["t"];
      if (doc_payload.containsKey("d"))
        p.duration = doc_payload["d"];
      if (doc_payload.containsKey("dr"))
        p.direction = doc_payload["dr"];

      found = true;
      break;
    }
  }

  if (!found) {
    PeerDevice newPeer;
    newPeer.mac = senderMac;
    newPeer.name = senderName;
    newPeer.lastSeen = millis();

    if (doc_payload.containsKey("r"))
      newPeer.isRunning = doc_payload["r"];
    if (doc_payload.containsKey("t"))
      newPeer.tpd = doc_payload["t"];
    if (doc_payload.containsKey("d"))
      newPeer.duration = doc_payload["d"];
    if (doc_payload.containsKey("dr"))
      newPeer.direction = doc_payload["dr"];

    peers.push_back(newPeer);
    needsUpdate = true;
    savePeers();

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = WiFi.channel();
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_add_peer(&peerInfo);
    }
  }

  if (type == "CMD") {
    if (doc_payload.containsKey("set")) {
      if (doc_payload.containsKey("t"))
        config.tpd = doc_payload["t"];
      if (doc_payload.containsKey("d"))
        config.duration = doc_payload["d"];
      if (doc_payload.containsKey("dr"))
        config.direction = doc_payload["dr"];
      if (doc_payload.containsKey("r"))
        isRunning = doc_payload["r"];

      saveConfig();
      updateSchedule();
      broadcastStatus();

      String json = "{\"running\":" + String(isRunning ? "true" : "false") +
                    ",\"tpd\":" + String(config.tpd) +
                    ",\"dur\":" + String(config.duration) +
                    ",\"dir\":" + String(config.direction) + "}";
      ws.textAll(json);
    } else {
      String action = doc_payload["action"];
      if (action == "start")
        isRunning = true;
      if (action == "stop")
        isRunning = false;

      broadcastStatus();
      String resp =
          "{\"running\":" + String(isRunning ? "true" : "false") + "}";
      ws.textAll(resp);
    }
  }

  if (needsUpdate) {
    String json = "{ \"peers\": [";
    for (int i = 0; i < peers.size(); i++) {
      if (i > 0)
        json += ",";
      json += "{\"mac\":\"" + peers[i].mac + "\",\"name\":\"" + peers[i].name +
              "\",\"tpd\":" + String(peers[i].tpd) +
              ",\"dur\":" + String(peers[i].duration) +
              ",\"dir\":" + String(peers[i].direction) +
              ",\"running\":" + (peers[i].isRunning ? "true" : "false") +
              ",\"online\":" +
              ((millis() - peers[i].lastSeen < 65000) ? "true" : "false") + "}";
    }
    json += "] }";
    ws.textAll(json);
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Debug info
}

void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    isEspNowActive = false;
    return;
  }

  isEspNowActive = true;
  Serial.println("ESP-NOW Başlatıldı");

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = WiFi.channel();
  peerInfo.encrypt = false;

  esp_now_add_peer(&peerInfo);
}

void restorePeers() {
  for (auto &p : peers) {
    uint8_t mac[6];
    sscanf(p.mac.c_str(), "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2],
           &mac[3], &mac[4], &mac[5]);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = WiFi.channel();
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_add_peer(&peerInfo);
    }
  }
  Serial.println("Peerlar ESP-NOW'a geri yüklendi.");
}

void loadPeers() {
  if (LittleFS.exists(PEER_FILE)) {
    File file = LittleFS.open(PEER_FILE, "r");
    if (file) {
      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        peers.clear();
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
          PeerDevice p;
          p.mac = obj["mac"].as<String>();
          p.name = obj["name"].as<String>();
          p.tpd = obj["tpd"];
          p.duration = obj["dur"];
          p.direction = obj["dir"];
          p.isRunning = false;
          p.lastSeen = 0;
          peers.push_back(p);
        }
      }
      file.close();
    }
  }
}

void savePeers() {
  File file = LittleFS.open(PEER_FILE, "w");
  if (file) {
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (auto &p : peers) {
      JsonObject obj = arr.createNestedObject();
      obj["mac"] = p.mac;
      obj["name"] = p.name;
      obj["tpd"] = p.tpd;
      obj["dur"] = p.duration;
      obj["dir"] = p.direction;
    }
    serializeJson(doc, file);
    file.close();
  }
}

String getShortStatusJson() {
  StaticJsonDocument<128> doc;
  doc["r"] = isRunning;
  doc["t"] = config.tpd;
  doc["d"] = config.duration;
  doc["dr"] = config.direction;
  String out;
  serializeJson(doc, out);
  return out;
}

void broadcastDiscovery() {
  if (!isEspNowActive)
    return;

  strcpy(myData.type, "DISCOVER");
  strcpy(myData.sender_mac, myMacAddress.c_str());
  String displayName =
      (config.hostname != "") ? config.hostname : "Horus-Device";
  strcpy(myData.sender_name, displayName.c_str());

  String payload = getShortStatusJson();
  payload.toCharArray(myData.payload, 64);

  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
}

void broadcastStatus() {
  if (!isEspNowActive)
    return;

  strcpy(myData.type, "STATUS");
  strcpy(myData.sender_mac, myMacAddress.c_str());
  String displayName =
      (config.hostname != "") ? config.hostname : "Horus-Device";
  strcpy(myData.sender_name, displayName.c_str());

  String payload = getShortStatusJson();
  payload.toCharArray(myData.payload, 64);

  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
}

void sendToPeer(String targetMac, String payloadJson) {
  if (!isEspNowActive)
    return;

  uint8_t peerAddr[6];
  sscanf(targetMac.c_str(), "%x:%x:%x:%x:%x:%x", &peerAddr[0], &peerAddr[1],
         &peerAddr[2], &peerAddr[3], &peerAddr[4], &peerAddr[5]);

  strcpy(myData.type, "CMD");
  strcpy(myData.sender_mac, myMacAddress.c_str());
  strcpy(myData.sender_name, config.hostname.c_str());
  payloadJson.toCharArray(myData.payload, 64);

  esp_now_send(peerAddr, (uint8_t *)&myData, sizeof(myData));
}

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
    broadcastStatus();
    String resp = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
    ws.textAll(resp);
  } else if (type == "settings") {
    if (doc.containsKey("tpd"))
      config.tpd = doc["tpd"];
    if (doc.containsKey("dur"))
      config.duration = doc["dur"];
    if (doc.containsKey("dir"))
      config.direction = doc["dir"];
    if (doc.containsKey("espnow")) {
      bool targetState = doc["espnow"];
      if (targetState && WiFi.status() != WL_CONNECTED) {
        config.espNowEnabled = false;
        String resp = "{\"espnow\":false}";
        ws.textAll(resp);

        String errorMsg =
            "{\"type\":\"error\",\"message\":\"WiFi baglantisi gerekli!\"}";
        ws.textAll(errorMsg);
      } else {
        config.espNowEnabled = targetState;
        if (config.espNowEnabled) {
          if (!isEspNowActive) {
            initESPNow();
            restorePeers();
          }
        } else {
          if (isEspNowActive) {
            esp_now_deinit();
            isEspNowActive = false;
          }
        }
      }
    }
    if (doc.containsKey("name")) {
      String newName = doc["name"].as<String>();
      if (newName != config.hostname) {
        config.hostname = newName;
        saveConfig();
        String resp = "{\"tpd\":" + String(config.tpd) +
                      ",\"dur\":" + String(config.duration) +
                      ",\"dir\":" + String(config.direction) + ",\"espnow\":" +
                      String(config.espNowEnabled ? "true" : "false") +
                      ",\"name\":\"" + config.hostname + "\"}";
        ws.textAll(resp);
        delay(500);
        ESP.restart();
        return;
      }
    }

    saveConfig();
    updateSchedule();

    String resp = "{\"tpd\":" + String(config.tpd) +
                  ",\"dur\":" + String(config.duration) +
                  ",\"dir\":" + String(config.direction) + ",\"espnow\":" +
                  String(config.espNowEnabled ? "true" : "false") +
                  ",\"name\":\"" + config.hostname + "\"}";
    ws.textAll(resp);
  } else if (type == "check_peers") {
    if (!isEspNowActive) {
      return;
    }
    broadcastDiscovery();

    String json = "{ \"peers\": [";
    for (int i = 0; i < peers.size(); i++) {
      if (i > 0)
        json += ",";
      json += "{\"mac\":\"" + peers[i].mac + "\",\"name\":\"" + peers[i].name +
              "\",\"tpd\":" + String(peers[i].tpd) +
              ",\"dur\":" + String(peers[i].duration) +
              ",\"dir\":" + String(peers[i].direction) +
              ",\"running\":" + (peers[i].isRunning ? "true" : "false") +
              ",\"online\":" +
              ((millis() - peers[i].lastSeen < 65000) ? "true" : "false") + "}";
    }
    json += "] }";
    ws.textAll(json);
  } else if (type == "peer_settings") {
    String target = doc["target"];
    StaticJsonDocument<128> pDoc;
    pDoc["set"] = true;
    pDoc["t"] = doc["tpd"];
    pDoc["d"] = doc["dur"];
    pDoc["dr"] = doc["dir"];
    pDoc["r"] = doc["running"];

    String payload;
    serializeJson(pDoc, payload);
    sendToPeer(target, payload);
  } else if (type == "del_peer") {
    String target = doc["target"];
    for (int i = 0; i < peers.size(); i++) {
      if (peers[i].mac == target) {
        uint8_t mac[6];
        sscanf(target.c_str(), "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2],
               &mac[3], &mac[4], &mac[5]);
        esp_now_del_peer(mac);

        peers.erase(peers.begin() + i);
        savePeers();

        String json = "{ \"peers\": [";
        for (int k = 0; k < peers.size(); k++) {
          if (k > 0)
            json += ",";
          json += "{\"mac\":\"" + peers[k].mac + "\",\"name\":\"" +
                  peers[k].name + "\"}";
        }
        json += "] }";
        ws.textAll(json);
        break;
      }
    }
  }
}

// ===============================
// PROPER WiFi Initialization
// ===============================
void initWiFi() {
  WiFi.mode(WIFI_AP_STA);
  uint64_t chipid = ESP.getEfuseMac();
  char macBuf[18];

  myMacAddress = WiFi.macAddress();
  if (myMacAddress == "00:00:00:00:00:00" || myMacAddress == "") {
    sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)(chipid >> 0),
            (uint8_t)(chipid >> 8), (uint8_t)(chipid >> 16),
            (uint8_t)(chipid >> 24), (uint8_t)(chipid >> 32),
            (uint8_t)(chipid >> 40));
    myMacAddress = String(macBuf);
  }

  String apName = SETUP_AP_SSID; // Global değişkenden al (Horus-Setup)

  // Kanal seçimi: Varsayılan 1
  int channel = 1;

  if (!setupMode) {
    String apBase = (config.hostname != "") ? config.hostname : "Horus";
    String apSlug = slugify(apBase);
    apName = apSlug + "-" + deviceSuffix;

    // EĞER Station modunda bağlıysak, aynı kanalı kullanmak ZORUNDAYIZ
    // Aksi takdirde ESP32 radio kanalı değiştireceği için bağlantı kopar!
    if (WiFi.status() == WL_CONNECTED) {
      channel = WiFi.channel();
      Serial.print(F("STA Kanalı algılandı: "));
      Serial.println(channel);
    }
  }

  // 172.217.28.1 sadece SETUP modunda (Android Captive Portal Hilesi)
  // Normal modda standart 192.168.4.1 kullan
  IPAddress apIP;
  if (setupMode) {
    apIP = IPAddress(172, 217, 28, 1);
  } else {
    apIP = IPAddress(192, 168, 4, 1);
  }

  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, subnet);

  // SoftAP Başlat
  WiFi.softAP(apName.c_str(), NULL, channel, 0, 8);
  WiFi.setHostname(apName.c_str());

  // KRİTİK: WiFi Güç Tasarrufunu kapat (Samsung bağlantı kopmalarını önler)
  esp_wifi_set_ps(WIFI_PS_NONE);

  // WiFi uykusunu kapat (Yanıt süresini milisaniyelere düşürür)
  WiFi.setSleep(false);

  // RFC 8910: DHCP Option 114 (Captive Portal API)
  // Sadece SETUP modunda aktifleştir
  if (setupMode) {
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
      esp_netif_dhcps_stop(ap_netif);
      const char *cp_url = "http://172.217.28.1/api/captive-portal";
      esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                             (esp_netif_dhcp_option_id_t)114, (void *)cp_url,
                             strlen(cp_url));
      esp_netif_dhcps_start(ap_netif);
    }
  }

  // mDNS Başlatma
  if (MDNS.begin(apName.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS Responder baslatildi: " + apName + ".local");
  } else {
    Serial.println("mDNS baslatilamadi!");
  }

  Serial.println("\n------------------------------------------------");
  Serial.println("HORUS: Erisim Adresleri");
  Serial.println("------------------------------------------------");
  Serial.println("1. Hotspot Baglantisi (AP):");
  Serial.print("   SSID: ");
  Serial.println(apName);
  Serial.print("   IP:   ");
  Serial.println(WiFi.softAPIP());
  Serial.print("   URL:  http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/");

  if (!setupMode && WiFi.status() == WL_CONNECTED) {
    Serial.println("\n2. Ev Agi Baglantisi (STA):");
    Serial.print("   IP:   ");
    Serial.println(WiFi.localIP());
    Serial.print("   mDNS: http://");
    Serial.print(apName);
    Serial.println(".local/");
    if (config.hostname != "") {
      Serial.print("   mDNS (Slug): http://");
      Serial.print(slugify(config.hostname));
      Serial.println(".local/ (Alternatif)");
    }
  }
  Serial.println("------------------------------------------------\n");
}

// ===============================
// Web Server Initialization
// ===============================
void initWebServer() {

  // Helping Lambda: Check if the request is for US (IP or mDNS)
  // [=] capture: config, deviceSuffix ve diğer global değişkenlere erişim
  // sağlar.
  auto isOurLocalRequest = [=](String host) {
    if (host == "172.217.28.1" || host == "192.168.4.1" ||
        host == "horus.local")
      return true;

    // IP Kontrolü: Google IP'si veya o anki Yerel IP veya AP IP
    if (host.indexOf("172.217.28.1") >= 0)
      return true;
    if (WiFi.localIP().toString() != "0.0.0.0" &&
        host.indexOf(WiFi.localIP().toString()) >= 0)
      return true;
    if (host.indexOf(WiFi.softAPIP().toString()) >= 0)
      return true;

    // İsim Kontrolü: "horus-" veya kullanıcının verdiği özel isim
    if (host.indexOf("horus") >= 0)
      return true;

    // Dinamik İsim Kontrolü (örn: livingroom-A1B2)
    String currentSlug =
        slugify((config.hostname != "") ? config.hostname : "horus");
    if (host.indexOf(currentSlug) >= 0)
      return true;

    return false;
  };

  auto sendPortalRedirect = [](AsyncWebServerRequest *request) {
    String currentApIP = WiFi.softAPIP().toString();
    String redirectUrl = "http://" + currentApIP + "/captive.html";

    // STEALTH + LUXURY REDIRECT: Redirect to a dedicated captive page first.
    // Dinamik IP ile yönlendirme (Setup modunda 172.x, diğerinde 192.x)
    String html = "<!DOCTYPE html><html><head>"
                  "<meta http-equiv='refresh' content='0;url=" +
                  redirectUrl +
                  "'>"
                  "<script>window.location.href='" +
                  redirectUrl +
                  "';</script>"
                  "</head><body>Redirecting to Horus...</body></html>";

    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", html);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "-1");
    response->addHeader("Connection", "close");
    request->send(response);
  };

  // Inline Captive Portal Page (Lüks & Minimalist)
  server.on("/captive.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    String currentApIP = WiFi.softAPIP().toString();
    String enterUrl = "http://" + currentApIP + "/";

    String html =
        "<!DOCTYPE html><html lang='tr'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Horus By Wyntro Kurulum Ekranı</title><style>"
        "body{background:#000;color:#fff;font-family:-apple-system,sans-serif;"
        "display:flex;flex-direction:column;align-items:center;justify-content:"
        "center;height:100vh;margin:0;text-align:center}"
        "h1{font-weight:200;letter-spacing:5px;margin-bottom:30px;color:#"
        "00f0ff;text-shadow:0 0 20px rgba(0,240,255,0.5)}"
        ".btn{color:#00f0ff;text-decoration:none;border:1px solid "
        "rgba(0,240,255,0.3);padding:18px "
        "40px;border-radius:50px;font-size:14px;letter-spacing:2px;background:"
        "rgba(0,240,255,0.05);transition:all 0.3s;box-shadow:0 0 30px "
        "rgba(0,240,255,0.1)}"
        ".btn:active{transform:scale(0.95);background:rgba(0,240,255,0.2)}"
        "</style></head><body>"
        "<h1>Horus By Wyntro</h1><p "
        "style='opacity:0.6;margin-bottom:40px'>Zamanı "
        "Korumaya Başlayın</p>"
        "<a href='" +
        enterUrl +
        "' class='btn'>KURULUMA BAŞLA</a>"
        "</body></html>";
    request->send(200, "text/html", html);
  });

  // RFC 8908 Uyumlu API endpoint
  server.on(
      "/api/captive-portal", HTTP_GET, [](AsyncWebServerRequest *request) {
        String currentApIP = WiFi.softAPIP().toString();
        String url = "http://" + currentApIP + "/captive.html";
        String json = "{\"captive\":true,\"user-portal-url\":\"" + url + "\"}";
        request->send(200, "application/json", json);
      });

  server.on(
      "/", HTTP_GET,
      [isOurLocalRequest, sendPortalRedirect](AsyncWebServerRequest *request) {
        if (setupMode || captiveMode) {
          if (!isOurLocalRequest(request->host())) {
            sendPortalRedirect(request);
          } else {
            request->send(LittleFS, "/index.html", "text/html");
          }
        } else {
          request->send(LittleFS, "/index.html", "text/html");
        }
      });

  server.serveStatic("/", LittleFS, "/");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  /* -------------------- WIFI SCAN (DÜZELTİLEN KISIM) -------------------- */

  server.on("/api/scan-networks", HTTP_GET, [](AsyncWebServerRequest *request) {
    int status = WiFi.scanComplete();

    // Scan hiç başlamamış → başlat
    if (status == WIFI_SCAN_FAILED || status == -2) {
      WiFi.scanDelete();
      WiFi.scanNetworks(true); // async
      request->send(200, "application/json", "[]");
      return;
    }

    // Scan devam ediyor
    if (status == WIFI_SCAN_RUNNING) {
      request->send(200, "application/json", "[]");
      return;
    }

    // Scan bitti
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < status; i++) {
      JsonObject n = arr.createNestedObject();
      n["ssid"] = WiFi.SSID(i);
      n["rssi"] = WiFi.RSSI(i);
      n["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    String out;
    serializeJson(arr, out);
    WiFi.scanDelete();

    request->send(200, "application/json", out);
  });

  /* -------------------- WIFI KAYIT -------------------- */

  server.on(
      "/api/save-wifi", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        StaticJsonDocument<200> doc;
        deserializeJson(doc, data);

        prefs.begin("wifi", false);
        prefs.putString("ssid", doc["ssid"].as<String>());
        prefs.putString("pass", doc["pass"].as<String>());
        prefs.end();

        // Yeni WiFi girildiğinde, SKIP bayrağını temizlemeliyiz ki tekrar
        // denesin
        Preferences p;
        p.begin("setup", false);
        p.remove("skip");
        p.end();

        request->send(200, "application/json", "{\"status\":\"saved\"}");
        restartTimer =
            millis() + 2000; // 2 saniye sonra restart (tarayıcıya zaman tanı)
        shouldRestartFlag = true;
      });

  /* -------------------- ULTIMATE CAPTIVE PORTAL (2026 Android/Fix)
   * -------------------- */
  // Google / Android Probes
  server.on("/generate_204", HTTP_ANY, sendPortalRedirect);
  server.on("/gen_204", HTTP_ANY, sendPortalRedirect);
  server.on("/blank.html", HTTP_ANY, sendPortalRedirect);
  server.on("/connectivity-check", HTTP_ANY, sendPortalRedirect);
  server.on("/connectivitycheck.android.com", HTTP_ANY, sendPortalRedirect);
  server.on("/connectivitycheck.gstatic.com", HTTP_ANY, sendPortalRedirect);

  // Apple Captive Portal Probes
  server.on("/hotspot-detect.html", HTTP_ANY, sendPortalRedirect);
  server.on("/library/test/success.html", HTTP_ANY, sendPortalRedirect);
  server.on("/success.txt", HTTP_ANY, sendPortalRedirect);

  // Windows / Microsoft Probes
  server.on("/connecttest.txt", HTTP_ANY, sendPortalRedirect);
  server.on("/ncsi.txt", HTTP_ANY, sendPortalRedirect);

  // Samsung Special Probe
  server.on("/nmcheck.gnm.samsung.com", HTTP_ANY, sendPortalRedirect);

  server.onNotFound(
      [isOurLocalRequest, sendPortalRedirect](AsyncWebServerRequest *request) {
        if (setupMode || captiveMode) {
          if (!isOurLocalRequest(request->host())) {
            sendPortalRedirect(request);
          } else {
            request->send(404, "text/plain", "Not Found");
          }
        } else {
          request->send(404, "text/plain", "Not Found");
        }
      });

  /* -------------------- SETUP / OTA / DEVICE API’LERİ -------------------- */

  // IP değişikliği kontrolü için Event Handler
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.println("\n[EVENT] Yeni IP Alindi!");
      Serial.print("[EVENT] IP: ");
      Serial.println(WiFi.localIP());

      // Eğer sistem zaten çalışıyorsa ve yeni IP aldıysa,
      // mDNS güncellemek için restart atmak en temizi
      if (!setupMode) {
        Serial.println(
            "[EVENT] mDNS senkronizasyonu icin restart planlaniyor...");
        shouldRestartFlag = true;
        restartTimer = millis() + 1000;
      }
    }
  });

  server.on("/api/device-state", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<200> doc;
    doc["setup"] = setupMode;
    doc["suffix"] = deviceSuffix;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/skip-setup", HTTP_POST, [](AsyncWebServerRequest *request) {
    skipSetup = true;
    setupMode = false;

    Preferences p;
    p.begin("setup", false);
    p.putBool("skip", true);
    p.end();

    request->send(200, "application/json", "{\"status\":\"skipped\"}");

    // Temiz geçiş için restart şart
    restartTimer = millis() + 1000;
    shouldRestartFlag = true;
  });

  server.on("/api/reset-setup", HTTP_POST, [](AsyncWebServerRequest *request) {
    Preferences p;
    p.begin("setup", false);
    p.remove("skip");
    p.end();
    request->send(200, "application/json", "{\"status\":\"reset\"}");
  });

  server.on("/api/ota-check", HTTP_GET, [](AsyncWebServerRequest *request) {
    NetworkClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, GITHUB_VERSION_URL);
    int httpCode = http.GET();

    StaticJsonDocument<256> resDoc;
    if (httpCode == 200) {
      String payload = http.getString();
      StaticJsonDocument<512> githubDoc;
      deserializeJson(githubDoc, payload);
      String newV = githubDoc["version"] | FIRMWARE_VERSION;
      resDoc["update_available"] = (newV != String(FIRMWARE_VERSION));
      resDoc["new_version"] = newV;
      resDoc["current_version"] = FIRMWARE_VERSION;
    } else {
      resDoc["error"] = "Check failed: " + String(httpCode);
    }
    http.end();

    String out;
    serializeJson(resDoc, out);
    request->send(200, "application/json", out);
  });

  server.on("/api/ota-auto", HTTP_POST, [](AsyncWebServerRequest *request) {
    shouldUpdate = true;
    otaStatus = "started";
    request->send(200, "application/json", "{\"status\":\"started\"}");
  });

  server.on("/api/ota-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  "{\"status\":\"" + otaStatus + "\"}");
  });

  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  String("{\"version\":\"") + FIRMWARE_VERSION + "\"}");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"rebooting\"}");
    delay(500);
    ESP.restart();
  });

  server.begin();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    String json =
        "{\"running\":" + String(isRunning ? "true" : "false") +
        ",\"tpd\":" + String(config.tpd) +
        ",\"dur\":" + String(config.duration) +
        ",\"dir\":" + String(config.direction) +
        ",\"espnow\":" + String(config.espNowEnabled ? "true" : "false") +
        ",\"name\":\"" + config.hostname + "\",\"suffix\":\"" + deviceSuffix +
        "\"}";
    client->text(json);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {
      String cmd = String((char *)data, len);
      processCommand(cmd);
    }
  }
}

void startSetupMode() {
  setupMode = true;

  // Cihazın modem olduğu kesinleşmeli (IP 172.217.28.1)
  IPAddress apIP(172, 217, 28, 1);

  // DNS Sunucusunu Başlat (Tüm sorguları Google IP'si gibi taklit eden adrese
  // çek)
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);

  Serial.println("Setup Mode: ON (Captive Portal 172.217.28.1 Active)");
}

bool connectToSavedWiFi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid == "") {
    Serial.println("Kayitli WiFi yok");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.print("WiFi baglaniyor: ");
  Serial.println(ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi baglandi!");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("\nWiFi baglanamadi");
  return false;
}
