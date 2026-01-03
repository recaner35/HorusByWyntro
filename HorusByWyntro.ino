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
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
// #include <WiFiManager.h> // REMOVED for Seamless Custom Logic
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_now.h>


// ===============================
// Motor Pin Tanımlamaları
// ===============================
// 28BYJ-48 için pin sırası: IN1-IN3-IN2-IN4 şeklinde olmalı (AccelStepper
// FULL4WIRE için) ESP32 GPIO Pinleri
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
#define GITHUB_VERSION_URL "https://recaner.github.io/horus-ota/version.json"

// ===============================
// Nesneler
// ===============================
// Motor nesnesi (FULL4WIRE modu)
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
  String hostname = "horus-master";
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
unsigned long hourDuration = 3600000; // 1 Saat (ms) -> Test için düşürülebilir

// ESP-NOW Peer Listesi
struct PeerDevice {
  String mac;
  String name;
};
std::vector<PeerDevice> peers;

// ===============================
// WiFi & Connection State
// ===============================
bool wifiScanning = false;
long lastWifiCheck = 0;
String wifiStatusStr = "disconnected";
bool tryConnect = false;
unsigned long connectStartTime = 0;

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

// ===============================
// Kurulum (Setup)
// ===============================
void setup() {
  // Seri Haberleşme
  Serial.begin(115200);
  Serial.println("\n\nHORUS BY WYNTRO - Başlatılıyor...");

  // Dosya Sistemi (LittleFS)
  if (!LittleFS.begin(true)) {
    Serial.println("HATA: LittleFS başlatılamadı!");
    return;
  }
  Serial.println("LittleFS Hazır.");

  // Ayarları Yükle
  loadConfig();

  // Pin Ayarları
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  // Motor Kurulumu
  initMotor();

  // Ağ Kurulumu (ASYNC / Seamless)
  initWiFi();

  // ESP-NOW Kurulumu
  if (esp_now_init() != ESP_OK) {
    Serial.println("HATA: ESP-NOW başlatılamadı");
  } else {
    Serial.println("ESP-NOW Hazır.");
  }

  // Web Sunucusu ve WebSocket
  initWebServer();

  // mDNS Başlat (horus.local)
  if (MDNS.begin(config.hostname.c_str())) {
    Serial.println("mDNS Başlatıldı: " + config.hostname + ".local");
  }

  // Hesaplamaları yap
  updateSchedule();
}

// ===============================
// Ana Döngü (Loop)
// ===============================
void loop() {
  // 1. WebSocket trafiğini işle
  ws.cleanupClients();

  // 1.1 WiFi İşlemleri (Async)
  handleWifiScan();
  handleWifiConnection();

  // 2. Fiziksel Buton Kontrolü
  handlePhysicalControl();

  // 3. Zamanlanmış İşler (Schedule)
  if (isRunning) {
    checkSchedule();
  } else {
    // Sistem durdurulduysa motoru serbest bırak (Enerji tasarrufu)
    if (stepper.distanceToGo() == 0) {
      stepper.disableOutputs();
    }
  }

  // 4. Motor Hareket Mantığı (Non-blocking)
  // AccelStepper run() fonksiyonu mümkün olduğunca sık çağrılmalıdır.
  if (isRunning && isMotorMoving) {
    stepper.run();

    // Hedefe ulaştı mı?
    if (stepper.distanceToGo() == 0) {
      isMotorMoving = false;
      turnsThisHour++;
      Serial.printf("Tur Tamamlandı. Saatlik Tur: %d/%d\n", turnsThisHour,
                    targetTurnsPerHour);

      // Saatlik hedef bitti mi?
      if (turnsThisHour >= targetTurnsPerHour) {
        Serial.println("Bu saatlik hedef tamamlandı. Uyku modu.");
        // Motor enerjisini kes
        stepper.disableOutputs();
      } else {
        // Yeni tur için bekleme veya hemen başlama mantığı
        // Basitlik için hemen yeni tura başlatalım ama yön değiştirerek (Eğer
        // Bi-dir ise) Burada küçük bir bekleme (delay değil, timer)
        // konulabilir. Şimdilik hemen sonraki loop'ta schedule kontrolü
        // tetikleyecek.
      }
    }
  }
}

// ===============================
// Yardımcı Fonksiyonlar: Motor
// ===============================
void initMotor() {
  stepper.setMaxSpeed(1000);    // Maksimum adım/sn
  stepper.setAcceleration(500); // İvmelenme adım/sn^2 (Yumuşak kalkış/duruş)
}

