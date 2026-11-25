#pragma once

#include <lvgl.h>

// Vue caméra de la centrale : page Caméra, aperçu sur l'Accueil, fiche dans Modules.
namespace camview {

void buildPage(lv_obj_t *page);
void buildHome(lv_obj_t *card);
void buildModuleCard(lv_obj_t *list);

// À appeler à chaque tour de loop() : demande le flux quand une vue est visible,
// affiche les nouvelles images, met à jour les textes.
void loop();

}  // namespace camview
