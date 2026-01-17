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

#include <DNSServer.h>
#include <AccelStepper.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_now.h>
#include <vector>
#include <Preferences.h>

// ===== FORWARD DECLARATIONS =====
bool connectToSavedWiFi();
void startSetupMode();
void initWebServer();
void initWiFi();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

Preferences prefs;

bool setupMode = false;
bool skipSetup = false;

const char* SETUP_AP_SSID = "Horus";
const char* SETUP_AP_PASS = "ByWyntro3545"; 

String cachedWifiList = "[]";
unsigned long lastScanTime = 0;

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
#define LED_PIN 2    

// ===============================
// Sabitler ve Ayarlar
// ===============================
#define STEPS_PER_REVOLUTION 2048 
#define JSON_CONFIG_FILE "/config.json"
#define GITHUB_VERSION_URL "https://raw.githubusercontent.com/recaner35/HorusByWyntro/main/version.json"
#define FIRMWARE_VERSION "1.0.203"
#define PEER_FILE "/peers.json"

// ===============================
// Nesneler
// ===============================
AccelStepper stepper(AccelStepper::HALF4WIRE, MOTOR_PIN_1, MOTOR_PIN_3, MOTOR_PIN_2, MOTOR_PIN_4);

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

bool isScanning = false;
bool shouldStartScan = false; 
long lastWifiCheck = 0;
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
void handleWifiScan();
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
  NetworkClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode == 200) {
    int contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength, command);

    if (canBegin) {
      Serial.println("OTA Basliyor: " + url);
      size_t written = Update.writeStream(http.getStream());

      if (written == contentLength) {
        Serial.println("Yazma basarili: " + String(written) + "/" + String(contentLength));
      } else {
        Serial.println("Yazma hatasi: " + String(written) + "/" + String(contentLength));
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

      String baseUrl = "https://github.com/recaner35/HorusByWyntro/releases/download/" + newVersion + "/";
      String fsUrl = baseUrl + "HorusByWyntro.littlefs.bin";
      String fwUrl = baseUrl + "HorusByWyntro.ino.bin";

      Serial.println("LittleFS indiriliyor: " + fsUrl);
      if (execOTA(fsUrl, U_LITTLEFS)) {
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
  Serial.println("\nHORUS BY WYNTRO - Baslatiliyor");

  Preferences setupPrefs;
  setupPrefs.begin("setup", true);
  skipSetup = setupPrefs.getBool("skip", false);
  setupPrefs.end();

  bool connected = connectToSavedWiFi();

  if (!connected && !skipSetup) {
    setupMode = true;
    startSetupMode();
  } else {
    setupMode = false;
    initWiFi();
  }

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS baslatilamadi!");
  } else {
    Serial.println("LittleFS OK");
  }

  loadConfig();

  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  uint64_t chipid = ESP.getEfuseMac();
  uint32_t low = chipid & 0xFFFFFFFF;
  uint16_t idHigh = (low >> 0) & 0xFFFF;
  char idStr[5];
  sprintf(idStr, "%04X", idHigh);
  deviceSuffix = String(idStr);

  Serial.println("Device Suffix: " + deviceSuffix);

  initMotor();
  initWebServer();
  server.begin();

  Serial.println("Web Server BASLADI");
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

  // WiFi Tarama Tetikleyici
  if (shouldStartScan) {
    shouldStartScan = false;
    handleWifiScan();
  }

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

  static unsigned long lastEspNowCheck = 0;
  if (millis() - lastEspNowCheck > 5000) {
    lastEspNowCheck = millis();
    bool shouldBeActive = !isScanning && config.espNowEnabled && (WiFi.status() == WL_CONNECTED);

    if (shouldBeActive && !isEspNowActive) {
      Serial.println("WiFi bağlı ve ESP-NOW toggle açık, başlatılıyor...");
      initESPNow();
      restorePeers();
    } else if (!shouldBeActive && isEspNowActive) {
      Serial.print("ESP-NOW durduruluyor");
      esp_now_deinit();
      isEspNowActive = false;
    }
  }
  
  if (setupMode) {
    dnsServer.processNextRequest();
  }

  static unsigned long lastDiscovery = 0;

  if (!isScanning && config.espNowEnabled && (millis() - lastDiscovery > 10000)) {
    lastDiscovery = millis();
    broadcastDiscovery();
  }

  delay(2); 
}

// ===============================
// Yardımcı Fonksiyonlar
// ===============================

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
    broadcastStatus();
  }
}

