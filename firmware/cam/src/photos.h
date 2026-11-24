#pragma once

#include <Arduino.h>
#include <BigFarmCore.h>
#include <WebServer.h>

// Photos enregistrées sur la microSD, dans /photos.
// Nom : AAAAMMJJ-HHMMSS.jpg quand l'heure est connue, sinon sansdate-<n>.jpg.
namespace photos {

struct SaveResult {
  bool ok = false;
  String name;
  size_t size = 0;
  String error;
};

// À appeler après le montage de la microSD.
void begin(bool sdMounted);
bool available();

SaveResult takeAndSave();

// Routes : POST /api/photo, GET /api/photos, GET /photo?nom=…,
// POST /api/photos/supprimer, page /photos.
void registerRoutes(WebServer &server);

void addStatus(bf::JsonWriter &json);

}  // namespace photos
