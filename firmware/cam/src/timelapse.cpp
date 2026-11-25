#include "timelapse.h"

#include <Preferences.h>
#include <SD_MMC.h>

#include <algorithm>
#include <vector>

#include "camera.h"

namespace timelapse {

static const char kRoot[] = "/timelapse";
static const char kPrefs[] = "bf-tl";
static const uint32_t kMinInterval = 10;
static const uint32_t kMaxInterval = 86400;
static const uint64_t kMinFreeBytes = 20ULL * 1024 * 1024;  // arrêt sous 20 Mo libres
static const size_t kMaxListed = 5000;

struct Config {
  bool active = false;
  uint32_t intervalS = 60;
  int16_t startMin = -1;  // plage horaire en minutes depuis minuit, -1 = toute la journée
  int16_t endMin = -1;
  uint8_t frameSize = FRAMESIZE_SVGA;
  uint8_t quality = 10;
  uint8_t led = 0;
  String series;
};

static Config gCfg;
static bool gSd = false;
static WebServer *gServer = nullptr;

// État
static uint32_t gCount = 0;  // images dans la série en cours
static uint32_t gNextDue = 0;
static String gState = "arrêté";
static String gLastName;
static String gLastTime;
static size_t gLastBytes = 0;
static uint32_t gErrors = 0;
static String gLastError;

// ---------------------------------------------------------------------------
// Outils

static bool validSeries(const String &n) {
  if (n.length() < 1 || n.length() > 32) return false;
  for (size_t i = 0; i < n.length(); i++) {
    const char c = n[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
  }
  return true;
}

static bool validImage(const String &n) {
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

static String baseName(const char *path) {
  String n = path;
  const int slash = n.lastIndexOf('/');
  return slash >= 0 ? n.substring(slash + 1) : n;
}

static String seriesDir(const String &series) { return String(kRoot) + "/" + series; }

static String minutesText(int16_t m) {
  if (m < 0) return String();
  char buf[8];
  snprintf(buf, sizeof buf, "%02d:%02d", m / 60, m % 60);
  return String(buf);
}

// "HH:MM" -> minutes ; "" -> -1 ; erreur -> -2
static int16_t parseMinutes(const String &s) {
  String t = s;
  t.trim();
  if (t.length() == 0) return -1;
  const int colon = t.indexOf(':');
  if (colon < 1) return -2;
  const long h = t.substring(0, colon).toInt();
  const long m = t.substring(colon + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -2;
  return static_cast<int16_t>(h * 60 + m);
}

static bool windowSet() { return gCfg.startMin >= 0 && gCfg.endMin >= 0 && gCfg.startMin != gCfg.endMin; }

static bool inWindow(int nowMin) {
  if (!windowSet()) return true;
  if (gCfg.startMin < gCfg.endMin) return nowMin >= gCfg.startMin && nowMin < gCfg.endMin;
  return nowMin >= gCfg.startMin || nowMin < gCfg.endMin;  // plage sur la nuit
}

static void save() {
  Preferences p;
  p.begin(kPrefs, false);
  p.putBool("actif", gCfg.active);
  p.putUInt("interv", gCfg.intervalS);
  p.putShort("debut", gCfg.startMin);
  p.putShort("fin", gCfg.endMin);
  p.putUChar("taille", gCfg.frameSize);
  p.putUChar("qualite", gCfg.quality);
  p.putUChar("led", gCfg.led);
  p.putString("serie", gCfg.series);
  p.end();
}

static void load() {
  Preferences p;
  p.begin(kPrefs, true);
  Config d;
  gCfg.active = p.getBool("actif", d.active);
  gCfg.intervalS = p.getUInt("interv", d.intervalS);
  gCfg.startMin = p.getShort("debut", d.startMin);
  gCfg.endMin = p.getShort("fin", d.endMin);
  gCfg.frameSize = p.getUChar("taille", d.frameSize);
  gCfg.quality = p.getUChar("qualite", d.quality);
  gCfg.led = p.getUChar("led", d.led);
  gCfg.series = p.getString("serie", "");
  p.end();
  if (gCfg.intervalS < kMinInterval || gCfg.intervalS > kMaxInterval) gCfg.intervalS = d.intervalS;
  if (gCfg.frameSize > cam::maxFrameSize()) gCfg.frameSize = cam::maxFrameSize();
}

static uint32_t countImages(const String &series, uint64_t *bytes) {
  uint32_t n = 0;
  if (bytes) *bytes = 0;
  File dir = SD_MMC.open(seriesDir(series));
  if (!dir || !dir.isDirectory()) return 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory() && validImage(baseName(f.name()))) {
      n++;
      if (bytes) *bytes += f.size();
    }
    f.close();
  }
  dir.close();
  return n;
}

static uint64_t freeBytes() { return SD_MMC.totalBytes() - SD_MMC.usedBytes(); }

// ---------------------------------------------------------------------------
// Démarrage, arrêt

static bool startSeries(String &error) {
  if (!gSd) {
    error = "microSD indisponible";
    return false;
  }
  String name = bf::fileTimestamp();
  if (name.length()) {
    name = name.substring(0, 13);  // AAAAMMJJ-HHMM
  } else {
    Preferences p;
    p.begin(kPrefs, false);
    const uint32_t n = p.getUInt("serie_n", 0) + 1;
    p.putUInt("serie_n", n);
    p.end();
    char buf[16];
    snprintf(buf, sizeof buf, "serie-%04lu", static_cast<unsigned long>(n));
    name = buf;
  }
  String unique = name;
  for (int i = 2; SD_MMC.exists(seriesDir(unique)) && i < 100; i++) unique = name + "-" + String(i);
  if ((!SD_MMC.exists(kRoot) && !SD_MMC.mkdir(kRoot)) || !SD_MMC.mkdir(seriesDir(unique))) {
    error = "impossible de créer le dossier de la série";
    return false;
  }
  gCfg.series = unique;
  gCfg.active = true;
  gCount = 0;
  gErrors = 0;
  gLastError = "";
  gLastName = "";
  gNextDue = millis();
  save();
  Serial.printf("[timelapse] nouvelle série %s, une image toutes les %lu s\n", unique.c_str(),
                static_cast<unsigned long>(gCfg.intervalS));
  return true;
}

static bool resumeSeries(String &error) {
  if (!gSd) {
    error = "microSD indisponible";
    return false;
  }
  if (!validSeries(gCfg.series) || !SD_MMC.exists(seriesDir(gCfg.series))) return startSeries(error);
  gCfg.active = true;
  gCount = countImages(gCfg.series, nullptr);
  gNextDue = millis();
  save();
  Serial.printf("[timelapse] reprise de la série %s (%lu images)\n", gCfg.series.c_str(),
                static_cast<unsigned long>(gCount));
  return true;
}

static void stop(const char *reason) {
  gCfg.active = false;
  gState = reason;
  save();
  Serial.printf("[timelapse] arrêt : %s\n", reason);
}

void begin(bool sdMounted) {
  gSd = sdMounted;
  load();
  if (gCfg.active) {
    if (!gSd) {
      gState = "microSD indisponible";
      return;
    }
    gCount = countImages(gCfg.series, nullptr);
    gNextDue = millis() + 5000;  // laisse le temps au Wi-Fi et à l'heure NTP
    Serial.printf("[timelapse] reprise après démarrage : série %s, %lu images\n", gCfg.series.c_str(),
                  static_cast<unsigned long>(gCount));
  }
}

// ---------------------------------------------------------------------------
// Capture

static void capture() {
  String name = bf::fileTimestamp();
  if (name.length() == 0) {
    char buf[20];
    snprintf(buf, sizeof buf, "img-%06lu", static_cast<unsigned long>(gCount + 1));
    name = buf;
  }
  name += ".jpg";
  const String path = seriesDir(gCfg.series) + "/" + name;
  String error;
  const uint32_t t0 = millis();
  const size_t bytes = cam::captureToFile(SD_MMC, path, static_cast<framesize_t>(gCfg.frameSize), gCfg.quality,
                                          gCfg.led, error);
  if (bytes == 0) {
    gErrors++;
    gLastError = error;
    Serial.printf("[timelapse] échec de capture : %s\n", error.c_str());
    return;
  }
  gCount++;
  gLastName = name;
  gLastBytes = bytes;
  gLastTime = bf::nowText();
  Serial.printf("[timelapse] %s/%s (%s, %lu ms)\n", gCfg.series.c_str(), name.c_str(),
                bf::humanBytes(bytes).c_str(), static_cast<unsigned long>(millis() - t0));
}

void loop() {
  if (!gCfg.active) return;
  if (!gSd) {
    gState = "microSD indisponible";
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - gNextDue) < 0) return;

  if (windowSet()) {
    if (!bf::timeValid()) {
      gState = "en attente de l'heure (NTP)";
      gNextDue = now + 10000;
      return;
    }
    const time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    if (!inWindow(tm.tm_hour * 60 + tm.tm_min)) {
      gState = "hors plage horaire";
      gNextDue = now + 20000;
      return;
    }
  }
  if (freeBytes() < kMinFreeBytes) {
    stop("arrêté : carte presque pleine");
    return;
  }

  gState = "en cours";
  capture();
  // Cadence régulière : on vise l'échéance suivante, sans cumuler de retard.
  gNextDue += gCfg.intervalS * 1000UL;
  if (static_cast<int32_t>(millis() - gNextDue) > 0) gNextDue = millis() + gCfg.intervalS * 1000UL;
}

// ---------------------------------------------------------------------------
// JSON

static void addConfig(bf::JsonWriter &j) {
  j.beginObject("reglages");
  j.add("intervalle_s", gCfg.intervalS);
  j.add("debut", minutesText(gCfg.startMin));
  j.add("fin", minutesText(gCfg.endMin));
  j.add("resolution", gCfg.frameSize);
  j.add("qualite", gCfg.quality);
  j.add("led", gCfg.led);
  j.endObject();
}

static void addState(bf::JsonWriter &j) {
  j.beginObject("etat");
  j.add("actif", gCfg.active);
  j.add("etat", gCfg.active ? gState : String(gState == "en cours" ? "arrêté" : gState));
  j.add("serie", gCfg.series);
  j.add("images", gCount);
  if (gLastName.length()) {
    j.add("derniere_image", gLastName);
    j.add("derniere_heure", gLastTime);
    j.add("derniere_taille", bf::humanBytes(gLastBytes));
  }
  if (gCfg.active) {
    const int32_t left = static_cast<int32_t>(gNextDue - millis());
    j.add("prochaine_dans_s", left > 0 ? left / 1000 : 0);
  }
  j.add("erreurs", gErrors);
  if (gLastError.length()) j.add("derniere_erreur", gLastError);
  if (gSd) j.add("espace_libre", bf::humanBytes(freeBytes()));
  j.endObject();
}

void addStatus(bf::JsonWriter &j) {
  j.beginObject("timelapse");
  j.add("actif", gCfg.active);
  j.add("etat", gCfg.active ? gState : String("arrêté"));
  j.add("serie", gCfg.series);
  j.add("images", gCount);
  j.add("intervalle_s", gCfg.intervalS);
  j.endObject();
}

// ---------------------------------------------------------------------------
// Web

static void sendJson(int code, const String &body) {
  gServer->sendHeader("Cache-Control", "no-store");
  gServer->sendHeader("Access-Control-Allow-Origin", "*");
  gServer->send(code, "application/json", body);
}

static void sendStatus(int code, const char *error) {
  bf::JsonWriter j;
  j.beginObject();
  if (error != nullptr) j.add("erreur", error);
  addConfig(j);
  addState(j);
  j.endObject();
  sendJson(code, j.str());
}

static void handleGet() { sendStatus(200, nullptr); }

static void handleConfig() {
  Config n = gCfg;
  if (gServer->hasArg("intervalle_s")) {
    const long v = gServer->arg("intervalle_s").toInt();
    if (v < static_cast<long>(kMinInterval) || v > static_cast<long>(kMaxInterval)) {
      sendStatus(400, "intervalle : 10 s à 24 h");
      return;
    }
    n.intervalS = static_cast<uint32_t>(v);
  }
  if (gServer->hasArg("debut") || gServer->hasArg("fin")) {
    const int16_t a = parseMinutes(gServer->arg("debut"));
    const int16_t b = parseMinutes(gServer->arg("fin"));
    if (a == -2 || b == -2 || (a < 0) != (b < 0)) {
      sendStatus(400, "plage horaire : deux heures HH:MM, ou les deux vides");
      return;
    }
    n.startMin = a;
    n.endMin = b;
  }
  if (gServer->hasArg("resolution")) {
    const long v = gServer->arg("resolution").toInt();
    if (v < 0 || v > static_cast<long>(cam::maxFrameSize())) {
      sendStatus(400, "résolution non disponible");
      return;
    }
    n.frameSize = static_cast<uint8_t>(v);
  }
  if (gServer->hasArg("qualite")) {
    const long v = gServer->arg("qualite").toInt();
    if (v < 4 || v > 63) {
      sendStatus(400, "qualité : 4 à 63");
      return;
    }
    n.quality = static_cast<uint8_t>(v);
  }
  if (gServer->hasArg("led")) {
    const long v = gServer->arg("led").toInt();
    if (v < 0 || v > 255) {
      sendStatus(400, "LED : 0 à 255");
      return;
    }
    n.led = static_cast<uint8_t>(v);
  }
  const bool intervalChanged = n.intervalS != gCfg.intervalS;
  gCfg = n;
  save();
  if (gCfg.active && intervalChanged) gNextDue = millis() + gCfg.intervalS * 1000UL;
  sendStatus(200, nullptr);
}

static void handleStart() {
  String error;
  if (!startSeries(error)) {
    sendStatus(503, error.c_str());
    return;
  }
  sendStatus(200, nullptr);
}

static void handleResume() {
  String error;
  if (!resumeSeries(error)) {
    sendStatus(503, error.c_str());
    return;
  }
  sendStatus(200, nullptr);
}

static void handleStop() {
  stop("arrêté");
  sendStatus(200, nullptr);
}

struct SeriesInfo {
  String name;
  uint32_t count;
  uint64_t bytes;
};

static void handleSeries() {
  bf::JsonWriter j;
  j.beginObject();
  j.add("disponible", gSd);
  if (gSd) {
    std::vector<SeriesInfo> list;
    File root = SD_MMC.open(kRoot);
    if (root && root.isDirectory()) {
      for (File f = root.openNextFile(); f && list.size() < 100; f = root.openNextFile()) {
        if (f.isDirectory()) {
          const String name = baseName(f.name());
          if (validSeries(name)) {
            SeriesInfo s;
            s.name = name;
            s.count = 0;
            s.bytes = 0;
            list.push_back(s);
          }
        }
        f.close();
      }
      root.close();
    }
    for (size_t i = 0; i < list.size(); i++) list[i].count = countImages(list[i].name, &list[i].bytes);
    std::sort(list.begin(), list.end(), [](const SeriesInfo &a, const SeriesInfo &b) { return b.name < a.name; });
    j.add("espace_libre", bf::humanBytes(freeBytes()));
    j.beginArray("series");
    for (size_t i = 0; i < list.size(); i++) {
      j.beginObject();
      j.add("nom", list[i].name);
      j.add("images", list[i].count);
      j.add("taille", bf::humanBytes(list[i].bytes));
      j.add("en_cours", gCfg.active && list[i].name == gCfg.series);
      j.endObject();
    }
    j.endArray();
  }
  j.endObject();
  sendJson(200, j.str());
}

static void handleImages() {
  const String series = gServer->arg("serie");
  if (!gSd || !validSeries(series)) {
    sendJson(404, "{\"erreur\":\"série introuvable\"}");
    return;
  }
  std::vector<String> names;
  File dir = SD_MMC.open(seriesDir(series));
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f && names.size() < kMaxListed; f = dir.openNextFile()) {
      if (!f.isDirectory()) {
        const String n = baseName(f.name());
        if (validImage(n)) names.push_back(n);
      }
      f.close();
    }
    dir.close();
  }
  std::sort(names.begin(), names.end());  // ordre chronologique
  bf::JsonWriter j;
  j.beginObject();
  j.add("serie", series);
  j.add("nombre", names.size());
  j.beginArray("images");
  for (size_t i = 0; i < names.size(); i++) j.add(nullptr, names[i]);
  j.endArray();
  j.endObject();
  sendJson(200, j.str());
}

