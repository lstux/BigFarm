#pragma once

#include <Arduino.h>
#include <BigFarmCore.h>
#include <WebServer.h>

// Timelapse : une photo toutes les N secondes, dans /timelapse/<série>/ sur la microSD.
// - réglages propres (résolution, qualité, LED) : le flux en direct garde les siens ;
// - plage horaire facultative (ex. 07:00 → 19:00, ou 22:00 → 06:00 sur la nuit) ;
// - reprise automatique après une coupure (même série) ;
// - arrêt automatique si la carte est presque pleine.
namespace timelapse {

// À appeler après le montage de la microSD et l'initialisation de la caméra.
void begin(bool sdMounted);
// À appeler dans loop() : déclenche les captures à l'heure.
void loop();

// Routes : page /timelapse, API /api/timelapse…, images /tl?serie=…&nom=…
void registerRoutes(WebServer &server);

void addStatus(bf::JsonWriter &json);

}  // namespace timelapse
