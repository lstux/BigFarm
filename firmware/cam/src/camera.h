#pragma once

#include <Arduino.h>
#include <BigFarmCore.h>
#include <esp_camera.h>

// Service caméra : initialisation, réglages persistants, mesure de capture.
namespace cam {

struct Settings {
  uint8_t frameSize = FRAMESIZE_VGA;
  uint8_t quality = 12;  // 4 (meilleure) à 63 (plus compressée)
  int8_t brightness = 0;  // -2 à 2
  int8_t contrast = 0;    // -2 à 2
  int8_t saturation = 0;  // -2 à 2
  bool vflip = false;
  bool hmirror = false;
  bool awb = true;  // balance des blancs automatique
  bool aec = true;  // exposition automatique
  bool agc = true;  // gain automatique
  uint8_t led = 0;     // intensité de la LED blanche, 0 = éteinte
  uint8_t maxFps = 0;  // limite du flux, 0 = sans limite
};

// Initialise la caméra puis applique les réglages enregistrés.
bool begin();
bool ready();

const Settings &settings();
// Vérifie, applique et enregistre de nouveaux réglages. Renvoie false et un
// message si une valeur est hors limites (rien n'est appliqué dans ce cas).
bool update(const Settings &s, String &error);

// Plus grande résolution utilisable (limitée par le capteur et les tampons).
framesize_t maxFrameSize();
String resolutionText(framesize_t size);
// Nom court : "VGA", "SVGA"...
const char *resolutionName(framesize_t size);

// Mesure de capture : plusieurs images à une résolution donnée, puis retour à la
// résolution réglée.
void benchmark(framesize_t size, uint8_t frames);

void addDiag(bf::JsonWriter &json);      // section "camera" du diagnostic
void addSettings(bf::JsonWriter &json);  // "reglages" + "resolutions"

}  // namespace cam
