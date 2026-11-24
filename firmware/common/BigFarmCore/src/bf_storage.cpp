#include "bf_storage.h"

#include <sd_defines.h>

#include "bf_system.h"

namespace bf {

static const char *kTestFile = "/bigfarm-test.bin";
static const size_t kChunk = 4096;

const char *cardTypeText(uint8_t type) {
  switch (type) {
    case CARD_NONE: return "aucune";
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SD";
    case CARD_SDHC: return "SDHC/SDXC";
    default: return "inconnu";
  }
}

static void fillChunk(uint8_t *buf, size_t chunkIndex) {
  for (size_t i = 0; i < kChunk; i++) buf[i] = static_cast<uint8_t>((chunkIndex * 31 + i * 7) & 0xFF);
}

void runReadWriteTest(fs::FS &fs, StorageTestResult &r, size_t bytes) {
  r.rwOk = false;
  r.testBytes = bytes;
  const size_t chunks = bytes / kChunk;
  uint8_t *buf = static_cast<uint8_t *>(malloc(kChunk));
  uint8_t *expected = static_cast<uint8_t *>(malloc(kChunk));
  if (buf == nullptr || expected == nullptr) {
    free(buf);
    free(expected);
    r.error = "mémoire insuffisante pour le test";
    return;
  }

  // Écriture (la fermeture est comptée : elle vide les tampons).
  uint32_t t0 = millis();
  File f = fs.open(kTestFile, FILE_WRITE);
  if (!f) {
    r.error = "impossible de créer le fichier de test";
    free(buf);
    free(expected);
    return;
  }
  for (size_t c = 0; c < chunks; c++) {
    fillChunk(buf, c);
    if (f.write(buf, kChunk) != kChunk) {
      r.error = "écriture incomplète";
      f.close();
      fs.remove(kTestFile);
      free(buf);
      free(expected);
      return;
    }
  }
  f.close();
  r.writeMs = millis() - t0;

  // Relecture et vérification.
  t0 = millis();
  f = fs.open(kTestFile, FILE_READ);
  bool ok = static_cast<bool>(f);
  if (!ok) r.error = "impossible de relire le fichier";
  for (size_t c = 0; ok && c < chunks; c++) {
    if (f.read(buf, kChunk) != kChunk) {
      ok = false;
      r.error = "lecture incomplète";
      break;
    }
    fillChunk(expected, c);
    if (memcmp(buf, expected, kChunk) != 0) {
      ok = false;
      r.error = "contenu relu différent";
    }
  }
  if (f) f.close();
  r.readMs = millis() - t0;

  fs.remove(kTestFile);
  free(buf);
  free(expected);
  r.rwOk = ok;
}

static double rateKoS(size_t bytes, uint32_t ms) { return ms ? bytes / 1024.0 / (ms / 1000.0) : 0.0; }

void addStorage(JsonWriter &json, const char *key, const StorageTestResult &r) {
  json.beginObject(key);
  if (!r.done) {
    json.add("etat", "non testé");
  } else if (!r.mounted) {
    json.add("etat", "non montée");
    if (r.bus.length()) json.add("bus", r.bus);
    if (r.error.length()) json.add("erreur", r.error);
  } else {
    json.add("etat", r.rwOk ? "OK" : "ÉCHEC du test lecture/écriture");
    json.add("bus", r.bus);
    json.add("type", r.cardType);
    json.add("capacite_carte", humanBytes(r.cardSize));
    json.add("espace_total", humanBytes(r.totalBytes));
    json.add("espace_utilise", humanBytes(r.usedBytes));
    json.add("taille_test", humanBytes(r.testBytes));
    json.add("ecriture_ko_s", rateKoS(r.testBytes, r.writeMs), 0);
    json.add("lecture_ko_s", rateKoS(r.testBytes, r.readMs), 0);
    if (r.error.length()) json.add("erreur", r.error);
  }
  json.endObject();
}

}  // namespace bf