void loadConfig() {
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

void handleWifiScan() {
  Serial.println("WiFi Taraması Başlatılıyor (Senkron)...");

  isScanning = true;
  cachedWifiList = "[]";

  if (isEspNowActive) {
    esp_now_deinit();
    isEspNowActive = false;
  }

  delay(200);

  WiFi.mode(WIFI_AP_STA);

  int n = WiFi.scanNetworks(false, true);
  Serial.printf("WiFi tarama tamamlandı: %d ağ bulundu\n", n);

  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]";

  cachedWifiList = json;
  lastScanTime = millis();

  WiFi.scanDelete();
  isScanning = false;

  if (setupMode) {
    WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS, 1);
    Serial.println("Setup AP yeniden aktif edildi.");
  } else {
    String apBase = "horus";
    if (config.hostname != "") apBase = slugify(config.hostname);
    String apName = apBase + "-" + deviceSuffix;
    WiFi.softAP(apName.c_str(), "", 1);
    Serial.println("Normal AP geri yüklendi.");
  }
}


void handleWifiConnection() {
  if (WiFi.status() == WL_CONNECTED && wifiStatusStr != "connected")
    wifiStatusStr = "connected";
  else if (WiFi.status() != WL_CONNECTED && !tryConnect)
    wifiStatusStr = "disconnected";
}

String getWifiListJson() {
  if (isScanning) {
    return "{\"status\":\"scanning\"}";
  }
  return cachedWifiList;
}


// ===============================
// ESP-NOW Logic
// ===============================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingDataPtr, int len) {
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
    peerInfo.channel = 0; 
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
      String resp = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
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
  peerInfo.channel = 0; 
  peerInfo.encrypt = false;

  esp_now_add_peer(&peerInfo);
}

