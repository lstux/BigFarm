#pragma once

#include <Arduino.h>
#include <time.h>

#include "bf_json.h"

namespace bf {

// Heure par NTP. Fuseau par défaut : Europe/Paris (heure d'été gérée).
// À appeler une fois ; la synchronisation se fait en tâche de fond dès que le
// réseau est disponible, puis toutes les heures.
void timeBegin(const char *tz = "CET-1CEST,M3.5.0,M10.5.0/3");

// Vrai quand l'heure a été obtenue au moins une fois.
bool timeValid();

// "2026-11-25 07:12:03" (heure locale), "—" si l'heure est inconnue.
String timeText(time_t t);
String nowText();
// "20261125-071203" pour les noms de fichiers ; vide si l'heure est inconnue.
String fileTimestamp();

void addTime(JsonWriter &json);

}  // namespace bf
