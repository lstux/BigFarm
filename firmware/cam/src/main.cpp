// BigFarm Cam — firmware de diagnostic (Jalon 0 / Jalon 1).
//
// Au démarrage : informations système, test PSRAM, initialisation de la caméra
// (modèle du capteur), mesure de capture, test de la microSD, Wi-Fi.
// Ensuite : page de diagnostic web, photo à la demande, console série.

#include <Arduino.h>
#include <BigFarmCore.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <esp_camera.h>

#include "board_esp32cam.h"

#ifndef BF_FW_VERSION
#define BF_FW_VERSION "dev"
#endif

static WebServer server(80);
static bf::WifiSetup wifi;
static bf::SerialConsole console;

// ---------------------------------------------------------------------------
// Caméra

struct CameraBenchmark {
  bool done = false;
  String resolution;
  uint8_t requested = 0;
  uint8_t captured = 0;
  uint32_t avgMs = 0;
  size_t minBytes = 0;
  size_t maxBytes = 0;
  size_t avgBytes = 0;
};

struct CameraDiag {
  bool ok = false;
  esp_err_t error = ESP_OK;
  String model;
  uint16_t pid = 0;
  uint8_t sccbAddress = 0;
  String maxResolution;
  bool jpeg = false;
  bool buffersInPsram = false;
  uint8_t bufferCount = 0;
  CameraBenchmark bench;
};

static CameraDiag camDiag;
static bf::PsramTestResult psramTest;
static bf::StorageTestResult sdTest;

static String resolutionText(framesize_t size) {
  char buf[32];
  snprintf(buf, sizeof buf, "%ux%u", resolution[size].width, resolution[size].height);
  return String(buf);
}

static void initCamera() {
  camera_config_t c = {};
  c.pin_pwdn = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;
  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_d7 = CAM_PIN_D7;
  c.pin_d6 = CAM_PIN_D6;
  c.pin_d5 = CAM_PIN_D5;
  c.pin_d4 = CAM_PIN_D4;
  c.pin_d3 = CAM_PIN_D3;
  c.pin_d2 = CAM_PIN_D2;
  c.pin_d1 = CAM_PIN_D1;
  c.pin_d0 = CAM_PIN_D0;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href = CAM_PIN_HREF;
  c.pin_pclk = CAM_PIN_PCLK;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    // Tampons dimensionnés pour la plus grande résolution courante (UXGA).
    c.frame_size = FRAMESIZE_UXGA;
    c.jpeg_quality = 10;
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Sans PSRAM : petite résolution, un seul tampon en RAM interne.
    c.frame_size = FRAMESIZE_CIF;
    c.jpeg_quality = 12;
    c.fb_count = 1;
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }
  camDiag.buffersInPsram = (c.fb_location == CAMERA_FB_IN_PSRAM);
  camDiag.bufferCount = c.fb_count;

  Serial.println("[cam] initialisation de la caméra...");
  camDiag.error = esp_camera_init(&c);
  if (camDiag.error != ESP_OK) {
    Serial.printf("[cam] ÉCHEC : 0x%x (%s)\n", camDiag.error, esp_err_to_name(camDiag.error));
    return;
  }
  camDiag.ok = true;

  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {
    camDiag.pid = s->id.PID;
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&s->id);
    if (info != nullptr) {
      camDiag.model = info->name;
      camDiag.sccbAddress = info->sccb_addr;
      camDiag.maxResolution = resolutionText(info->max_size);
      camDiag.jpeg = info->support_jpeg;
    } else {
      camDiag.model = "capteur non répertorié";
    }
  }
  Serial.printf("[cam] capteur : %s (PID 0x%04x)\n", camDiag.model.c_str(), camDiag.pid);
}

// Capture plusieurs images à une résolution donnée et mesure le temps et la taille.
static void benchmarkCamera(framesize_t size, uint8_t frames) {
  CameraBenchmark &b = camDiag.bench;
  b = CameraBenchmark();
  b.done = true;
  b.resolution = resolutionText(size);
  b.requested = frames;
  sensor_t *s = esp_camera_sensor_get();
  if (!camDiag.ok || s == nullptr) return;

  s->set_framesize(s, size);
  // Les premières images après un changement de résolution sont à jeter.
  for (int i = 0; i < 2; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }

  uint32_t totalMs = 0;
  size_t totalBytes = 0;
  b.minBytes = SIZE_MAX;
  for (uint8_t i = 0; i < frames; i++) {
    const uint32_t t0 = millis();
    camera_fb_t *fb = esp_camera_fb_get();
    const uint32_t dt = millis() - t0;
    if (fb == nullptr) continue;
    b.captured++;
    totalMs += dt;
    totalBytes += fb->len;
    if (fb->len < b.minBytes) b.minBytes = fb->len;
    if (fb->len > b.maxBytes) b.maxBytes = fb->len;
    esp_camera_fb_return(fb);
  }
  if (b.captured) {
    b.avgMs = totalMs / b.captured;
    b.avgBytes = totalBytes / b.captured;
  } else {
    b.minBytes = 0;
  }
  Serial.printf("[cam] %s : %u/%u images, %lu ms/image, %u octets en moyenne\n", b.resolution.c_str(),
                b.captured, b.requested, static_cast<unsigned long>(b.avgMs), static_cast<unsigned>(b.avgBytes));
}

