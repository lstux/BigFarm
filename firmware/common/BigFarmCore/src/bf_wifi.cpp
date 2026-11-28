#include "bf_wifi.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "bf_system.h"
#include "bf_web.h"

namespace bf {

// Mot de passe du point d'accès de configuration (8 caractères minimum en WPA2).
static const char kApPassword[] = "bigfarm-setup";
static const char kPrefsNamespace[] = "bf-wifi";
static const uint32_t kConnectTimeoutMs = 20000;
static const uint32_t kRetryEveryMs = 30000;
static const uint32_t kReopenApAfterMs = 120000;

void WifiSetup::begin(WebServer &server, const String &moduleId) {
  server_ = &server;
  id_ = moduleId;
  // "bf-cam-a1b2c3" -> "BigFarm-cam-a1b2c3"
  apSsid_ = String("BigFarm-") + (id_.startsWith("bf-") ? id_.substring(3) : id_);

  host_ = hostName();

  WiFi.persistent(false);  // les identifiants sont gérés ici, pas par le pilote
  WiFi.setHostname(host_.c_str());

  bool ok = false;
  if (loadCredentials()) {
    WiFi.mode(WIFI_STA);
    ok = connectStation(kConnectTimeoutMs);
  } else {
    Serial.println("[wifi] aucun réseau enregistré");
  }
  if (!ok) startAp();

  if (MDNS.begin(host_.c_str())) {
    MDNS.addService("http", "tcp", 80);
    // Service BigFarm : permet à la centrale de découvrir les modules.
    MDNS.addService("bigfarm", "tcp", 80);
    MDNS.addServiceTxt("bigfarm", "tcp", "type", moduleType());
    MDNS.addServiceTxt("bigfarm", "tcp", "id", id_.c_str());
    MDNS.addServiceTxt("bigfarm", "tcp", "fw", firmwareVersion());
    Serial.printf("[wifi] mDNS : http://%s.local/\n", host_.c_str());
  }

  server.on("/wifi", HTTP_GET, [this]() { handlePage(); });
  server.on("/wifi", HTTP_POST, [this]() { handleSave(); });
  server.on("/wifi/oublier", HTTP_POST, [this]() { handleForget(); });
  server.on("/nom", HTTP_POST, [this]() { handleRename(); });
  server.onNotFound([this]() { handleNotFound(); });
}

bool WifiSetup::loadCredentials() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  ssid_ = prefs.getString("ssid", "");
  pass_ = prefs.getString("pass", "");
  prefs.end();
  return ssid_.length() > 0;
}

bool WifiSetup::connectStation(uint32_t timeoutMs) {
  Serial.printf("[wifi] connexion à « %s »", ssid_.c_str());
  WiFi.begin(ssid_.c_str(), pass_.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    lastConnected_ = millis();
    Serial.printf("[wifi] connecté : %s (RSSI %d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.println("[wifi] échec de connexion");
  return false;
}

void WifiSetup::startAp() {
  // Station toujours active à côté du point d'accès : le scan des réseaux en a besoin.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid_.c_str(), kApPassword);
  delay(100);
  dns_.start(53, "*", WiFi.softAPIP());
  apActive_ = true;
  Serial.printf("[wifi] point d'accès « %s » (mot de passe « %s »), configuration sur http://%s/wifi\n",
                apSsid_.c_str(), kApPassword, WiFi.softAPIP().toString().c_str());
}

void WifiSetup::stopAp() {
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive_ = false;
  Serial.println("[wifi] réseau retrouvé, point d'accès fermé");
}

bool WifiSetup::connected() const { return WiFi.status() == WL_CONNECTED; }

void WifiSetup::loop() {
  if (apActive_) dns_.processNextRequest();

  if (restartAt_ != 0 && static_cast<int32_t>(millis() - restartAt_) >= 0) {
    Serial.println("[wifi] redémarrage");
    delay(100);
    ESP.restart();
  }

  if (ssid_.length() == 0) return;  // pas de réseau connu : on reste en point d'accès

  if (connected()) {
    lastConnected_ = millis();
    if (apActive_ && WiFi.softAPgetStationNum() == 0) stopAp();
    return;
  }

  // Station déconnectée.
  const bool apIdle = !apActive_ || WiFi.softAPgetStationNum() == 0;
  if (millis() - lastRetry_ > kRetryEveryMs && apIdle) {
    // Une tentative change le canal radio : on évite de couper un client du point d'accès.
    lastRetry_ = millis();
    WiFi.begin(ssid_.c_str(), pass_.c_str());
  }
  if (!apActive_ && millis() - lastConnected_ > kReopenApAfterMs) startAp();
}

void WifiSetup::saveCredentials(const String &ssid, const String &pass) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  Serial.printf("[wifi] réseau « %s » enregistré\n", ssid.c_str());
  restartAt_ = millis() + 1500;
}

void WifiSetup::forget() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.clear();
  prefs.end();
  Serial.println("[wifi] réseau oublié");
  restartAt_ = millis() + 1500;
}

