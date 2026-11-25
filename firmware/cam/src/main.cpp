// BigFarm Cam — module caméra.
//
// - Page « Caméra » (/) : flux en direct, photo, réglages enregistrés.
// - Flux MJPEG : http://<module>:81/stream
// - Page « Photos » (/photos) : photos enregistrées sur la microSD.
// - API : /api/camera (réglages, GET/POST), /api/info (identité, pour la centrale),
//   /api/diag (diagnostic complet), /capture (photo JPEG sans enregistrement),
//   /api/photo (POST : photo enregistrée sur la microSD), /api/photos (liste).
// - Page « Diagnostic » (/diag) et console série (« aide »).

#include <Arduino.h>
#include <BigFarmCore.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_camera.h>

#include "board_esp32cam.h"
#include "camera.h"
#include "photos.h"
#include "stream.h"
#include "web_page.h"

#ifndef BF_FW_VERSION
#define BF_FW_VERSION "dev"
#endif

static WebServer server(80);
static bf::WifiSetup wifi;
static bf::SerialConsole console;

static bf::PsramTestResult psramTest;
static bf::StorageTestResult sdTest;

// ---------------------------------------------------------------------------
// microSD (SDMMC 1 bit : GPIO4 reste libre pour la LED)

static void testSd() {
  sdTest = bf::StorageTestResult();
  sdTest.done = true;
  sdTest.bus = "SDMMC 1 bit";
  Serial.println("[sd] montage de la microSD...");
  if (!SD_MMC.begin("/sdcard", true)) {
    sdTest.error = "carte absente ou illisible";
    Serial.println("[sd] aucune carte lisible");
    return;
  }
  sdTest.mounted = true;
  sdTest.cardType = bf::cardTypeText(SD_MMC.cardType());
  sdTest.cardSize = SD_MMC.cardSize();
  sdTest.totalBytes = SD_MMC.totalBytes();
  sdTest.usedBytes = SD_MMC.usedBytes();
  bf::runReadWriteTest(SD_MMC, sdTest, 512 * 1024);
  Serial.printf("[sd] %s, %s, test %s\n", sdTest.cardType.c_str(), bf::humanBytes(sdTest.cardSize).c_str(),
                sdTest.rwOk ? "OK" : sdTest.error.c_str());
}

// ---------------------------------------------------------------------------
// JSON

static String buildDiag() {
  bf::JsonWriter j;
  j.beginObject();
  bf::addSystemInfo(j);
  bf::addPsramTest(j, psramTest);
  cam::addDiag(j);
  stream::addStatus(j);
  bf::addStorage(j, "microsd", sdTest);
  photos::addStatus(j);
  bf::addTime(j);
  wifi.addStatus(j);
  j.endObject();
  return j.str();
}

static String buildCameraJson(const char *error) {
  bf::JsonWriter j;
  j.beginObject();
  j.add("pret", cam::ready());
  if (error != nullptr) j.add("erreur", error);
  cam::addSettings(j);
  stream::addStatus(j);
  j.endObject();
  return j.str();
}

static String localIp() { return wifi.connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }

static String buildInfo() {
  const String ip = localIp();
  bf::JsonWriter j;
  j.beginObject();
  j.add("type", bf::moduleType());
  j.add("identifiant", bf::moduleId());
  j.add("nom", bf::moduleLabel());
  j.add("firmware", bf::firmwareVersion());
  j.add("camera_prete", cam::ready());
  j.beginArray("capacites");
  j.add(nullptr, "flux");
  j.add(nullptr, "photo");
  j.add(nullptr, "reglages");
  if (photos::available()) j.add(nullptr, "stockage");
  j.endArray();
  j.add("flux", "http://" + ip + ":" + String(stream::kPort) + "/stream");
  j.add("photo", "http://" + ip + "/capture");
  j.add("reglages", "http://" + ip + "/api/camera");
  j.add("enregistrer_photo", "http://" + ip + "/api/photo");
  j.endObject();
  return j.str();
}

static void printDiag() {
  Serial.println("===== Diagnostic BigFarm Cam =====");
  bf::printJsonPretty(Serial, buildDiag());
  Serial.println("==================================");
}

// ---------------------------------------------------------------------------
// Web

static void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

static bool argBool(const char *name, bool current) {
  if (!server.hasArg(name)) return current;
  const String v = server.arg(name);
  return v == "1" || v == "on" || v == "true";
}

static long argInt(const char *name, long current) {
  if (!server.hasArg(name)) return current;
  return server.arg(name).toInt();
}

static void handleCameraPost() {
  cam::Settings s = cam::settings();
  const long frameSize = argInt("resolution", s.frameSize);
  const long quality = argInt("qualite", s.quality);
  const long brightness = argInt("luminosite", s.brightness);
  const long contrast = argInt("contraste", s.contrast);
  const long saturation = argInt("saturation", s.saturation);
  const long led = argInt("led", s.led);
  const long maxFps = argInt("ips_max", s.maxFps);
  // Bornes larges ici (types), bornes métier dans cam::update().
  if (frameSize < 0 || frameSize > 255 || quality < 0 || quality > 255 || brightness < -128 ||
      brightness > 127 || contrast < -128 || contrast > 127 || saturation < -128 || saturation > 127 || led < 0 ||
      led > 255 || maxFps < 0 || maxFps > 255) {
    sendJson(400, buildCameraJson("valeur hors limites"));
    return;
  }
  s.frameSize = static_cast<uint8_t>(frameSize);
  s.quality = static_cast<uint8_t>(quality);
  s.brightness = static_cast<int8_t>(brightness);
  s.contrast = static_cast<int8_t>(contrast);
  s.saturation = static_cast<int8_t>(saturation);
  s.led = static_cast<uint8_t>(led);
  s.maxFps = static_cast<uint8_t>(maxFps);
  s.vflip = argBool("miroir_vertical", s.vflip);
  s.hmirror = argBool("miroir_horizontal", s.hmirror);
  s.awb = argBool("balance_blancs_auto", s.awb);
  s.aec = argBool("exposition_auto", s.aec);
  s.agc = argBool("gain_auto", s.agc);

  String error;
  if (!cam::update(s, error)) {
    sendJson(400, buildCameraJson(error.c_str()));
    return;
  }
  sendJson(200, buildCameraJson(nullptr));
}

