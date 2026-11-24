#include "bf_wifi.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

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

  WiFi.persistent(false);  // les identifiants sont gérés ici, pas par le pilote
  WiFi.setHostname(id_.c_str());

  bool ok = false;
  if (loadCredentials()) {
    WiFi.mode(WIFI_STA);
    ok = connectStation(kConnectTimeoutMs);
  } else {
    Serial.println("[wifi] aucun réseau enregistré");
  }
  if (!ok) startAp();

  if (MDNS.begin(id_.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[wifi] mDNS : http://%s.local/\n", id_.c_str());
  }

  server.on("/wifi", HTTP_GET, [this]() { handlePage(); });
  server.on("/wifi", HTTP_POST, [this]() { handleSave(); });
  server.on("/wifi/oublier", HTTP_POST, [this]() { handleForget(); });
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
  WiFi.mode(ssid_.length() ? WIFI_AP_STA : WIFI_AP);
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
  json.add("nom_reseau", WiFi.getHostname());
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

void WifiSetup::handlePage() {
  // Le scan prend 2 à 3 secondes.
  const int16_t n = WiFi.scanNetworks();
  String page = pageHeader("Wi-Fi — " + id_);
  page += F("<section><h2>État</h2><p>");
  if (connected()) {
    page += "Connecté à « " + htmlEscape(WiFi.SSID()) + " », adresse " + WiFi.localIP().toString() + ".";
  } else {
    page += F("Non connecté à un réseau.");
  }
  page += F("</p></section><section><h2>Choisir un réseau</h2>"
            "<form method=\"post\" action=\"/wifi\">"
            "<label for=\"ssid\">Nom du réseau</label>"
            "<input id=\"ssid\" name=\"ssid\" list=\"reseaux\" required autocomplete=\"off\" value=\"");
  page += htmlEscape(ssid_);
  page += F("\"><datalist id=\"reseaux\">");
  for (int16_t i = 0; i < n; i++) {
    page += "<option value=\"" + htmlEscape(WiFi.SSID(i)) + "\">" + String(WiFi.RSSI(i)) + " dBm</option>";
  }
  page += F("</datalist>"
            "<label for=\"pass\">Mot de passe</label>"
            "<input id=\"pass\" name=\"pass\" type=\"password\" autocomplete=\"off\">"
            "<p><button type=\"submit\">Enregistrer et redémarrer</button></p></form></section>"
            "<section><h2>Oublier le réseau</h2>"
            "<form method=\"post\" action=\"/wifi/oublier\" "
            "onsubmit=\"return confirm('Oublier le réseau et redémarrer en point d\\'accès ?')\">"
            "<button class=\"second\" type=\"submit\">Oublier et redémarrer</button></form></section>");
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