void WifiSetup::addStatus(JsonWriter &json) const {
  json.beginObject("wifi");
  const wifi_mode_t mode = WiFi.getMode();
  json.add("mode", mode == WIFI_AP_STA ? "station + point d'accès"
                   : mode == WIFI_AP   ? "point d'accès"
                   : mode == WIFI_STA  ? "station"
                                       : "arrêté");
  json.add("nom_reseau", host_ + ".local");
  json.add("mac", WiFi.macAddress());
  json.add("reseau_enregistre", ssid_.length() ? ssid_.c_str() : nullptr);
  json.add("connecte", connected());
  if (connected()) {
    json.add("ip", WiFi.localIP().toString());
    json.add("rssi_dbm", WiFi.RSSI());
    json.add("canal", WiFi.channel());
  }
  if (apActive_) {
    json.add("point_acces", apSsid_);
    json.add("point_acces_ip", WiFi.softAPIP().toString());
    json.add("clients_point_acces", WiFi.softAPgetStationNum());
  }
  json.endObject();
}

static String signalBars(int32_t rssi) {
  if (rssi >= -55) return "▂▄▆█";
  if (rssi >= -67) return "▂▄▆";
  if (rssi >= -78) return "▂▄";
  return "▂";
}

void WifiSetup::handlePage() {
  // Le scan prend 2 à 3 secondes.
  int16_t n = WiFi.scanNetworks(false, false);
  if (n < 0) {
    // Un scan précédent a pu échouer : on nettoie et on réessaie une fois.
    WiFi.scanDelete();
    delay(100);
    n = WiFi.scanNetworks(false, false);
  }
  Serial.printf("[wifi] scan : %d réseau(x)\n", n);

  String page = pageHeader("Wi-Fi — " + id_);
  page += F("<style>.reseaux{display:flex;flex-direction:column;gap:6px;margin:6px 0 4px}"
            ".reseaux button{display:flex;justify-content:space-between;background:#eef3f1;color:#1c2421;"
            "border:1px solid #cdd8d4;text-align:left}.reseaux button.choisi{border-color:#1fbf8f;background:#dff6ee}"
            ".sig{color:#1fbf8f;letter-spacing:-1px;white-space:nowrap;margin-left:12px}</style>");
  page += F("<section><h2>État</h2><p>");
  if (connected()) {
    page += "Connecté à « " + htmlEscape(WiFi.SSID()) + " », adresse " + WiFi.localIP().toString() + ".";
  } else {
    page += F("Non connecté à un réseau.");
  }
  page += F("</p></section><section><h2>Réseaux détectés</h2>");
  if (n < 0) {
    page += F("<p>La recherche des réseaux a échoué. <a href=\"/wifi\">Réessayer</a> ou saisir le nom à la main.</p>");
  } else if (n == 0) {
    page += F("<p>Aucun réseau détecté. <a href=\"/wifi\">Réessayer</a> ou saisir le nom à la main.</p>");
  } else {
    page += F("<p>Toucher un réseau pour le choisir, puis saisir son mot de passe.</p><div class=\"reseaux\">");
    int shown = 0;
    for (int16_t i = 0; i < n && shown < 20; i++) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;  // réseau masqué
      bool duplicate = false;  // même nom sur plusieurs bornes : on garde le meilleur signal (déjà en premier)
      for (int16_t k = 0; k < i; k++) {
        if (WiFi.SSID(k) == ssid) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;
      const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
      page += "<button type=\"button\" data-ssid=\"" + htmlEscape(ssid) + "\"><span>" + htmlEscape(ssid) +
              (open ? " (ouvert)" : "") + "</span><span class=\"sig\">" + signalBars(WiFi.RSSI(i)) + " " +
              String(WiFi.RSSI(i)) + " dBm</span></button>";
      shown++;
    }
    page += F("</div><p><a href=\"/wifi\">Actualiser la liste</a></p>");
  }
  page += F("</section><section><h2>Se connecter</h2>"
            "<form method=\"post\" action=\"/wifi\">"
            "<label for=\"ssid\">Nom du réseau</label>"
            "<input id=\"ssid\" name=\"ssid\" required autocomplete=\"off\" autocapitalize=\"off\" value=\"");
  page += htmlEscape(ssid_);
  page += F("\"><label for=\"pass\">Mot de passe</label>"
            "<input id=\"pass\" name=\"pass\" type=\"password\" autocomplete=\"off\">"
            "<p><label style=\"display:inline\"><input type=\"checkbox\" "
            "onchange=\"document.getElementById('pass').type=this.checked?'text':'password'\"> Afficher</label></p>"
            "<p><button type=\"submit\">Enregistrer et redémarrer</button></p></form></section>"
            "<section><h2>Nom sur le réseau</h2>"
            "<form method=\"post\" action=\"/nom\">"
            "<label for=\"nom\">Le module sera joignable sur http://&lt;nom&gt;.local/ "
            "(minuscules, chiffres, tirets ; vide = nom par défaut)</label>"
            "<input id=\"nom\" name=\"nom\" maxlength=\"32\" autocapitalize=\"off\" "
            "pattern=\"[a-z0-9]([a-z0-9-]*[a-z0-9])?\" value=\"");
  page += htmlEscape(host_);
  page += F("\"><p><button type=\"submit\">Renommer et redémarrer</button></p></form></section>"
            "<section><h2>Oublier le réseau</h2>"
            "<form method=\"post\" action=\"/wifi/oublier\" "
            "onsubmit=\"return confirm('Oublier le réseau et redémarrer en point d\\'accès ?')\">"
            "<button class=\"second\" type=\"submit\">Oublier et redémarrer</button></form></section>"
            "<script>document.querySelectorAll('.reseaux button').forEach(function(b){b.onclick=function(){"
            "document.querySelectorAll('.reseaux button').forEach(function(x){x.className='';});b.className='choisi';"
            "document.getElementById('ssid').value=b.getAttribute('data-ssid');"
            "var p=document.getElementById('pass');p.value='';p.focus();};});</script>");
  page += pageFooter();
  WiFi.scanDelete();
  server_->send(200, "text/html; charset=utf-8", page);
}