void startMotorTurn() {
  // Bir tur attır
  // 28BYJ-48 dişli oranı nedeniyle 2048 adım = 360 derece (yaklaşık)
  // İstenen süreye göre hız ayarla
  // config.duration (sn). Hız = Adım / Süre

  float speed = (float)STEPS_PER_REVOLUTION / (float)config.duration;
  stepper.setMaxSpeed(speed);

  long targetPos = STEPS_PER_REVOLUTION; // Varsayılan CW

  // Yön Mantığı
  if (config.direction == 1) { // CCW
    targetPos = -STEPS_PER_REVOLUTION;
  } else if (config.direction == 2) { // Bi-Directional
    // Çift yönde sırayla: Tek sayılar ters, çift sayılar düz olsun
    if (turnsThisHour % 2 != 0) {
      targetPos = -STEPS_PER_REVOLUTION;
    }
  }

  stepper.move(targetPos);
  stepper.enableOutputs();
  isMotorMoving = true;
  Serial.println("Motor harekete başladı...");
}

// ===============================
// Mantık: Zamanlama
// ===============================
void updateSchedule() {
  // TPD'den Saatlik Tur Hesapla (24 saatlik döngü)
  targetTurnsPerHour = config.tpd / 24;
  if (targetTurnsPerHour < 1)
    targetTurnsPerHour = 1;
  Serial.printf("Yeni Hedef: %d tur/saat\n", targetTurnsPerHour);
}

void checkSchedule() {
  // Saat kontrolü (Basit milisaniye sayacı)
  // Gerçek bir RTC olmadığı için millis() kullanıyoruz.
  // Günde birkaç dakika sapabilir ama watch winder için kritik değil.

  unsigned long now = millis();

  // Saat başı sıfırlama
  if (now - lastHourCheck > hourDuration) {
    lastHourCheck = now;
    turnsThisHour = 0;
    Serial.println("YENİ SAAT DİLİMİ BAŞLADI");
  }

  // Eğer bu saatlik hedef tamamlanmadıysa ve motor duruyorsa -> Yeni tur başlat
  if (turnsThisHour < targetTurnsPerHour && !isMotorMoving) {
    // Burada turlar arası bekleme eklenebilir. Şimdilik peş peşe yapıyor.
    startMotorTurn();
  }
}

// ===============================
// Donanım Kontrolü (Dokunmatik)
// ===============================
void handlePhysicalControl() {
  // TTP223 dijital okuma
  int reading = digitalRead(TOUCH_PIN);

  // Debounce (Basit)
  if (reading == HIGH && (millis() - lastTouchTime > 500)) {
    lastTouchTime = millis();

    // Durum değiştir
    isRunning = !isRunning;
    Serial.println(isRunning ? "FİZİKSEL: BAŞLATILDI" : "FİZİKSEL: DURDURULDU");

    // Web arayüzüne bildir
    String json = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
    ws.textAll(json);
  }
}

// ===============================
// Ağ ve Web Sunucusu
// ===============================
void initWiFi() {
  // MOD: AP + STA (Her zaman)
  WiFi.mode(WIFI_AP_STA);

  // 1. AP Başlat
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String shortMac = mac.substring(8);  // Son 4 hane
  String apName = "horus-" + shortMac; // Slugify benzeri

  WiFi.softAP(apName.c_str());
  Serial.println("AP Başlatıldı: " + apName);
  Serial.println("AP IP: " + WiFi.softAPIP().toString());

  // 2. Hostname
  WiFi.setHostname(config.hostname.c_str());

  // 3. Varsa kayıtlı ağa bağlan (Engellemeden)
  // LittleFS'den veya NVS'den okuyup bağlanabiliriz.
  // Şimdilik WiFi.begin() eğer hafızada varsa otomatik dener.
  WiFi.begin();
}

void handleWifiScan() {
  if (wifiScanning) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      Serial.printf("Tarama Tamamlandı: %d ağ bulundu.\n", n);
      wifiScanning = false;
    } else if (n == -1) {
      // Devam ediyor...
    } else if (n == -2) {
      // Başlamadı veya hata
      wifiScanning = false;
    }
  }
}

void handleWifiConnection() {
  // Bağlantı durumunu izle
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiStatusStr != "connected") {
      wifiStatusStr = "connected";
      Serial.println("WIFI BAĞLANDI! IP: " + WiFi.localIP().toString());
    }
  } else {
    if (tryConnect) {
      // Timeout kontrolü (örn 15sn)
      if (millis() - connectStartTime > 15000) {
        tryConnect = false;
        wifiStatusStr = "failed";
        Serial.println("Bağlantı zaman aşımı.");
      } else {
        wifiStatusStr = "connecting";
      }
    } else {
      wifiStatusStr = "disconnected";
    }
  }
}