static void addCamera(bf::JsonWriter &j) {
  j.beginObject("camera");
  j.add("etat", camDiag.ok ? "OK" : "ÉCHEC");
  if (!camDiag.ok) {
    j.add("erreur", esp_err_to_name(camDiag.error));
  } else {
    j.add("capteur", camDiag.model);
    char pid[8];
    snprintf(pid, sizeof pid, "0x%04x", camDiag.pid);
    j.add("pid", pid);
    char addr[8];
    snprintf(addr, sizeof addr, "0x%02x", camDiag.sccbAddress);
    j.add("adresse_sccb", addr);
    j.add("resolution_max", camDiag.maxResolution);
    j.add("jpeg_materiel", camDiag.jpeg);
    j.add("tampons", camDiag.bufferCount);
    j.add("tampons_en_psram", camDiag.buffersInPsram);
    const CameraBenchmark &b = camDiag.bench;
    if (b.done) {
      j.beginObject("mesure_capture");
      j.add("resolution", b.resolution);
      j.add("images", String(b.captured) + "/" + String(b.requested));
      j.add("ms_par_image", b.avgMs);
      if (b.avgMs) j.add("images_par_seconde", 1000.0 / b.avgMs, 1);
      j.add("taille_moyenne", bf::humanBytes(b.avgBytes));
      j.add("taille_min", bf::humanBytes(b.minBytes));
      j.add("taille_max", bf::humanBytes(b.maxBytes));
      j.endObject();
    }
  }
  j.endObject();
}

// ---------------------------------------------------------------------------
// microSD (SDMMC 1 bit)

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
// Diagnostic

static String buildDiag() {
  bf::JsonWriter j;
  j.beginObject();
  bf::addSystemInfo(j);
  bf::addPsramTest(j, psramTest);
  addCamera(j);
  bf::addStorage(j, "microsd", sdTest);
  wifi.addStatus(j);
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

static const char kCamExtraHtml[] PROGMEM = R"HTML(
<section><h2>Photo</h2>
<p><button onclick="var i=document.getElementById('photo');i.src='/capture?t='+Date.now();">Prendre une photo</button></p>
<img id="photo" alt="">
</section>
)HTML";

static void handleCapture() {
  if (!camDiag.ok) {
    server.send(503, "text/plain; charset=utf-8", "Caméra indisponible");
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    server.send(503, "text/plain; charset=utf-8", "Capture impossible");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.sendContent(reinterpret_cast<const char *>(fb->buf), fb->len);
  esp_camera_fb_return(fb);
}

static void setupWeb() {
  server.on("/", HTTP_GET, []() { bf::sendDiagPage(server, kCamExtraHtml); });
  server.on("/api/diag", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", buildDiag());
  });
  server.on("/capture", HTTP_GET, handleCapture);
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
  console.add("mesure", "relance la mesure de capture (SVGA, 10 images)",
              [](const String &) { benchmarkCamera(FRAMESIZE_SVGA, 10); });
  console.add("sd", "relance le test de la microSD", [](const String &) {
    SD_MMC.end();
    testSd();
    digitalWrite(CAM_PIN_FLASH_LED, LOW);
  });
  console.add("redemarrer", "redémarre le module", [](const String &) { ESP.restart(); });
}

// ---------------------------------------------------------------------------

void setup() {
  // LED blanche éteinte dès le démarrage.
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
  initCamera();
  benchmarkCamera(FRAMESIZE_SVGA, 10);

  testSd();
  digitalWrite(CAM_PIN_FLASH_LED, LOW);

  wifi.begin(server, bf::moduleId());
  setupWeb();
  setupConsole();

  printDiag();
  Serial.println("Tapez « aide » pour la liste des commandes.");
  digitalWrite(CAM_PIN_STATUS_LED, LOW);  // allumée : démarrage terminé
}

void loop() {
  server.handleClient();
  wifi.loop();
  console.loop(Serial);
  delay(2);
}
