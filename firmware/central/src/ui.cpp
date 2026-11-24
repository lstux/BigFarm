#include "ui.h"

#include <BigFarmCore.h>
#include <WiFi.h>

#include "display.h"

namespace ui {

lv_color_t colorBg() { return lv_color_hex(0x10161A); }
lv_color_t colorCard() { return lv_color_hex(0x1C252B); }
lv_color_t colorAccent() { return lv_color_hex(0x1FBF8F); }
lv_color_t colorWarning() { return lv_color_hex(0xF0A500); }
lv_color_t colorError() { return lv_color_hex(0xE74C3C); }
lv_color_t colorText() { return lv_color_hex(0xE8EEEC); }
lv_color_t colorMuted() { return lv_color_hex(0x8A9A96); }

static const lv_coord_t kTopH = 44;
static const lv_coord_t kNavH = 64;
static const lv_coord_t kW = 480;
static const lv_coord_t kContentH = 480 - kTopH - kNavH;

static lv_obj_t *gSplash = nullptr;
static lv_obj_t *gPages[PAGE_COUNT] = {nullptr};
static lv_obj_t *gNavBtns[PAGE_COUNT] = {nullptr};
static Page gCurrent = PAGE_HOME;

// Barre d'état
static lv_obj_t *gClock = nullptr;
static lv_obj_t *gWifiIcon = nullptr;
static lv_obj_t *gWifiText = nullptr;

// Accueil
static lv_obj_t *gHomeCamera = nullptr;
static lv_obj_t *gHomeModules = nullptr;
static lv_obj_t *gHomeAlerts = nullptr;

// Modules
static lv_obj_t *gModulesList = nullptr;

// Réglages
static lv_obj_t *gSetWifi = nullptr;
static lv_obj_t *gSetSystem = nullptr;
static lv_obj_t *gRotBtns[4] = {nullptr};
static lv_obj_t *gRebootBox = nullptr;

// ---------------------------------------------------------------------------
// Outils

lv_obj_t *makeCard(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, colorCard(), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 10, 0);
  lv_obj_set_style_pad_all(c, 10, 0);
  lv_obj_set_style_pad_row(c, 4, 0);
  lv_obj_set_style_pad_column(c, 8, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

static lv_obj_t *makeWrapLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
                               lv_coord_t width) {
  lv_obj_t *l = makeLabel(parent, text, font, color);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, width);
  return l;
}

static lv_obj_t *makeColumn(lv_obj_t *card) {
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return card;
}

static lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *userData) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_style_bg_color(b, colorAccent(), 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &bf_font_18, 0);
  lv_obj_center(l);
  if (cb != nullptr) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, userData);
  return b;
}

// ---------------------------------------------------------------------------
// Démarrage

void showSplash(const char *message) {
  if (gSplash == nullptr) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, colorBg(), 0);
    gSplash = lv_obj_create(scr);
    lv_obj_set_size(gSplash, kW, 480);
    lv_obj_set_style_bg_color(gSplash, colorBg(), 0);
    lv_obj_set_style_border_width(gSplash, 0, 0);
    lv_obj_t *t = makeLabel(gSplash, "BIGFARM", &bf_font_32, colorAccent());
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);
    lv_obj_t *m = makeLabel(gSplash, "", &bf_font_18, colorMuted());
    lv_obj_align(m, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_user_data(gSplash, m);
  }
  lv_obj_t *m = static_cast<lv_obj_t *>(lv_obj_get_user_data(gSplash));
  lv_label_set_text(m, message);
  lv_obj_align(m, LV_ALIGN_CENTER, 0, 20);
  display::loop();
}

// ---------------------------------------------------------------------------
// Navigation

static void navCb(lv_event_t *e) {
  const intptr_t page = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  showPage(static_cast<Page>(page));
}

void showPage(Page page) {
  if (page >= PAGE_COUNT) return;
  gCurrent = page;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (gPages[i] == nullptr) continue;
    if (i == page) {
      lv_obj_clear_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_state(gNavBtns[i], LV_STATE_CHECKED);
    } else {
      lv_obj_add_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_state(gNavBtns[i], LV_STATE_CHECKED);
    }
  }
}

