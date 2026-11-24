#include "bf_system.h"

#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <sdkconfig.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

namespace bf {

static const char *gType = "inconnu";
static const char *gLabel = "Module BigFarm";
static const char *gVersion = "0.0.0";

void setModule(const char *type, const char *label, const char *version) {
  gType = type;
  gLabel = label;
  gVersion = version;
}
const char *moduleType() { return gType; }
const char *moduleLabel() { return gLabel; }
const char *firmwareVersion() { return gVersion; }

String moduleId() {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[40];
  // Les trois derniers octets de l'adresse MAC.
  snprintf(buf, sizeof buf, "bf-%s-%02x%02x%02x", gType, static_cast<uint8_t>(mac >> 24),
           static_cast<uint8_t>(mac >> 32), static_cast<uint8_t>(mac >> 40));
  return String(buf);
}

String humanBytes(uint64_t bytes) {
  char buf[24];
  if (bytes >= 1024ULL * 1024ULL) {
    snprintf(buf, sizeof buf, "%.1f Mo", bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024ULL) {
    snprintf(buf, sizeof buf, "%.1f Ko", bytes / 1024.0);
  } else {
    snprintf(buf, sizeof buf, "%u o", static_cast<unsigned>(bytes));
  }
  return String(buf);
}

static uint32_t flashJedecId() {
  uint32_t id = 0;
  if (esp_flash_read_id(nullptr, &id) != ESP_OK) return 0;
  return id;
}

uint32_t detectedFlashSize() {
  // Le dernier octet de l'identifiant JEDEC code la capacité : 2^n octets.
  const uint8_t capacity = flashJedecId() & 0xFF;
  if (capacity < 0x10 || capacity > 0x1F) return 0;
  return 1UL << capacity;
}

const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "mise sous tension";
    case ESP_RST_EXT: return "broche reset externe";
    case ESP_RST_SW: return "redémarrage logiciel";
    case ESP_RST_PANIC: return "plantage (panic)";
    case ESP_RST_INT_WDT: return "watchdog d'interruption";
    case ESP_RST_TASK_WDT: return "watchdog de tâche";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "réveil de deep sleep";
    case ESP_RST_BROWNOUT: return "chute de tension (brownout)";
    case ESP_RST_SDIO: return "SDIO";
    default: return "inconnue";
  }
}

void addSystemInfo(JsonWriter &json) {
  json.beginObject("systeme");
  json.add("module", moduleLabel());
  json.add("identifiant", moduleId());
  json.add("firmware", firmwareVersion());
  json.add("compile_le", __DATE__ " " __TIME__);
  json.add("puce", ESP.getChipModel());
  json.add("revision_puce", ESP.getChipRevision());
  json.add("coeurs", ESP.getChipCores());
  json.add("frequence_mhz", ESP.getCpuFreqMHz());
  json.add("sdk", ESP.getSdkVersion());
#ifdef ESP_ARDUINO_VERSION_MAJOR
  char arduino[16];
  snprintf(arduino, sizeof arduino, "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR,
           ESP_ARDUINO_VERSION_PATCH);
  json.add("arduino_esp32", arduino);
#endif
  json.add("dernier_redemarrage", resetReasonText());
  json.add("en_marche_depuis_s", millis() / 1000);

  char jedec[12];
  snprintf(jedec, sizeof jedec, "0x%06lx", static_cast<unsigned long>(flashJedecId()));
  json.add("flash_jedec", jedec);
  json.add("flash_detectee", humanBytes(detectedFlashSize()));
  json.add("flash_configuree", humanBytes(ESP.getFlashChipSize()));
  json.add("flash_vitesse_mhz", ESP.getFlashChipSpeed() / 1000000);
  json.add("programme", humanBytes(ESP.getSketchSize()));
  json.add("place_libre_programme", humanBytes(ESP.getFreeSketchSpace()));

  json.add("ram_interne", humanBytes(ESP.getHeapSize()));
  json.add("ram_interne_libre", humanBytes(ESP.getFreeHeap()));
  json.add("ram_interne_mini_libre", humanBytes(ESP.getMinFreeHeap()));

  json.add("psram_presente", psramFound());
  json.add("psram", humanBytes(ESP.getPsramSize()));
  json.add("psram_libre", humanBytes(ESP.getFreePsram()));
#if defined(CONFIG_SPIRAM_MODE_OCT)
  json.add("psram_mode_compile", "octal");
#elif defined(CONFIG_SPIRAM)
  json.add("psram_mode_compile", "quad");
#else
  json.add("psram_mode_compile", "désactivée");
#endif
  json.endObject();
}

PsramTestResult runPsramTest(size_t bytes) {
  PsramTestResult r;
  r.done = true;
  r.bytes = bytes;
  if (!psramFound()) {
    r.error = "PSRAM absente";
    return r;
  }
  uint32_t *buf = static_cast<uint32_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    r.error = "allocation impossible";
    return r;
  }
  const size_t words = bytes / sizeof(uint32_t);
  const uint32_t kMul = 2654435761UL;  // motif pseudo-aléatoire, différent à chaque adresse

  uint32_t t0 = micros();
  for (size_t i = 0; i < words; i++) buf[i] = static_cast<uint32_t>(i) * kMul;
  r.writeUs = micros() - t0;

  size_t errors = 0;
  t0 = micros();
  for (size_t i = 0; i < words; i++) {
    if (buf[i] != static_cast<uint32_t>(i) * kMul) errors++;
  }
  r.readUs = micros() - t0;
  heap_caps_free(buf);

  r.ok = (errors == 0);
  if (errors) r.error = String(static_cast<unsigned long>(errors)) + " mots incorrects";
  return r;
}

void addPsramTest(JsonWriter &json, const PsramTestResult &r) {
  json.beginObject("test_psram");
  if (!r.done) {
    json.add("etat", "non lancé");
  } else {
    json.add("etat", r.ok ? "OK" : "ÉCHEC");
    json.add("taille", humanBytes(r.bytes));
    if (r.writeUs) json.add("ecriture_mo_s", r.bytes / static_cast<double>(r.writeUs) * 1e6 / 1048576.0, 1);
    if (r.readUs) json.add("lecture_mo_s", r.bytes / static_cast<double>(r.readUs) * 1e6 / 1048576.0, 1);
    if (r.error.length()) json.add("erreur", r.error);
  }
  json.endObject();
}

}  // namespace bf