static void handleCapture() {
  if (!cam::ready()) {
    server.send(503, "text/plain; charset=utf-8", "Caméra indisponible");
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    server.send(503, "text/plain; charset=utf-8", "Capture impossible");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.sendContent(reinterpret_cast<const char *>(fb->buf), fb->len);
  esp_camera_fb_return(fb);
}

static void handleCameraPage() {
  String page = bf::pageHeader(String(bf::moduleLabel()) + " — caméra");
  if (!cam::ready()) {
    page += F("<section><p class=\"err\">Caméra indisponible : voir le <a href=\"/diag\">diagnostic</a>.</p></section>");
  }
  page += FPSTR(kCameraPageHtml);
  page += bf::pageFooter();
  server.send(200, "text/html; charset=utf-8", page);
}

static void setupWeb() {
  bf::addNavLink("/", "Caméra");
  bf::addNavLink("/photos", "Photos");
  bf::addNavLink("/diag", "Diagnostic");
  server.on("/", HTTP_GET, handleCameraPage);
  server.on("/diag", HTTP_GET, []() { bf::sendDiagPage(server, nullptr); });
  server.on("/api/diag", HTTP_GET, []() { sendJson(200, buildDiag()); });
  server.on("/api/info", HTTP_GET, []() { sendJson(200, buildInfo()); });
  server.on("/api/camera", HTTP_GET, []() { sendJson(200, buildCameraJson(nullptr)); });
  server.on("/api/camera", HTTP_POST, handleCameraPost);
  server.on("/capture", HTTP_GET, handleCapture);
  photos::registerRoutes(server);
  server.begin();
  Serial.println("[web] serveur démarré sur le port 80");
}

// ---------------------------------------------------------------------------
// Console série

static void setupConsole() {
  console.add("info", "affiche le diagnostic complet", [](const String &) { printDiag(); });
  console.add("wifi", "wifi <réseau> [mot de passe] (guillemets si espaces)", [](const String &args) {
    String ssid, pass;
    bf::SerialConsole::splitFirst(args, ssid, pass);
    if (ssid.length() == 0) {
      Serial.println("Usage : wifi <réseau> [mot de passe]");
      return;
    }
    wifi.saveCredentials(ssid, pass);
  });
  console.add("oublier", "oublie le réseau Wi-Fi et redémarre", [](const String &) { wifi.forget(); });
  console.add("mesure", "mesure de capture (SVGA, 10 images)",
              [](const String &) { cam::benchmark(FRAMESIZE_SVGA, 10); });
  console.add("sd", "relance le test de la microSD", [](const String &) {
    SD_MMC.end();
    testSd();
    photos::begin(sdTest.mounted);
  });
  console.add("photo", "prend une photo et l'enregistre sur la microSD", [](const String &) {
    const photos::SaveResult r = photos::takeAndSave();
    if (!r.ok) Serial.printf("Échec : %s\n", r.error.c_str());
  });
  console.add("heure", "affiche l'heure (NTP)", [](const String &) {
    Serial.printf("%s (%s)\n", bf::nowText().c_str(), bf::timeValid() ? "synchronisée" : "non synchronisée");
  });
  console.add("redemarrer", "redémarre le module", [](const String &) { ESP.restart(); });
}

// ---------------------------------------------------------------------------

void setup() {
  // LED blanche éteinte dès le démarrage (elle passe ensuite en PWM dans cam::begin()).
  pinMode(CAM_PIN_FLASH_LED, OUTPUT);
  digitalWrite(CAM_PIN_FLASH_LED, LOW);
  pinMode(CAM_PIN_STATUS_LED, OUTPUT);
  digitalWrite(CAM_PIN_STATUS_LED, HIGH);  // éteinte (active à l'état bas)

  Serial.begin(115200);
  delay(300);
  bf::setModule("cam", "BigFarm Cam", BF_FW_VERSION);
  Serial.printf("\n\nBigFarm Cam %s — %s\n", BF_FW_VERSION, bf::moduleId().c_str());

  psramTest = bf::runPsramTest(1024 * 1024);
  Serial.printf("[psram] test : %s\n", psramTest.ok ? "OK" : psramTest.error.c_str());

  // Caméra avant la microSD : ordre recommandé sur cette carte.
  if (cam::begin()) cam::benchmark(FRAMESIZE_SVGA, 10);
  testSd();
  photos::begin(sdTest.mounted);

  wifi.begin(server, bf::moduleId());
  bf::timeBegin();
  setupWeb();
  stream::begin();
  setupConsole();

  printDiag();
  Serial.printf("Caméra : http://%s/  ·  flux : http://%s:%u/stream\n", localIp().c_str(), localIp().c_str(),
                stream::kPort);
  Serial.println("Tapez « aide » pour la liste des commandes.");
  digitalWrite(CAM_PIN_STATUS_LED, LOW);  // allumée : démarrage terminé
}

void loop() {
  server.handleClient();
  wifi.loop();
  console.loop(Serial);
  delay(2);
}