Page currentPage() { return gCurrent; }
lv_obj_t *pageContainer(Page page) { return page < PAGE_COUNT ? gPages[page] : nullptr; }
lv_obj_t *homeCameraCard() { return gHomeCamera; }
lv_obj_t *modulesList() { return gModulesList; }

static void buildTopBar(lv_obj_t *scr) {
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, kW, kTopH);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x0B1013), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 14, 0);
  lv_obj_set_style_pad_ver(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = makeLabel(bar, "BIGFARM", &bf_font_18, colorAccent());
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

  gWifiText = makeLabel(bar, "", &bf_font_14, colorMuted());
  lv_obj_align(gWifiText, LV_ALIGN_RIGHT_MID, 0, 0);
  gWifiIcon = makeLabel(bar, LV_SYMBOL_WIFI, &bf_font_18, colorMuted());
  lv_obj_align_to(gWifiIcon, gWifiText, LV_ALIGN_OUT_LEFT_MID, -6, 0);

  gClock = makeLabel(bar, "--:--", &bf_font_18, colorText());
  lv_obj_align(gClock, LV_ALIGN_CENTER, 0, 0);
}

static void buildNavBar(lv_obj_t *scr) {
  static const char *const kIcons[PAGE_COUNT] = {LV_SYMBOL_HOME,  LV_SYMBOL_VIDEO, LV_SYMBOL_TINT,
                                                 LV_SYMBOL_POWER, LV_SYMBOL_LIST,  LV_SYMBOL_SETTINGS};
  static const char *const kNames[PAGE_COUNT] = {"Accueil", "Caméra", "Mesures", "Commandes", "Modules", "Réglages"};

  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, kW, kNavH);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x0B1013), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_set_style_pad_column(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);

  for (int i = 0; i < PAGE_COUNT; i++) {
    lv_obj_t *b = lv_btn_create(bar);
    lv_obj_set_size(b, kW / PAGE_COUNT, kNavH);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_pad_row(b, 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, colorCard(), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, colorMuted(), 0);
    lv_obj_set_style_text_color(b, colorAccent(), LV_STATE_CHECKED);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *icon = lv_label_create(b);
    lv_label_set_text(icon, kIcons[i]);
    lv_obj_set_style_text_font(icon, &bf_font_24, 0);
    lv_obj_t *name = lv_label_create(b);
    lv_label_set_text(name, kNames[i]);
    lv_obj_set_style_text_font(name, &bf_font_14, 0);
    lv_obj_set_style_text_letter_space(name, -1, 0);  // « Commandes » tient dans 80 px
    lv_obj_add_event_cb(b, navCb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    gNavBtns[i] = b;
  }
}