String getWifiListJson() {
  int n = WiFi.scanComplete();
  if (n == -2)
    return "[]"; // Tarama yok

  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i > 0)
      json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" +
            String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]";
  WiFi.scanDelete(); // Hafıza temizle
  return json;
}

void initWebServer() {
  // Statik Dosyalar (LittleFS)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });

  server.on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/manifest.json", "application/json");
  });

  // OTA Update Formu
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/update.html", "text/html");
  });

  // OTA Update işlemi
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
        if (!index) {
          Serial.printf("Update Start: %s\n", filename.c_str());
          if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
            Update.printError(Serial);
          }
        }
        if (!Update.hasError()) {
          if (Update.write(data, len) != len) {
            Update.printError(Serial);
          }
        }
        if (final) {
          if (Update.end(true)) {
            Serial.printf("Update Success: %uB\n", index + len);
          } else {
            Update.printError(Serial);
          }
        }
      });

  // API: WiFi Tara
  server.on("/api/wifi-scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!wifiScanning) {
      WiFi.scanNetworks(true); // Async scan
      wifiScanning = true;
      request->send(202, "application/json", "{\"status\":\"scanning\"}");
    } else {
      request->send(200, "application/json", "{\"status\":\"busy\"}");
    }
  });

  // API: WiFi Listele
  server.on("/api/wifi-list", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = getWifiListJson();
    request->send(200, "application/json", json);
  });

  // API: WiFi Durum
  server.on("/api/wifi-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"status\":\"" + wifiStatusStr + "\",";
    if (WiFi.status() == WL_CONNECTED) {
      json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
      json += "\"ssid\":\"" + WiFi.SSID() + "\"";
    } else {
      json += "\"ip\":\"null\"";
    }
    json += "}";
    request->send(200, "application/json", json);
  });

  // API: WiFi Bağlan
  server.on("/api/wifi-connect", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) && request->hasParam("pass", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String pass = request->getParam("pass", true)->value();

      WiFi.begin(ssid.c_str(), pass.c_str());
      tryConnect = true;
      connectStartTime = millis();
      wifiStatusStr = "connecting";

      request->send(200, "application/json", "{\"status\":\"started\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"missing params\"}");
    }
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();
  Serial.println("HTTP Sunucusu Başlatıldı.");
}

// ===============================
// WebSocket Olayları
// ===============================
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WS İstemci bağlandı");
    // Mevcut durumu gönder
    String json = "{";
    json += "\"running\":" + String(isRunning ? "true" : "false") + ",";
    json += "\"tpd\":" + String(config.tpd) + ",";
    json += "\"dur\":" + String(config.duration) + ",";
    json += "\"dir\":" + String(config.direction);
    json += "}";
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

void processCommand(String jsonStr) {
  // JSON Parse (ArduinoJson 6)
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonStr);

  if (error) {
    Serial.println("JSON Hatası");
    return;
  }

  String type = doc["type"];

  if (type == "command") {
    String action = doc["action"];
    if (action == "start")
      isRunning = true;
    if (action == "stop")
      isRunning = false;
    updateSchedule(); // Durum değişirse zamanlamayı resetlemek gerekebilir

    // Tüm clientlara bildir
    String resp = "{\"running\":" + String(isRunning ? "true" : "false") + "}";
    ws.textAll(resp);

  } else if (type == "settings") {
    if (doc.containsKey("tpd"))
      config.tpd = doc["tpd"];
    if (doc.containsKey("dur"))
      config.duration = doc["dur"];
    if (doc.containsKey("dir"))
      config.direction = doc["dir"];

    saveConfig();
    updateSchedule();

    // Geri bildirim (Echo)
    String resp = "{";
    resp += "\"tpd\":" + String(config.tpd) + ",";
    resp += "\"dur\":" + String(config.duration) + ",";
    resp += "\"dir\":" + String(config.direction);
    resp += "}";
    ws.textAll(resp);
  }
}

// ===============================
// Dosya İşlemleri (Config)
// ===============================
void loadConfig() {
  if (LittleFS.exists(JSON_CONFIG_FILE)) {
    File file = LittleFS.open(JSON_CONFIG_FILE, "r");
    StaticJsonDocument<512> doc;
    deserializeJson(doc, file);

    config.tpd = doc["tpd"] | 600;
    config.duration = doc["dur"] | 15;
    config.direction = doc["dir"] | 0;
    config.hostname = doc["name"] | "horus-master";

    file.close();
    Serial.println("Ayarlar yüklendi.");
  } else {
    Serial.println("Ayar dosyası yok, varsayılanlar aktif.");
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
  Serial.println("Ayarlar kaydedildi.");
}
