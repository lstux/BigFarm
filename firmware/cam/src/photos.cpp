#include "photos.h"

#include <Preferences.h>
#include <SD_MMC.h>
#include <esp_camera.h>

#include <algorithm>
#include <vector>

#include "camera.h"

namespace photos {

static const char kDir[] = "/photos";
static const size_t kMaxListed = 300;
static bool gAvailable = false;
static WebServer *gServer = nullptr;
static uint32_t gSavedThisBoot = 0;
static String gLastName;

bool available() { return gAvailable; }

void begin(bool sdMounted) {
  gAvailable = false;
  if (!sdMounted) return;
  if (!SD_MMC.exists(kDir) && !SD_MMC.mkdir(kDir)) {
    Serial.println("[photos] impossible de créer /photos");
    return;
  }
  gAvailable = true;
}

// Nom de fichier sûr : lettres, chiffres, '-', '_', un seul '.', extension .jpg.
static bool validName(const String &n) {
  if (n.length() < 5 || n.length() > 48 || !n.endsWith(".jpg")) return false;
  int dots = 0;
  for (size_t i = 0; i < n.length(); i++) {
    const char c = n[i];
    if (c == '.') {
      dots++;
      continue;
    }
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
  }
  return dots == 1;
}

static String nextName() {
  String base = bf::fileTimestamp();
  if (base.length() == 0) {
    // Heure inconnue : compteur conservé en NVS.
    Preferences p;
    p.begin("bf-cam", false);
    const uint32_t n = p.getUInt("photo_n", 0) + 1;
    p.putUInt("photo_n", n);
    p.end();
    char buf[24];
    snprintf(buf, sizeof buf, "sansdate-%05lu", static_cast<unsigned long>(n));
    base = buf;
  }
  String name = base + ".jpg";
  for (int i = 2; SD_MMC.exists(String(kDir) + "/" + name) && i < 100; i++) {
    name = base + "-" + String(i) + ".jpg";
  }
  return name;
}

SaveResult takeAndSave() {
  SaveResult r;
  if (!gAvailable) {
    r.error = "microSD indisponible";
    return r;
  }
  if (!cam::ready()) {
    r.error = "caméra indisponible";
    return r;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    r.error = "capture impossible";
    return r;
  }
  r.name = nextName();
  const String path = String(kDir) + "/" + r.name;
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    esp_camera_fb_return(fb);
    r.error = "impossible de créer le fichier";
    return r;
  }
  const size_t written = f.write(fb->buf, fb->len);
  f.close();
  r.size = fb->len;
  esp_camera_fb_return(fb);
  if (written != r.size) {
    SD_MMC.remove(path);
    r.error = "écriture incomplète (carte pleine ?)";
    return r;
  }
  r.ok = true;
  gSavedThisBoot++;
  gLastName = r.name;
  Serial.printf("[photos] %s enregistrée (%s)\n", r.name.c_str(), bf::humanBytes(r.size).c_str());
  return r;
}

void addStatus(bf::JsonWriter &j) {
  j.beginObject("photos");
  j.add("stockage", gAvailable ? "microSD" : "indisponible");
  j.add("dossier", kDir);
  j.add("enregistrees_depuis_demarrage", gSavedThisBoot);
  if (gLastName.length()) j.add("derniere", gLastName);
  if (gAvailable) {
    j.add("espace_libre", bf::humanBytes(SD_MMC.totalBytes() - SD_MMC.usedBytes()));
  }
  j.endObject();
}

// ---------------------------------------------------------------------------
// Web

struct Entry {
  String name;
  size_t size;
};

static void sendJson(int code, const String &body) {
  gServer->sendHeader("Cache-Control", "no-store");
  gServer->sendHeader("Access-Control-Allow-Origin", "*");
  gServer->send(code, "application/json", body);
}

static void handleTake() {
  const SaveResult r = takeAndSave();
  bf::JsonWriter j;
  j.beginObject();
  j.add("ok", r.ok);
  if (r.ok) {
    j.add("nom", r.name);
    j.add("taille", bf::humanBytes(r.size));
    j.add("url", "/photo?nom=" + r.name);
  } else {
    j.add("erreur", r.error);
  }
  j.endObject();
  sendJson(r.ok ? 200 : 503, j.str());
}