static lv_obj_t *buildPage(lv_obj_t *scr) {
  lv_obj_t *p = lv_obj_create(scr);
  lv_obj_set_size(p, kW, kContentH);
  lv_obj_align(p, LV_ALIGN_TOP_MID, 0, kTopH);
  lv_obj_set_style_bg_color(p, colorBg(), 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_radius(p, 0, 0);
  lv_obj_set_style_pad_all(p, 12, 0);
  lv_obj_set_style_pad_row(p, 10, 0);
  lv_obj_set_style_pad_column(p, 10, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_scroll_dir(p, LV_DIR_VER);
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  return p;
}

static lv_obj_t *pageTitle(lv_obj_t *page, const char *text) {
  lv_obj_t *t = makeLabel(page, text, &bf_font_24, colorText());
  lv_obj_set_width(t, kW - 24);
  return t;
}

// ---------------------------------------------------------------------------
// Pages

static void openCameraCb(lv_event_t *) { showPage(PAGE_CAMERA); }

static lv_obj_t *valueCard(lv_obj_t *parent, const char *name, const char *value, const char *sub,
                           lv_coord_t w, lv_coord_t h) {
  lv_obj_t *c = makeColumn(makeCard(parent, w, h));
  makeLabel(c, name, &bf_font_14, colorMuted());
  lv_obj_t *v = makeLabel(c, value, &bf_font_32, colorText());
  if (sub != nullptr) makeLabel(c, sub, &bf_font_14, colorMuted());
  return v;
}

static void buildHome(lv_obj_t *p) {
  pageTitle(p, "MINI TENTE");
  const lv_coord_t half = (kW - 24 - 10) / 2;
  valueCard(p, "Température", "— °C", "aucun capteur", half, 100);
  valueCard(p, "Humidité", "— %", "aucun capteur", half, 100);

  gHomeCamera = makeCard(p, kW - 24, 120);
  lv_obj_t *camTitle = makeLabel(gHomeCamera, LV_SYMBOL_VIDEO "  Caméra", &bf_font_18, colorText());
  lv_obj_align(camTitle, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *camState = makeLabel(gHomeCamera, "Non configurée", &bf_font_14, colorMuted());
  lv_obj_align(camState, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_obj_set_user_data(gHomeCamera, camState);
  lv_obj_t *open = makeButton(gHomeCamera, "Ouvrir", openCameraCb, nullptr);
  lv_obj_align(open, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_t *mods = makeCard(p, half, 64);
  makeLabel(mods, "Modules", &bf_font_14, colorMuted());
  gHomeModules = makeLabel(mods, "1/1", &bf_font_24, colorText());
  lv_obj_align(gHomeModules, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *alerts = makeCard(p, half, 64);
  makeLabel(alerts, "Alertes", &bf_font_14, colorMuted());
  gHomeAlerts = makeLabel(alerts, "0", &bf_font_24, colorText());
  lv_obj_align(gHomeAlerts, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void buildCameraPlaceholder(lv_obj_t *p) {
  pageTitle(p, "CAMÉRA");
  lv_obj_t *c = makeColumn(makeCard(p, kW - 24, 160));
  makeWrapLabel(c, "La vue caméra arrive avec la prochaine mise à jour de la centrale.", &bf_font_18,
                colorText(), kW - 48);
  makeWrapLabel(c, "En attendant : page Caméra du module, depuis un navigateur.", &bf_font_14, colorMuted(),
                kW - 48);
}

static void buildMeasures(lv_obj_t *p) {
  pageTitle(p, "MESURES");
  lv_obj_t *c = makeColumn(makeCard(p, kW - 24, 150));
  makeLabel(c, LV_SYMBOL_TINT "  Aucun module de mesure", &bf_font_18, colorText());
  makeWrapLabel(c,
                "Les mesures (température, humidité, substrat…) apparaîtront ici dès qu'un module de mesure "
                "sera connecté.",
                &bf_font_14, colorMuted(), kW - 48);
}

static void buildCommands(lv_obj_t *p) {
  pageTitle(p, "COMMANDES");
  static const char *const kOutputs[3] = {"Sortie 1 (L1)", "Sortie 2 (L2)", "Sortie 3 (L3)"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *c = makeCard(p, kW - 24, 64);
    lv_obj_t *n = makeLabel(c, kOutputs[i], &bf_font_18, colorText());
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *s = makeLabel(c, "Désactivée : carte à valider", &bf_font_14, colorWarning());
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *sw = lv_switch_create(c);
    lv_obj_add_state(sw, LV_STATE_DISABLED);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  makeWrapLabel(p,
                LV_SYMBOL_WARNING " Les sorties secteur restent coupées tant que la carte fille n'est pas "
                                  "identifiée électriquement.",
                &bf_font_14, colorMuted(), kW - 24);
}

static void buildModules(lv_obj_t *p) {
  pageTitle(p, "MODULES");
  gModulesList = lv_obj_create(p);
  lv_obj_set_size(gModulesList, kW - 24, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(gModulesList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(gModulesList, 0, 0);
  lv_obj_set_style_pad_all(gModulesList, 0, 0);
  lv_obj_set_style_pad_row(gModulesList, 10, 0);
  lv_obj_set_flex_flow(gModulesList, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(gModulesList, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *c = makeColumn(makeCard(gModulesList, kW - 24, LV_SIZE_CONTENT));
  makeLabel(c, LV_SYMBOL_HOME "  BigFarm Central", &bf_font_18, colorText());
  String line = String("Cette centrale · en ligne · ") + bf::firmwareVersion();
  makeLabel(c, line.c_str(), &bf_font_14, colorAccent());
  makeLabel(c, bf::moduleId().c_str(), &bf_font_14, colorMuted());
}

static void brightnessCb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  display::setBrightness(static_cast<uint8_t>(lv_slider_get_value(slider)));
}

static void updateRotationButtons() {
  static const uint16_t kDeg[4] = {0, 90, 180, 270};
  for (int i = 0; i < 4; i++) {
    if (gRotBtns[i] == nullptr) continue;
    if (display::rotation() == kDeg[i]) {
      lv_obj_add_state(gRotBtns[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(gRotBtns[i], LV_STATE_CHECKED);
    }
  }
}

static void rotationCb(lv_event_t *e) {
  const intptr_t deg = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  display::setRotation(static_cast<uint16_t>(deg));
  updateRotationButtons();
}

static void rebootAnswerCb(lv_event_t *e) {
  lv_obj_t *box = lv_event_get_current_target(e);
  const uint16_t btn = lv_msgbox_get_active_btn(box);
  if (btn == 0) {
    ESP.restart();
  }
  lv_msgbox_close(box);
  gRebootBox = nullptr;
}

static void rebootCb(lv_event_t *) {
  if (gRebootBox != nullptr) return;
  static const char *btns[] = {"Redémarrer", "Annuler", ""};
  gRebootBox = lv_msgbox_create(nullptr, "Redémarrer", "Redémarrer la centrale maintenant ?", btns, false);
  lv_obj_set_width(gRebootBox, 360);
  lv_obj_center(gRebootBox);
  lv_obj_add_event_cb(gRebootBox, rebootAnswerCb, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void buildSettings(lv_obj_t *p) {
  pageTitle(p, "RÉGLAGES");

  lv_obj_t *wifi = makeColumn(makeCard(p, kW - 24, LV_SIZE_CONTENT));
  makeLabel(wifi, LV_SYMBOL_WIFI "  Wi-Fi", &bf_font_18, colorText());
  gSetWifi = makeWrapLabel(wifi, "", &bf_font_14, colorMuted(), kW - 48);

  lv_obj_t *scr = makeColumn(makeCard(p, kW - 24, LV_SIZE_CONTENT));
  makeLabel(scr, LV_SYMBOL_IMAGE "  Écran", &bf_font_18, colorText());
  makeLabel(scr, "Luminosité", &bf_font_14, colorMuted());
  lv_obj_t *slider = lv_slider_create(scr);
  lv_obj_set_width(slider, kW - 80);
  lv_slider_set_range(slider, 10, 255);
  lv_slider_set_value(slider, display::brightness(), LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, brightnessCb, LV_EVENT_RELEASED, nullptr);
  makeLabel(scr, "Rotation", &bf_font_14, colorMuted());
  lv_obj_t *row = lv_obj_create(scr);
  lv_obj_set_size(row, kW - 48, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  static const char *const kRot[4] = {"0°", "90°", "180°", "270°"};
  static const uint16_t kDeg[4] = {0, 90, 180, 270};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *b = makeButton(row, kRot[i], rotationCb, reinterpret_cast<void *>(static_cast<intptr_t>(kDeg[i])));
    lv_obj_set_width(b, 92);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A363D), 0);
    lv_obj_set_style_bg_color(b, colorAccent(), LV_STATE_CHECKED);
    gRotBtns[i] = b;
  }
  updateRotationButtons();

  lv_obj_t *sys = makeColumn(makeCard(p, kW - 24, LV_SIZE_CONTENT));
  makeLabel(sys, LV_SYMBOL_SETTINGS "  Système", &bf_font_18, colorText());
  gSetSystem = makeWrapLabel(sys, "", &bf_font_14, colorMuted(), kW - 48);
  lv_obj_t *reboot = makeButton(sys, LV_SYMBOL_REFRESH " Redémarrer", rebootCb, nullptr);
  lv_obj_set_style_bg_color(reboot, lv_color_hex(0x5A3A3A), 0);
}

// ---------------------------------------------------------------------------

void begin() {
  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t *th = lv_theme_default_init(disp, colorAccent(), colorWarning(), true, &bf_font_18);
  lv_disp_set_theme(disp, th);

  if (gSplash != nullptr) {
    lv_obj_del(gSplash);
    gSplash = nullptr;
  }
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, colorBg(), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildTopBar(scr);
  for (int i = 0; i < PAGE_COUNT; i++) gPages[i] = buildPage(scr);
  buildNavBar(scr);

  buildHome(gPages[PAGE_HOME]);
  buildCameraPlaceholder(gPages[PAGE_CAMERA]);
  buildMeasures(gPages[PAGE_MEASURES]);
  buildCommands(gPages[PAGE_COMMANDS]);
  buildModules(gPages[PAGE_MODULES]);
  buildSettings(gPages[PAGE_SETTINGS]);

  showPage(PAGE_HOME);
  tick();
}

void setModuleCounts(uint8_t online, uint8_t total) {
  if (gHomeModules == nullptr) return;
  char buf[12];
  snprintf(buf, sizeof buf, "%u/%u", online, total);
  lv_label_set_text(gHomeModules, buf);
}

static String uptimeText() {
  const uint32_t s = millis() / 1000;
  char buf[24];
  snprintf(buf, sizeof buf, "%luj %02luh%02lu", static_cast<unsigned long>(s / 86400),
           static_cast<unsigned long>((s / 3600) % 24), static_cast<unsigned long>((s / 60) % 60));
  return String(buf);
}

void tick() {
  // Heure
  if (gClock != nullptr) {
    if (bf::timeValid()) {
      const time_t t = time(nullptr);
      struct tm tm;
      localtime_r(&t, &tm);
      char buf[8];
      strftime(buf, sizeof buf, "%H:%M", &tm);
      lv_label_set_text(gClock, buf);
    } else {
      lv_label_set_text(gClock, "--:--");
    }
  }

  // Wi-Fi
  const bool connected = WiFi.status() == WL_CONNECTED;
  const wifi_mode_t mode = WiFi.getMode();
  const bool ap = (mode == WIFI_AP || mode == WIFI_AP_STA);
  if (gWifiIcon != nullptr) {
    lv_color_t c = connected ? colorAccent() : (ap ? colorWarning() : colorMuted());
    lv_obj_set_style_text_color(gWifiIcon, c, 0);
    lv_label_set_text(gWifiText, connected ? "En ligne" : (ap ? "Configuration" : "Hors ligne"));
    lv_obj_align_to(gWifiIcon, gWifiText, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  }

  if (gPages[PAGE_SETTINGS] != nullptr && gSetWifi != nullptr) {
    String w;
    if (connected) {
      w = "Réseau : " + WiFi.SSID() + "\nAdresse : " + WiFi.localIP().toString() + "\nSignal : " +
          String(WiFi.RSSI()) + " dBm\nWeb : http://" + bf::moduleId() + ".local/";
    } else {
      w = "Non connecté.";
    }
    if (ap) {
      w += "\n\nPoint d'accès de configuration :\n" + WiFi.softAPSSID() + "  (mot de passe bigfarm-setup)\n" +
           "puis http://" + WiFi.softAPIP().toString() + "/wifi";
    }
    lv_label_set_text(gSetWifi, w.c_str());

    String s = "Identifiant : " + bf::moduleId() + "\nFirmware : " + String(bf::firmwareVersion()) +
               "\nHeure : " + bf::nowText() + "\nEn marche depuis : " + uptimeText() +
               "\nMémoire libre : " + bf::humanBytes(ESP.getFreeHeap()) + " interne, " +
               bf::humanBytes(ESP.getFreePsram()) + " PSRAM";
    lv_label_set_text(gSetSystem, s.c_str());
  }
  (void)gHomeAlerts;
}

}  // namespace ui