static void handleFile() {
  const String series = gServer->arg("serie");
  const String name = gServer->arg("nom");
  if (!gSd || !validSeries(series) || !validImage(name)) {
    gServer->send(404, "text/plain; charset=utf-8", "Image introuvable");
    return;
  }
  File f = SD_MMC.open(seriesDir(series) + "/" + name, FILE_READ);
  if (!f) {
    gServer->send(404, "text/plain; charset=utf-8", "Image introuvable");
    return;
  }
  gServer->sendHeader("Cache-Control", "max-age=86400");
  gServer->sendHeader("Access-Control-Allow-Origin", "*");
  if (gServer->hasArg("telecharger")) gServer->sendHeader("Content-Disposition", "attachment; filename=" + name);
  gServer->streamFile(f, "image/jpeg");
  f.close();
}

static void handleDelete() {
  const String series = gServer->arg("serie");
  if (!gSd || !validSeries(series)) {
    sendJson(400, "{\"erreur\":\"série invalide\"}");
    return;
  }
  if (gCfg.active && series == gCfg.series) {
    sendJson(409, "{\"erreur\":\"série en cours : arrêter le timelapse d'abord\"}");
    return;
  }
  const String dirPath = seriesDir(series);
  std::vector<String> files;
  File dir = SD_MMC.open(dirPath);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (!f.isDirectory()) files.push_back(baseName(f.name()));
      f.close();
    }
    dir.close();
  }
  for (size_t i = 0; i < files.size(); i++) SD_MMC.remove(dirPath + "/" + files[i]);
  const bool ok = SD_MMC.rmdir(dirPath);
  Serial.printf("[timelapse] série %s supprimée (%u fichiers)\n", series.c_str(), static_cast<unsigned>(files.size()));
  sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"erreur\":\"suppression incomplète\"}");
}

