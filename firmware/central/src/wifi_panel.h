#pragma once

#include <Arduino.h>

// Choix du réseau Wi-Fi depuis l'écran : liste des réseaux détectés,
// mot de passe au clavier virtuel, puis enregistrement et redémarrage.
namespace wifipanel {

typedef void (*SaveFn)(const String &ssid, const String &pass);

// La fonction d'enregistrement est fournie par main.cpp (WifiSetup::saveCredentials).
void setSaveHandler(SaveFn fn);
void open();
void close();

}  // namespace wifipanel
