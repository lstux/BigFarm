#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "bf_json.h"

namespace bf {

// Mises à jour sans fil (OTA), deux façons :
// - depuis PlatformIO : environnement « -ota » (protocole espota, port 3232) ;
// - depuis le navigateur : page /maj, envoi du fichier firmware.bin.
// Mot de passe espota : le même que le point d'accès de configuration (bigfarm-setup).
struct OtaHooks {
  void (*onStart)() = nullptr;                 // début de la mise à jour
  void (*onProgress)(uint8_t percent) = nullptr;
  void (*onEnd)(bool ok, const char *message) = nullptr;
};

// À appeler après WifiSetup::begin() (le mDNS doit être démarré).
void otaBegin(WebServer &server, const OtaHooks &hooks);
// À appeler dans loop().
void otaLoop();
// Vrai pendant une mise à jour (les modules peuvent suspendre leurs tâches lourdes).
bool otaInProgress();
void addOtaStatus(JsonWriter &json);

}  // namespace bf
