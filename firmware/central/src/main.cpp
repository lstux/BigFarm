// BigFarm Central — firmware de diagnostic (Jalon 0).
//
// Au démarrage : informations système (taille réelle de la Flash et de la PSRAM),
// test PSRAM, scan du bus I2C du tactile et identification du GT911,
// test du lecteur microSD, Wi-Fi.
// Ensuite : page de diagnostic web et console série.
//
// L'écran n'est pas encore initialisé : ce sera l'étape suivante (ST7701 + LVGL).
// Les sorties de la carte fille ne sont pas touchées.

#include <Arduino.h>
#include <BigFarmCore.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <Wire.h>

#include <vector>

#include "board_guition_4848s040.h"

#ifndef BF_FW_VERSION
#define BF_FW_VERSION "dev"
#endif

static WebServer server(80);
static bf::WifiSetup wifi;
static bf::SerialConsole console;

static bf::PsramTestResult psramTest;
static bf::StorageTestResult sdTest;
static SPIClass sdSpi(FSPI);

// ---------------------------------------------------------------------------
// Tactile (I2C)

struct TouchDiag {
  bool done = false;
  std::vector<uint8_t> found;  // adresses qui répondent sur le bus du tactile
  bool gt911 = false;
  uint8_t gt911Address = 0;
  String productId;
  uint16_t firmware = 0;
};
static TouchDiag touchDiag;

// Lit des registres 16 bits du GT911 (adresse de registre sur 2 octets, poids fort d'abord).
static bool gt911Read(uint8_t addr, uint16_t reg, uint8_t *out, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

static void scanTouchBus() {
  touchDiag = TouchDiag();
  touchDiag.done = true;
  Serial.println("[i2c] scan du bus du tactile...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) touchDiag.found.push_back(addr);
  }
  for (size_t i = 0; i < touchDiag.found.size(); i++) {
    const uint8_t addr = touchDiag.found[i];
    Serial.printf("[i2c]   0x%02x\n", addr);
    if (addr != BOARD_GT911_ADDR_A && addr != BOARD_GT911_ADDR_B) continue;
    // Registres 0x8140-0x8143 : identifiant produit (ASCII), 0x8144-0x8145 : firmware.
    uint8_t buf[6] = {0};
    if (gt911Read(addr, 0x8140, buf, sizeof buf)) {
      char id[5];
      memcpy(id, buf, 4);
      id[4] = '\0';
      touchDiag.gt911 = true;
      touchDiag.gt911Address = addr;
      touchDiag.productId = id;
      touchDiag.firmware = buf[4] | (buf[5] << 8);
      Serial.printf("[i2c] contrôleur tactile : GT%s à 0x%02x, firmware 0x%04x\n", id, addr, touchDiag.firmware);
    }
  }
  if (touchDiag.found.empty()) Serial.println("[i2c]   aucun périphérique");
}

static void addTouch(bf::JsonWriter &j) {
  j.beginObject("tactile");
  char pins[24];
  snprintf(pins, sizeof pins, "SDA %d, SCL %d", BOARD_TOUCH_SDA, BOARD_TOUCH_SCL);
  j.add("bus_i2c", pins);
  j.beginArray("adresses_trouvees");
  for (size_t i = 0; i < touchDiag.found.size(); i++) {
    char a[8];
    snprintf(a, sizeof a, "0x%02x", touchDiag.found[i]);
    j.add(nullptr, a);
  }
  j.endArray();
  if (touchDiag.gt911) {
    j.add("controleur", "GT" + touchDiag.productId);
    char a[8];
    snprintf(a, sizeof a, "0x%02x", touchDiag.gt911Address);
    j.add("adresse", a);
    char fw[8];
    snprintf(fw, sizeof fw, "0x%04x", touchDiag.firmware);
    j.add("firmware", fw);
  } else {
    j.add("controleur", "non identifié");
  }
  j.endObject();
}

// ---------------------------------------------------------------------------
// microSD (SPI)

static void testSd() {
  sdTest = bf::StorageTestResult();
  sdTest.done = true;
  char bus[48];
  snprintf(bus, sizeof bus, "SPI (CS %d, SCK %d, MOSI %d, MISO %d)", BOARD_SD_CS, BOARD_SD_SCK, BOARD_SD_MOSI,
           BOARD_SD_MISO);
  sdTest.bus = bus;
  Serial.println("[sd] montage de la microSD...");
  if (!SD.begin(BOARD_SD_CS, sdSpi, 20000000)) {
    sdTest.error = "carte absente, illisible ou brochage différent";
    Serial.println("[sd] aucune carte lisible");
    return;
  }
  sdTest.mounted = true;
  sdTest.cardType = bf::cardTypeText(SD.cardType());
  sdTest.cardSize = SD.cardSize();
  sdTest.totalBytes = SD.totalBytes();
  sdTest.usedBytes = SD.usedBytes();
  bf::runReadWriteTest(SD, sdTest, 1024 * 1024);
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
  addTouch(j);
  bf::addStorage(j, "microsd", sdTest);
  j.beginObject("ecran");
  j.add("etat", "non initialisé (étape suivante : ST7701 + LVGL)");
  j.endObject();
  j.beginObject("sorties");
  j.add("etat", "non pilotées (carte fille à identifier avant tout usage)");
  j.endObject();
  wifi.addStatus(j);
  j.endObject();
  return j.str();
}

static void printDiag() {
  Serial.println("===== Diagnostic BigFarm Central =====");
  bf::printJsonPretty(Serial, buildDiag());
  Serial.println("======================================");
}

// ---------------------------------------------------------------------------
// Web et console

static void setupWeb() {
  server.on("/", HTTP_GET, []() { bf::sendDiagPage(server, nullptr); });
  server.on("/api/diag", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", buildDiag());
  });
  server.begin();
  Serial.println("[web] serveur démarré sur le port 80");
}

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
  console.add("i2c", "relance le scan du bus du tactile", [](const String &) { scanTouchBus(); });
  console.add("sd", "relance le test de la microSD", [](const String &) {
    SD.end();
    testSd();
  });
  console.add("redemarrer", "redémarre la centrale", [](const String &) { ESP.restart(); });
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  bf::setModule("central", "BigFarm Central", BF_FW_VERSION);
  Serial.printf("\n\nBigFarm Central %s — %s\n", BF_FW_VERSION, bf::moduleId().c_str());
  Serial.printf("[mem] Flash détectée %s (configurée %s), PSRAM %s\n",
                bf::humanBytes(bf::detectedFlashSize()).c_str(), bf::humanBytes(ESP.getFlashChipSize()).c_str(),
                psramFound() ? bf::humanBytes(ESP.getPsramSize()).c_str() : "absente");

  psramTest = bf::runPsramTest(2 * 1024 * 1024);
  Serial.printf("[psram] test : %s\n", psramTest.ok ? "OK" : psramTest.error.c_str());

  Wire.begin(BOARD_TOUCH_SDA, BOARD_TOUCH_SCL, BOARD_TOUCH_I2C_FREQ);
  scanTouchBus();

  sdSpi.begin(BOARD_SD_SCK, BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CS);
  testSd();

  wifi.begin(server, bf::moduleId());
  setupWeb();
  setupConsole();

  printDiag();
  Serial.println("Tapez « aide » pour la liste des commandes.");
}

void loop() {
  server.handleClient();
  wifi.loop();
  console.loop(Serial);
  delay(2);
}