void restorePeers() {
  for (auto &p : peers) {
    uint8_t mac[6];
    sscanf(p.mac.c_str(), "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0; 
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
  String displayName = (config.hostname != "") ? config.hostname : "Horus-Device";
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
  String displayName = (config.hostname != "") ? config.hostname : "Horus-Device";
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
  sscanf(targetMac.c_str(), "%x:%x:%x:%x:%x:%x", &peerAddr[0], &peerAddr[1], &peerAddr[2], &peerAddr[3], &peerAddr[4], &peerAddr[5]);

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

        String errorMsg = "{\"type\":\"error\",\"message\":\"WiFi baglantisi gerekli!\"}";
        ws.textAll(errorMsg);
      } else {
        config.espNowEnabled = targetState;
        if (config.espNowEnabled) {
          if (!isEspNowActive && !isScanning) {
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
    if (isScanning) {
      return;
    }
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
        sscanf(target.c_str(), "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
        esp_now_del_peer(mac);

        peers.erase(peers.begin() + i);
        savePeers();

        String json = "{ \"peers\": [";
        for (int k = 0; k < peers.size(); k++) {
          if (k > 0)
            json += ",";
          json += "{\"mac\":\"" + peers[k].mac + "\",\"name\":\"" + peers[k].name + "\"}";
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

  String apBase = "horus";
  if (config.hostname != "") {
    apBase = slugify(config.hostname);
  }
  String apName = apBase + "-" + deviceSuffix;

  WiFi.softAP(apName.c_str(), "");
  WiFi.setHostname(apName.c_str());

  Serial.println("WiFi AP Başlatıldı: " + apName);
  Serial.println("MAC Adresi: " + myMacAddress);
  Serial.println("AP IP Adresi: ");
  Serial.println(WiFi.softAPIP());
}

// ===============================
// Web Server Initialization
// ===============================
void initWebServer() {
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/scan-wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    int n = WiFi.scanNetworks();
    
    // Dinamik JSON dökümanı oluştur (Boyutu ağ sayısına göre ayarla)
    // Her ağ yaklaşık 100 byte yer tutsa, 50 ağ için 5120 yeterli olur.
    JsonDocument doc; 
    JsonArray array = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
      JsonObject net = array.add<JsonObject>();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
        
      // Şifreleme türü kontrolü (Basitleştirilmiş)
      wifi_auth_mode_t encryption = WiFi.encryptionType(i);
      net["secure"] = (encryption != WIFI_AUTH_OPEN);
      }

    String json;
    serializeJson(doc, json);
    
    request->send(200, "application/json", json);
  });
  // Android (çoğu sürüm)
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(204, "text/plain", ""); // Boş 204 → No captive
    // Alternatif: 200 + "No content" da olur ama 204 daha iyi
  });


  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(204, "text/plain", "");
  });


  // Eski Android / bazı Samsung
  server.on("/connectivitycheck.gstatic.com/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(204, "text/plain", "");
  });


  // iOS / Apple
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  });


  // Windows 10/11 ekstra
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Microsoft Connect Test");
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Microsoft NCSI");
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/manifest.json", "application/manifest+json");
  });
  server.on("/192x192.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/192x192.png", "image/png");
  });
  server.on("/512x512.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/512x512.png", "image/png");
  });
  server.on("/sw.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/sw.js", "application/javascript");
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
    if (!isScanning && !shouldStartScan) {
      shouldStartScan = true;
      request->send(202, "application/json", "{\"status\":\"scanning\"}");
    } else {
      request->send(200, "application/json", "{\"status\":\"busy\"}");
    }
  });

  server.on("/api/wifi-list", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getWifiListJson());
  });

  server.on("/api/wifi-connect", HTTP_POST, [](AsyncWebServerRequest *request){
    String ssid = request->arg("ssid");
    String pass = request->arg("pass");

    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    request->send(200, "application/json", "{\"status\":\"started\"}");

    delay(1000);
    ESP.restart();
  });

  server.on("/api/device-state", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"setup\":";
    json += (setupMode && !skipSetup) ? "true" : "false";
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/api/skip-setup", HTTP_POST, [](AsyncWebServerRequest *request){
    skipSetup = true;
    setupMode = false;

    Preferences setupPrefs;
    setupPrefs.begin("setup", false);
    setupPrefs.putBool("skip", true);
    setupPrefs.end();
    
    initWiFi();
    
    request->send(200, "application/json", "{\"status\":\"skipped\"}");
  });

  server.on("/api/reset-setup", HTTP_POST, [](AsyncWebServerRequest *request){
    skipSetup = false;

    Preferences setupPrefs;
    setupPrefs.begin("setup", false);
    setupPrefs.remove("skip");
    setupPrefs.end();

    request->send(200, "application/json", "{\"status\":\"reset\"}");
  });

  server.on("/generate_204", HTTP_ANY, [](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.on("/gen_204", HTTP_ANY, [](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.on("/hotspot-detect.html", HTTP_ANY, [](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.on("/connectivitycheck.gstatic.com", HTTP_ANY, [](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.on("/api/ota-auto", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (otaStatus == "started" || otaStatus == "updating") {
      request->send(200, "application/json", "{\"status\":\"busy\"}");
      return;
    }
    shouldUpdate = true;
    otaStatus = "started";
    request->send(200, "application/json", "{\"status\":\"started\"}");
  });

  server.on("/api/ota-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"status\":\"" + otaStatus + "\"}";
    request->send(200, "application/json", json);
  });

  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"version\":\"" + String(FIRMWARE_VERSION) + "\"}";
    request->send(200, "application/json", json);
  });

}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
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
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String cmd = String((char *)data, len);
      processCommand(cmd);
    }
  }
}

void startSetupMode() {
  Serial.println("SETUP MODE AKTIF");

  WiFi.mode(WIFI_AP);
  IPAddress apIP(8, 8, 8, 8);
  IPAddress gateway(8, 8, 8, 8);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPConfig(apIP, gateway, subnet);
  WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS);

  dnsServer.start(53, "*", apIP);

  Serial.print("Setup IP: ");
  Serial.println(WiFi.softAPIP());
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
