#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Interface tactile de la centrale (480x480).
// Barre d'état en haut, contenu au centre, navigation en bas :
// Accueil, Caméra, Mesures, Commandes, Modules, Réglages.
namespace ui {

enum Page { PAGE_HOME = 0, PAGE_CAMERA, PAGE_MEASURES, PAGE_COMMANDS, PAGE_MODULES, PAGE_SETTINGS, PAGE_COUNT };

// Couleurs communes.
lv_color_t colorBg();
lv_color_t colorCard();
lv_color_t colorAccent();
lv_color_t colorWarning();
lv_color_t colorError();
lv_color_t colorText();
lv_color_t colorMuted();

// Écran d'attente affiché pendant le démarrage (connexion Wi-Fi…).
void showSplash(const char *message);

// Construit toute l'interface. À appeler une fois, après display::begin().
void begin();
// Met à jour les informations d'état (heure, Wi-Fi…). Appelé chaque seconde.
void tick();

void showPage(Page page);
Page currentPage();

// Conteneurs mis à disposition des autres modules de l'interface (vue caméra…).
lv_obj_t *pageContainer(Page page);
lv_obj_t *homeCameraCard();
lv_obj_t *modulesList();

// Petits outils de construction.
lv_obj_t *makeCard(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);

// Nombre de modules connus / en ligne (affiché sur l'accueil).
void setModuleCounts(uint8_t online, uint8_t total);

// Saisie de texte plein écran avec clavier virtuel (AZERTY).
// `done` reçoit le texte validé ; rien n'est appelé si l'utilisateur annule.
typedef void (*TextDoneFn)(const char *text);
void openTextInput(const char *title, const char *hint, const char *initial, bool password, uint8_t maxLen,
                   TextDoneFn done);
void closeTextInput();

// Panneau d'information plein écran (mise à jour, redémarrage…).
// percent < 0 : pas de barre de progression. Affiché immédiatement.
void showOverlay(const char *title, const char *text, int percent);
void hideOverlay();

// Boîte de message simple avec un bouton OK.
void showMessage(const char *title, const char *text);

// Actions fournies par main.cpp (Wi-Fi, renommage).
struct Actions {
  void (*openWifiPanel)() = nullptr;
  bool (*rename)(const char *name) = nullptr;
};
void setActions(const Actions &actions);

}  // namespace ui
