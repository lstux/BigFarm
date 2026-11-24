#pragma once

#include <Arduino.h>
#include <FS.h>

#include "bf_json.h"

namespace bf {

// Résultat du diagnostic d'une carte SD / microSD.
struct StorageTestResult {
  bool done = false;
  bool mounted = false;
  String bus;       // "SDMMC 1 bit", "SPI"...
  String cardType;  // SDHC, SD, MMC...
  uint64_t cardSize = 0;
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
  bool rwOk = false;
  size_t testBytes = 0;
  uint32_t writeMs = 0;
  uint32_t readMs = 0;
  String error;
};

// Type de carte à partir de la valeur renvoyée par SD.cardType() / SD_MMC.cardType().
const char *cardTypeText(uint8_t type);

// Écrit un fichier de test, le relit, vérifie son contenu, mesure les débits
// puis le supprime. Remplit rwOk, testBytes, writeMs, readMs et error.
void runReadWriteTest(fs::FS &fs, StorageTestResult &result, size_t bytes);

void addStorage(JsonWriter &json, const char *key, const StorageTestResult &result);

}  // namespace bf