static void handleList() {
  bf::JsonWriter j;
  j.beginObject();
  j.add("disponible", gAvailable);
  if (gAvailable) {
    std::vector<Entry> entries;
    File dir = SD_MMC.open(kDir);
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory()) {
          String name = f.name();
          const int slash = name.lastIndexOf('/');  // selon la version, name() peut contenir le chemin
          if (slash >= 0) name = name.substring(slash + 1);
          if (validName(name)) {
            Entry e;
            e.name = name;
            e.size = f.size();
            entries.push_back(e);
          }
        }
        f.close();
      }
      dir.close();
    }
    // Les noms horodatés se trient chronologiquement : les plus récentes d'abord.
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return b.name < a.name; });
    j.add("nombre", entries.size());
    j.add("espace_libre", bf::humanBytes(SD_MMC.totalBytes() - SD_MMC.usedBytes()));
    j.beginArray("photos");
    for (size_t i = 0; i < entries.size() && i < kMaxListed; i++) {
      j.beginObject();
      j.add("nom", entries[i].name);
      j.add("taille", bf::humanBytes(entries[i].size));
      j.endObject();
    }
    j.endArray();
  }
  j.endObject();
  sendJson(200, j.str());
}

static void handleGet() {
  const String name = gServer->arg("nom");
  if (!gAvailable || !validName(name)) {
    gServer->send(404, "text/plain; charset=utf-8", "Photo introuvable");
    return;
  }
  File f = SD_MMC.open(String(kDir) + "/" + name, FILE_READ);
  if (!f) {
    gServer->send(404, "text/plain; charset=utf-8", "Photo introuvable");
    return;
  }
  gServer->sendHeader("Cache-Control", "max-age=86400");
  if (gServer->hasArg("telecharger")) {
    gServer->sendHeader("Content-Disposition", "attachment; filename=" + name);
  }
  gServer->streamFile(f, "image/jpeg");
  f.close();
}

static void handleDelete() {
  const String name = gServer->arg("nom");
  bf::JsonWriter j;
  j.beginObject();
  if (!gAvailable || !validName(name) || !SD_MMC.remove(String(kDir) + "/" + name)) {
    j.add("ok", false);
    j.add("erreur", "suppression impossible");
    j.endObject();
    sendJson(400, j.str());
    return;
  }
  Serial.printf("[photos] %s supprimée\n", name.c_str());
  j.add("ok", true);
  j.endObject();
  sendJson(200, j.str());
}

static const char kGalleryHtml[] PROGMEM = R"HTML(
<style>
[hidden]{display:none!important}
.galerie{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}
.galerie figure{margin:0;background:#fff;border-radius:6px;padding:6px;box-shadow:0 1px 2px #0002}
.galerie img{width:100%;aspect-ratio:4/3;object-fit:cover;background:#ddd}
.galerie figcaption{font-size:.78em;color:#556;margin-top:4px}
.galerie a,.galerie button{font-size:.8em;padding:4px 8px;margin-right:4px}
</style>
<section><h2>Photos sur la microSD</h2>
<p><button id="prendre">Prendre et enregistrer une photo</button> <span id="info"></span></p>
<div class="galerie" id="g"></div>
</section>
<script>
(function(){
var g=document.getElementById('g'),info=document.getElementById('info');
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function charger(){
  fetch('/api/photos').then(function(r){return r.json();}).then(function(d){
    if(!d.disponible){info.textContent='microSD indisponible.';g.innerHTML='';return;}
    info.textContent=d.nombre+' photo(s), '+d.espace_libre+' libres';
    g.innerHTML=d.photos.map(function(p){var u='/photo?nom='+encodeURIComponent(p.nom);
      return '<figure><a href="'+u+'" target="_blank"><img loading="lazy" src="'+u+'" alt=""></a>'+
      '<figcaption>'+esc(p.nom)+' · '+esc(p.taille)+'<br><a href="'+u+'&telecharger=1">Télécharger</a>'+
      '<button class="second" data-nom="'+esc(p.nom)+'">Supprimer</button></figcaption></figure>';}).join('');
  }).catch(function(e){info.textContent='Erreur : '+e;});
}
g.addEventListener('click',function(ev){
  var n=ev.target.getAttribute('data-nom');if(!n)return;
  if(!confirm('Supprimer '+n+' ?'))return;
  fetch('/api/photos/supprimer',{method:'POST',body:new URLSearchParams({nom:n})}).then(charger);
});
document.getElementById('prendre').onclick=function(){
  info.textContent='Capture…';
  fetch('/api/photo',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    if(!d.ok){info.textContent='Erreur : '+d.erreur;return;}
    charger();
  }).catch(function(e){info.textContent='Erreur : '+e;});
};
charger();
})();
</script>
)HTML";

static void handleGallery() {
  String page = bf::pageHeader(String(bf::moduleLabel()) + " — photos");
  page += FPSTR(kGalleryHtml);
  page += bf::pageFooter();
  gServer->send(200, "text/html; charset=utf-8", page);
}

void registerRoutes(WebServer &server) {
  gServer = &server;
  server.on("/photos", HTTP_GET, handleGallery);
  server.on("/api/photo", HTTP_POST, handleTake);
  server.on("/api/photos", HTTP_GET, handleList);
  server.on("/photo", HTTP_GET, handleGet);
  server.on("/api/photos/supprimer", HTTP_POST, handleDelete);
}

}  // namespace photos
