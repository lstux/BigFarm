#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

#include "bf_json.h"

namespace bf {

// Gestion du Wi-Fi d'un module BigFarm :
// - identifiants enregistrés en NVS (survivent aux mises à jour) ;
// - au démarrage, connexion en mode station ;
// - en l'absence d'identifiants, ou si la connexion échoue, point d'accès de
//   configuration "BigFarm-<identifiant>" avec portail captif sur /wifi ;
// - si le réseau revient alors que le point d'accès est ouvert et que personne
//   n'y est connecté, retour au seul mode station ;
// - si le réseau est perdu longtemps, réouverture du point d'accès.
class WifiSetup {
 public:
  // Enregistre les routes /wifi et lance la connexion. Bloque au plus ~20 s.
  void begin(WebServer &server, const String &moduleId);
  void loop();

  bool connected() const;
  bool apActive() const { return apActive_; }

  // Enregistre de nouveaux identifiants puis redémarre (sous ~1,5 s).
  void saveCredentials(const String &ssid, const String &pass);
  // Efface les identifiants puis redémarre.
  void forget();

  // Change le nom réseau (<nom>.local) puis redémarre. false si le nom est invalide.
  bool rename(const String &name);
  // Réseau enregistré (vide si aucun).
  const String &savedSsid() const { return ssid_; }
  // Nom réseau actuel, sans « .local ».
  const String &host() const { return host_; }

  void addStatus(JsonWriter &json) const;

 private:
  bool loadCredentials();
  bool connectStation(uint32_t timeoutMs);
  void startAp();
  void stopAp();
  void handlePage();
  void handleSave();
  void handleForget();
  void handleRename();
  void handleNotFound();

  WebServer *server_ = nullptr;
  DNSServer dns_;
  String id_;
  String host_;
  String ssid_;
  String pass_;
  String apSsid_;
  bool apActive_ = false;
  uint32_t lastRetry_ = 0;
  uint32_t lastConnected_ = 0;
  uint32_t restartAt_ = 0;
};

}  // namespace bf