static const char kPageHtml[] PROGMEM = R"HTML(
<style>
[hidden]{display:none!important}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:4px 18px}
.etat b{color:#1fbf8f}
table.series td,table.series th{padding:4px 6px;border-bottom:1px solid #eee}
table.series button{font-size:.8em;padding:4px 8px}
.lecteur{background:#111;border-radius:6px;text-align:center;min-height:120px}
.lecteur img{max-width:100%;display:block;margin:auto}
#msg{min-height:1.2em}.err{color:#c0392b}
</style>
<section><h2>État</h2><div class="etat" id="etat">Chargement…</div>
<p><button id="bnouvelle">Nouvelle série</button> <button id="breprendre" class="second">Reprendre</button>
<button id="barreter" class="second">Arrêter</button></p><p id="msg"></p></section>
<section><h2>Réglages</h2>
<form id="f" onsubmit="return false"><div class="grid">
<label>Une image toutes les (secondes)<br><input type="number" name="intervalle_s" min="10" max="86400" required></label>
<label>Résolution<br><select name="resolution" id="res"></select></label>
<label>Qualité JPEG (petit = meilleure)<br><input type="number" name="qualite" min="4" max="63"></label>
<label>LED blanche pendant la photo (0 = non)<br><input type="number" name="led" min="0" max="255"></label>
<label>Plage horaire : de<br><input type="time" name="debut"></label>
<label>à (vide = toute la journée)<br><input type="time" name="fin"></label>
</div><p><button id="benr">Enregistrer les réglages</button></p></form>
<p style="font-size:.85em;color:#667">Exemples : 60 s pour tester ; 900 s (15 min) ou 3600 s (1 h) en croisière.
Les réglages s'appliquent à la prochaine image, sans changer le flux en direct.</p>
</section>
<section><h2>Séries</h2><p id="place"></p><table class="series" id="series"></table></section>
<section id="vue" hidden><h2 id="vuetitre"></h2>
<div class="lecteur"><img id="img" alt=""></div>
<p><input type="range" id="pos" min="0" value="0" style="width:100%"></p>
<p><button id="blecture">▶ Lecture</button>
<label style="display:inline">Images/s <select id="ips"><option>2</option><option selected>5</option><option>10</option><option>15</option></select></label>
<span id="nom"></span> · <a id="dl" href="#">Télécharger cette image</a></p>
</section>
<script>
(function(){
var f=document.getElementById('f'),msg=document.getElementById('msg');
function err(t){msg.textContent=t;msg.className='err';}
function ok(t){msg.textContent=t;msg.className='';}
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function etat(d){
  var e=d.etat,h='<b>'+esc(e.etat)+'</b>';
  if(e.serie)h+=' · série '+esc(e.serie)+' · '+e.images+' image(s)';
  if(e.prochaine_dans_s!==undefined&&e.actif)h+=' · prochaine dans '+e.prochaine_dans_s+' s';
  if(e.derniere_image)h+='<br>Dernière : '+esc(e.derniere_image)+' ('+esc(e.derniere_taille)+', '+esc(e.derniere_heure)+')';
  if(e.erreurs)h+='<br>Erreurs : '+e.erreurs+(e.derniere_erreur?' ('+esc(e.derniere_erreur)+')':'');
  document.getElementById('etat').innerHTML=h;
}
function remplir(d){var r=d.reglages;for(var k in r){var e=f.elements[k];if(e)e.value=r[k];}}
function api(url,opts){return fetch(url,opts).then(function(r){return r.json().then(function(d){if(!r.ok)throw new Error(d.erreur||('code '+r.status));return d;});});}
function charger(){api('/api/timelapse').then(function(d){etat(d);});}
function action(url,texte){api(url,{method:'POST'}).then(function(d){etat(d);ok(texte);series();}).catch(function(e){err('Erreur : '+e.message);});}
document.getElementById('bnouvelle').onclick=function(){action('/api/timelapse/demarrer','Nouvelle série démarrée.');};
document.getElementById('breprendre').onclick=function(){action('/api/timelapse/reprendre','Timelapse repris.');};
document.getElementById('barreter').onclick=function(){action('/api/timelapse/arreter','Timelapse arrêté.');};
document.getElementById('benr').onclick=function(){
  if(!f.reportValidity())return;
  var p=new URLSearchParams(new FormData(f));
  api('/api/timelapse',{method:'POST',body:p}).then(function(d){remplir(d);etat(d);ok('Réglages enregistrés.');}).catch(function(e){err('Erreur : '+e.message);});
};
function series(){
  api('/api/timelapse/series').then(function(d){
    if(!d.disponible){document.getElementById('place').textContent='microSD indisponible.';return;}
    document.getElementById('place').textContent=d.espace_libre+' libres sur la carte.';
    var h='<tr><th>Série</th><th>Images</th><th>Taille</th><th></th></tr>';
    d.series.forEach(function(s){h+='<tr><td>'+esc(s.nom)+(s.en_cours?' (en cours)':'')+'</td><td>'+s.images+'</td><td>'+esc(s.taille)+
      '</td><td><button data-voir="'+esc(s.nom)+'">Voir</button> '+(s.en_cours?'':'<button class="second" data-suppr="'+esc(s.nom)+'">Supprimer</button>')+'</td></tr>';});
    document.getElementById('series').innerHTML=h;
  });
}
document.getElementById('series').addEventListener('click',function(ev){
  var v=ev.target.getAttribute('data-voir'),s=ev.target.getAttribute('data-suppr');
  if(v)voir(v);
  if(s&&confirm('Supprimer la série '+s+' et toutes ses images ?'))
    api('/api/timelapse/supprimer',{method:'POST',body:new URLSearchParams({serie:s})}).then(series).catch(function(e){err('Erreur : '+e.message);});
});
// Lecteur : enchaîne les images de la série.
var images=[],serie='',idx=0,timer=null,img=document.getElementById('img'),pos=document.getElementById('pos');
function url(i,dl){return '/tl?serie='+encodeURIComponent(serie)+'&nom='+encodeURIComponent(images[i])+(dl?'&telecharger=1':'');}
function montrer(i){if(!images.length)return;idx=i;img.src=url(i);pos.value=i;document.getElementById('nom').textContent=(i+1)+'/'+images.length+' · '+images[i];document.getElementById('dl').href=url(i,true);}
function voir(s){
  stopLecture();serie=s;
  api('/api/timelapse/images?serie='+encodeURIComponent(s)).then(function(d){
    images=d.images;document.getElementById('vue').hidden=false;
    document.getElementById('vuetitre').textContent='Série '+s+' ('+images.length+' images)';
    pos.max=Math.max(0,images.length-1);montrer(images.length?images.length-1:0);
    document.getElementById('vue').scrollIntoView();
  });
}
function stopLecture(){clearTimeout(timer);timer=null;document.getElementById('blecture').textContent='▶ Lecture';}
function suivante(){
  if(idx>=images.length-1){stopLecture();return;}
  var t0=Date.now(),delai=1000/parseInt(document.getElementById('ips').value,10);
  img.onload=function(){img.onload=null;timer=setTimeout(suivante,Math.max(0,delai-(Date.now()-t0)));};
  montrer(idx+1);
}
document.getElementById('blecture').onclick=function(){
  if(timer){stopLecture();return;}
  if(idx>=images.length-1)montrer(0);
  this.textContent='⏸ Pause';timer=setTimeout(suivante,0);
};
pos.oninput=function(){stopLecture();montrer(parseInt(pos.value,10));};
fetch('/api/camera').then(function(r){return r.json();}).then(function(d){
  var sel=document.getElementById('res');
  d.resolutions.forEach(function(r){var o=document.createElement('option');o.value=r.id;o.textContent=r.nom;sel.appendChild(o);});
  return api('/api/timelapse');
}).then(function(d){remplir(d);etat(d);});
series();
setInterval(charger,3000);
})();
</script>
)HTML";

static void handlePage() {
  String page = bf::pageHeader(String(bf::moduleLabel()) + " — timelapse");
  page += FPSTR(kPageHtml);
  page += bf::pageFooter();
  gServer->send(200, "text/html; charset=utf-8", page);
}

void registerRoutes(WebServer &server) {
  gServer = &server;
  server.on("/timelapse", HTTP_GET, handlePage);
  server.on("/api/timelapse", HTTP_GET, handleGet);
  server.on("/api/timelapse", HTTP_POST, handleConfig);
  server.on("/api/timelapse/demarrer", HTTP_POST, handleStart);
  server.on("/api/timelapse/reprendre", HTTP_POST, handleResume);
  server.on("/api/timelapse/arreter", HTTP_POST, handleStop);
  server.on("/api/timelapse/series", HTTP_GET, handleSeries);
  server.on("/api/timelapse/images", HTTP_GET, handleImages);
  server.on("/api/timelapse/supprimer", HTTP_POST, handleDelete);
  server.on("/tl", HTTP_GET, handleFile);
}

}  // namespace timelapse
