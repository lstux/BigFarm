#include "bf_ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_ota_ops.h>

#include "bf_system.h"
#include "bf_web.h"

namespace bf {

static const char kOtaPassword[] = "bigfarm-setup";
static const uint16_t kOtaPort = 3232;

static WebServer *gServer = nullptr;
static OtaHooks gHooks;
static volatile bool gInProgress = false;
static uint32_t gRestartAt = 0;
static String gLastResult;
static uint8_t gLastPercent = 255;

static void notifyStart() {
  gInProgress = true;
  gLastPercent = 255;
  if (gHooks.onStart) gHooks.onStart();
}

static void notifyProgress(uint32_t done, uint32_t total) {
  if (total == 0) return;
  const uint8_t pct = static_cast<uint8_t>(done * 100ULL / total);
  if (pct == gLastPercent) return;
  gLastPercent = pct;
  if (gHooks.onProgress) gHooks.onProgress(pct);
}

static void notifyEnd(bool ok, const String &message) {
  gInProgress = false;
  gLastResult = message;
  Serial.printf("[maj] %s\n", message.c_str());
  if (gHooks.onEnd) gHooks.onEnd(ok, message.c_str());
}

// ---------------------------------------------------------------------------
// Page web /maj

static const char kPageHtml[] PROGMEM = R"HTML(
<section><h2>Mettre à jour le firmware</h2>
<p>Fichier produit par PlatformIO : <code>.pio/build/&lt;environnement&gt;/firmware.bin</code>.
Le module redémarre sur la nouvelle version une fois le fichier vérifié.</p>
<form id="f" method="post" action="/maj" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required>
<p><button type="submit">Envoyer la mise à jour</button></p>
</form>
<p id="etat"></p>
</section>
<script>
document.getElementById('f').onsubmit=function(ev){
  ev.preventDefault();
  var fichier=this.elements.firmware.files[0];if(!fichier)return;
  var d=new FormData();d.append('firmware',fichier,fichier.name);
  var x=new XMLHttpRequest(),etat=document.getElementById('etat');
  x.upload.onprogress=function(e){if(e.lengthComputable)etat.textContent='Envoi : '+Math.round(e.loaded*100/e.total)+' %';};
  x.onload=function(){etat.textContent=x.responseText;};
  x.onerror=function(){etat.textContent='Connexion perdue (le module redémarre peut-être déjà).';};
  x.open('POST','/maj');x.send(d);etat.textContent='Envoi…';
};
</script>
)HTML";

static void handlePage() {
  String page = pageHeader(String(moduleLabel()) + " — mise à jour");
  const esp_partition_t *running = esp_ota_get_running_partition();
  page += "<section><p>Version actuelle : <b>" + htmlEscape(firmwareVersion()) + "</b>";
  if (running != nullptr) page += " (partition " + htmlEscape(running->label) + ")";
  page += ". Place disponible : " + humanBytes(ESP.getFreeSketchSpace()) + ".</p>";
  if (gLastResult.length()) page += "<p>Dernière mise à jour : " + htmlEscape(gLastResult) + "</p>";
  page += F("</section>");
  page += FPSTR(kPageHtml);
  page += pageFooter();
  gServer->send(200, "text/html; charset=utf-8", page);
}

static void handleUpload() {
  HTTPUpload &up = gServer->upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[maj] réception de %s\n", up.filename.c_str());
    notifyStart();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning() && Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.printError(Serial);
    }
    // Taille totale inconnue côté serveur : progression approximative sur la place disponible.
    notifyProgress(up.totalSize, ESP.getFreeSketchSpace());
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.isRunning()) Update.end(true);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    notifyEnd(false, "envoi interrompu");
  }
}

static void handleDone() {
  if (Update.hasError() || !Update.isFinished()) {
    const String err = String("échec : ") + Update.errorString();
    notifyEnd(false, err);
    gServer->send(500, "text/plain; charset=utf-8", "Échec de la mise à jour (" + String(Update.errorString()) +
                                                         "). Le module garde sa version actuelle.");
    return;
  }
  notifyEnd(true, "réussie, redémarrage");
  gServer->sendHeader("Connection", "close");
  gServer->send(200, "text/plain; charset=utf-8",
                "Mise à jour réussie. Le module redémarre, recharger la page dans une vingtaine de secondes.");
  gRestartAt = millis() + 1500;
}

// ---------------------------------------------------------------------------

void otaBegin(WebServer &server, const OtaHooks &hooks) {
  gServer = &server;
  gHooks = hooks;

  // espota (PlatformIO). Le mDNS est déjà démarré par WifiSetup : on annonce
  // seulement le service Arduino, sans redémarrer le mDNS.
  ArduinoOTA.setHostname(hostName().c_str());
  ArduinoOTA.setPassword(kOtaPassword);
  ArduinoOTA.setPort(kOtaPort);
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.setRebootOnSuccess(true);
  ArduinoOTA.onStart([]() {
    Serial.println("[maj] mise à jour par espota");
    notifyStart();
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) { notifyProgress(done, total); });
  ArduinoOTA.onEnd([]() { notifyEnd(true, "réussie, redémarrage"); });
  ArduinoOTA.onError([](ota_error_t e) {
    const char *msg = e == OTA_AUTH_ERROR      ? "mot de passe refusé"
                      : e == OTA_BEGIN_ERROR   ? "démarrage impossible (place ?)"
                      : e == OTA_CONNECT_ERROR ? "connexion perdue"
                      : e == OTA_RECEIVE_ERROR ? "réception interrompue"
                                               : "vérification finale échouée";
    notifyEnd(false, String("échec : ") + msg);
  });
  ArduinoOTA.begin();
  MDNS.enableArduino(kOtaPort, true);

  server.on("/maj", HTTP_GET, handlePage);
  server.on("/maj", HTTP_POST, handleDone, handleUpload);
  addNavLink("/maj", "Mise à jour");
  Serial.printf("[maj] OTA prêt : espota sur %s.local:%u, ou page /maj\n", hostName().c_str(), kOtaPort);
}

void otaLoop() {
  ArduinoOTA.handle();
  if (gRestartAt != 0 && static_cast<int32_t>(millis() - gRestartAt) >= 0) {
    Serial.println("[maj] redémarrage");
    delay(100);
    ESP.restart();
  }
}

bool otaInProgress() { return gInProgress; }

void addOtaStatus(JsonWriter &j) {
  j.beginObject("mise_a_jour");
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running != nullptr) j.add("partition", running->label);
  j.add("place_disponible", humanBytes(ESP.getFreeSketchSpace()));
  j.add("espota_port", kOtaPort);
  j.add("en_cours", static_cast<bool>(gInProgress));
  if (gLastResult.length()) j.add("derniere", gLastResult);
  j.endObject();
}

}  // namespace bf