void WifiSetup::handleSave() {
  const String ssid = server_->arg("ssid");
  if (ssid.length() == 0 || ssid.length() > 32) {
    server_->send(400, "text/plain; charset=utf-8", "Nom de réseau invalide");
    return;
  }
  const String pass = server_->arg("pass");
  String page = pageHeader("Wi-Fi — " + id_);
  page += "<section><p>Réseau « " + htmlEscape(ssid) +
          " » enregistré. Le module redémarre et s'y connecte. Il sera joignable sur <b>http://" + id_ +
          ".local/</b> (ou via l'adresse attribuée par la box).</p></section>";
  page += pageFooter();
  server_->send(200, "text/html; charset=utf-8", page);
  saveCredentials(ssid, pass);
}

void WifiSetup::handleForget() {
  server_->send(200, "text/plain; charset=utf-8", "Réseau oublié, redémarrage en point d'accès.");
  forget();
}

bool WifiSetup::rename(const String &name) {
  if (!setHostName(name)) return false;
  Serial.printf("[wifi] nouveau nom réseau : %s.local\n", hostName().c_str());
  restartAt_ = millis() + 1500;
  return true;
}

void WifiSetup::handleRename() {
  const String name = server_->arg("nom");
  String page = pageHeader("Nom du module");
  if (!rename(name)) {
    page += F("<section><p>Nom invalide : lettres minuscules, chiffres et tirets seulement, 32 caractères au plus, "
              "sans tiret au début ni à la fin.</p><p><a href=\"/wifi\">Retour</a></p></section>");
    page += pageFooter();
    server_->send(400, "text/html; charset=utf-8", page);
    return;
  }
  const String n = hostName();
  page += "<section><p>Nouveau nom : <b>" + htmlEscape(n) + ".local</b>. Le module redémarre ; il sera joignable sur "
          "<a href=\"http://" + htmlEscape(n) + ".local/\">http://" + htmlEscape(n) + ".local/</a> dans quelques secondes.</p></section>";
  page += pageFooter();
  server_->send(200, "text/html; charset=utf-8", page);
}

void WifiSetup::handleNotFound() {
  if (apActive_) {
    // Portail captif : toute adresse inconnue renvoie vers la configuration.
    server_->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/wifi", true);
    server_->send(302, "text/plain", "");
    return;
  }
  server_->send(404, "text/plain; charset=utf-8", "Introuvable");
}

}  // namespace bf
