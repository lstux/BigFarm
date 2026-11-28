#pragma once

#include <Arduino.h>

#include "bf_json.h"

namespace bf {

// Identité du module : à appeler en tout premier dans setup().
// type : identifiant court ("cam", "central"...), label : nom lisible.
void setModule(const char *type, const char *label, const char *version);
const char *moduleType();
const char *moduleLabel();
const char *firmwareVersion();

// Identifiant stable du module, dérivé de l'adresse MAC : "bf-cam-a1b2c3".
String moduleId();

// Nom du module sur le réseau (nom d'hôte et mDNS : <nom>.local).
// Par défaut, l'identifiant du module. Personnalisable, pris en compte au redémarrage.
String hostName();
// Lettres minuscules, chiffres et tirets, 1 à 32 caractères, sans tiret au début ni à la fin.
// Une chaîne vide revient au nom par défaut. Renvoie false si le nom est invalide.
bool setHostName(const String &name);
bool validHostName(const String &name);

// Taille lisible : "16.0 Mo", "320.0 Ko", "512 o".
String humanBytes(uint64_t bytes);

// Taille de la Flash lue sur la puce (identifiant JEDEC), indépendante de la
// configuration de compilation. 0 si la lecture échoue.
uint32_t detectedFlashSize();

const char *resetReasonText();

// Section "systeme" : puce, mémoire, Flash, PSRAM, firmware, redémarrage.
void addSystemInfo(JsonWriter &json);

// Test de la PSRAM : écriture puis relecture d'un motif, avec débit.
struct PsramTestResult {
  bool done = false;
  bool ok = false;
  size_t bytes = 0;
  uint32_t writeUs = 0;
  uint32_t readUs = 0;
  String error;
};
PsramTestResult runPsramTest(size_t bytes);
void addPsramTest(JsonWriter &json, const PsramTestResult &result);

}  // namespace bf
