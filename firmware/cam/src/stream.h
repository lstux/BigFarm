#pragma once

#include <Arduino.h>
#include <BigFarmCore.h>

// Serveur de flux MJPEG sur le port 81 (http://<module>:81/stream).
// Chaque client est servi par sa propre tâche ; jusqu'à kMaxClients en même temps
// (par exemple la centrale et un navigateur).
namespace stream {

static const uint16_t kPort = 81;
static const uint8_t kMaxClients = 3;

void begin();
uint8_t clientCount();
void addStatus(bf::JsonWriter &json);

}  // namespace stream
