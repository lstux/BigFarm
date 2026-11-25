#include "camera_view.h"

#include <Arduino.h>

#include "camera_client.h"
#include "ui.h"

namespace camview {

static const lv_coord_t kAreaW = 456;
static const lv_coord_t kAreaH = 300;
static const lv_coord_t kHomePreviewH = 100;

static lv_img_dsc_t gDsc;
static uint32_t gSequence = 0;
static bool gHasImage = false;

// Page Caméra
static lv_obj_t *gArea = nullptr;
static lv_obj_t *gImg = nullptr;
static lv_obj_t *gPlaceholder = nullptr;
static lv_obj_t *gInfo = nullptr;
static lv_obj_t *gPauseLabel = nullptr;
static lv_obj_t *gResLabel = nullptr;
static lv_obj_t *gResMenu = nullptr;
static bool gPaused = false;

// Plein écran
static lv_obj_t *gFull = nullptr;
static lv_obj_t *gFullImg = nullptr;

// Accueil
static lv_obj_t *gHomeState = nullptr;
static lv_obj_t *gHomeImg = nullptr;

// Modules
static lv_obj_t *gModState = nullptr;
static lv_obj_t *gModDetail = nullptr;

static String gTransient;  // message temporaire (résultat de photo…)
static uint32_t gTransientUntil = 0;
static bool gPhotoPending = false;

static uint16_t zoomToFit(uint16_t w, uint16_t h, lv_coord_t maxW, lv_coord_t maxH) {
  if (w == 0 || h == 0) return LV_IMG_ZOOM_NONE;
  const uint32_t zw = static_cast<uint32_t>(maxW) * 256 / w;
  const uint32_t zh = static_cast<uint32_t>(maxH) * 256 / h;
  const uint32_t z = zw < zh ? zw : zh;
  return static_cast<uint16_t>(z > 1024 ? 1024 : z);
}

// ---------------------------------------------------------------------------
// Construction

static void photoCb(lv_event_t *) {
  camclient::requestPhoto();
  gPhotoPending = true;
  gTransient = "Photo demandée…";
  gTransientUntil = millis() + 10000;
}

static void pauseCb(lv_event_t *) {
  gPaused = !gPaused;
  lv_label_set_text(gPauseLabel, gPaused ? LV_SYMBOL_PLAY " Lecture" : LV_SYMBOL_PAUSE " Pause");
}

static void closeFullCb(lv_event_t *) {
  if (gFull != nullptr) {
    lv_obj_del(gFull);
    gFull = nullptr;
    gFullImg = nullptr;
  }
}

static void openFullCb(lv_event_t *) {
  if (gFull != nullptr || !gHasImage) return;
  gFull = lv_obj_create(lv_layer_top());
  lv_obj_set_size(gFull, 480, 480);
  lv_obj_set_style_bg_color(gFull, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(gFull, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gFull, 0, 0);
  lv_obj_set_style_radius(gFull, 0, 0);
  lv_obj_set_style_pad_all(gFull, 0, 0);
  lv_obj_clear_flag(gFull, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(gFull, closeFullCb, LV_EVENT_CLICKED, nullptr);
  gFullImg = lv_img_create(gFull);
  lv_img_set_src(gFullImg, &gDsc);
  lv_img_set_size_mode(gFullImg, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_pivot(gFullImg, 0, 0);
  lv_img_set_zoom(gFullImg, zoomToFit(gDsc.header.w, gDsc.header.h, 480, 480));
  lv_obj_center(gFullImg);
  lv_obj_t *hint = ui::makeLabel(gFull, "Toucher pour fermer", &bf_font_14, ui::colorMuted());
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// Choix de la résolution (valable pour le flux et pour les photos).
static void closeResMenu() {
  if (gResMenu != nullptr) {
    lv_obj_del(gResMenu);
    gResMenu = nullptr;
  }
}

static void resPickCb(lv_event_t *e) {
  const int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  camclient::setResolution(id);
  gTransient = "Résolution demandée…";
  gTransientUntil = millis() + 4000;
  closeResMenu();
}

static void resCancelCb(lv_event_t *) { closeResMenu(); }

static void openResMenu(lv_event_t *) {
  const int n = camclient::resolutionCount();
  if (gResMenu != nullptr || n == 0) return;
  gResMenu = lv_obj_create(lv_layer_top());
  lv_obj_set_size(gResMenu, 480, 480);
  lv_obj_set_style_bg_color(gResMenu, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(gResMenu, LV_OPA_90, 0);
  lv_obj_set_style_border_width(gResMenu, 0, 0);
  lv_obj_set_style_radius(gResMenu, 0, 0);
  lv_obj_set_style_pad_all(gResMenu, 12, 0);
  lv_obj_set_style_pad_row(gResMenu, 6, 0);
  lv_obj_set_flex_flow(gResMenu, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(gResMenu, LV_OBJ_FLAG_SCROLLABLE);
  ui::makeLabel(gResMenu, "Résolution (flux et photos)", &bf_font_18, ui::colorText());
  const int current = camclient::currentResolutionId();
  for (int i = n - 1; i >= 0; i--) {  // de la plus grande à la plus petite
    const int id = camclient::resolutionId(i);
    lv_obj_t *b = lv_btn_create(gResMenu);
    lv_obj_set_size(b, 440, 40);
    lv_obj_set_style_bg_color(b, id == current ? ui::colorAccent() : lv_color_hex(0x2A363D), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, camclient::resolutionName(i).c_str());
    lv_obj_set_style_text_font(l, &bf_font_18, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, resPickCb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(id)));
  }
  lv_obj_t *c = lv_btn_create(gResMenu);
  lv_obj_set_size(c, 440, 40);
  lv_obj_set_style_bg_color(c, lv_color_hex(0x444444), 0);
  lv_obj_set_style_radius(c, 8, 0);
  lv_obj_t *cl = lv_label_create(c);
  lv_label_set_text(cl, "Annuler");
  lv_obj_set_style_text_font(cl, &bf_font_18, 0);
  lv_obj_center(cl);
  lv_obj_add_event_cb(c, resCancelCb, LV_EVENT_CLICKED, nullptr);
}

static lv_obj_t *smallButton(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **labelOut) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_height(b, 40);
  lv_obj_set_style_bg_color(b, ui::colorAccent(), 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &bf_font_18, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  if (labelOut != nullptr) *labelOut = l;
  return b;
}

void buildPage(lv_obj_t *page) {
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(page, 8, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

  gArea = lv_obj_create(page);
  lv_obj_set_size(gArea, kAreaW, kAreaH);
  lv_obj_set_style_bg_color(gArea, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(gArea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gArea, 0, 0);
  lv_obj_set_style_radius(gArea, 8, 0);
  lv_obj_set_style_pad_all(gArea, 0, 0);
  lv_obj_clear_flag(gArea, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gArea, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(gArea, openFullCb, LV_EVENT_CLICKED, nullptr);

  gImg = lv_img_create(gArea);
  lv_obj_add_flag(gImg, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(gImg, LV_OBJ_FLAG_CLICKABLE);

  gPlaceholder = ui::makeLabel(gArea, "", &bf_font_18, ui::colorMuted());
  lv_label_set_long_mode(gPlaceholder, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(gPlaceholder, kAreaW - 40);
  lv_obj_set_style_text_align(gPlaceholder, LV_TEXT_ALIGN_CENTER, 0);
  // Fond sombre : le texte reste lisible par-dessus la dernière image.
  lv_obj_set_style_bg_color(gPlaceholder, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(gPlaceholder, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(gPlaceholder, 10, 0);
  lv_obj_set_style_radius(gPlaceholder, 6, 0);
  lv_obj_center(gPlaceholder);

  lv_obj_t *row = lv_obj_create(page);
  lv_obj_set_size(row, kAreaW, 40);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  gInfo = ui::makeLabel(row, "", &bf_font_14, ui::colorMuted());
  lv_label_set_long_mode(gInfo, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(gInfo, 150);
  lv_obj_align(gInfo, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *pause = smallButton(row, LV_SYMBOL_PAUSE " Pause", pauseCb, &gPauseLabel);
  lv_obj_set_width(pause, 100);
  lv_obj_set_style_bg_color(pause, lv_color_hex(0x2A363D), 0);
  lv_obj_align(pause, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *photo = smallButton(row, LV_SYMBOL_IMAGE " Photo", photoCb, nullptr);
  lv_obj_set_width(photo, 100);
  lv_obj_align_to(photo, pause, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  lv_obj_t *res = smallButton(row, "Rés.", openResMenu, &gResLabel);
  lv_obj_set_style_bg_color(res, lv_color_hex(0x2A363D), 0);
  lv_obj_set_width(res, 100);
  lv_obj_align_to(res, photo, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

void buildHome(lv_obj_t *card) {
  gHomeState = static_cast<lv_obj_t *>(lv_obj_get_user_data(card));
  gHomeImg = lv_img_create(card);
  lv_img_set_size_mode(gHomeImg, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_pivot(gHomeImg, 0, 0);
  lv_obj_add_flag(gHomeImg, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(gHomeImg, LV_ALIGN_RIGHT_MID, 0, 0);
}

void buildModuleCard(lv_obj_t *list) {
  lv_obj_t *c = ui::makeCard(list, 456, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  ui::makeLabel(c, LV_SYMBOL_VIDEO "  BigFarm Cam", &bf_font_18, ui::colorText());
  gModState = ui::makeLabel(c, "", &bf_font_14, ui::colorMuted());
  gModDetail = ui::makeLabel(c, "", &bf_font_14, ui::colorMuted());
}

// ---------------------------------------------------------------------------
// Mise à jour

static void showFrame(const camclient::Frame &f) {
  lv_img_cache_invalidate_src(&gDsc);
  gDsc.header.always_zero = 0;
  gDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  gDsc.header.w = f.width;
  gDsc.header.h = f.height;
  gDsc.data_size = static_cast<uint32_t>(f.width) * f.height * sizeof(uint16_t);
  gDsc.data = reinterpret_cast<const uint8_t *>(f.pixels);
  gHasImage = true;

  if (gImg != nullptr) {
    lv_img_set_src(gImg, &gDsc);
    lv_obj_clear_flag(gImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(gImg);
    lv_obj_invalidate(gImg);
    lv_obj_add_flag(gPlaceholder, LV_OBJ_FLAG_HIDDEN);
  }
  if (gHomeImg != nullptr && ui::currentPage() == ui::PAGE_HOME) {
    lv_img_set_src(gHomeImg, &gDsc);
    lv_img_set_zoom(gHomeImg, zoomToFit(f.width, f.height, 200, kHomePreviewH));
    lv_obj_clear_flag(gHomeImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(gHomeImg, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_invalidate(gHomeImg);
  }
  if (gFullImg != nullptr) {
    lv_img_set_src(gFullImg, &gDsc);
    lv_img_set_zoom(gFullImg, zoomToFit(f.width, f.height, 480, 480));
    lv_obj_center(gFullImg);
    lv_obj_invalidate(gFullImg);
  }
}

static void updateTexts(const camclient::Status &s) {
  const bool fresh = s.lastFrameMs != 0 && millis() - s.lastFrameMs < 5000;
  char info[128];
  if (s.state == camclient::STATE_LIVE && fresh) {
    snprintf(info, sizeof info, "En direct\n%.1f img/s · %lu ms", static_cast<double>(s.fps),
             static_cast<unsigned long>(s.decodeMs));
  } else if (gPaused && s.host.length()) {
    snprintf(info, sizeof info, "En pause · %s", s.host.c_str());
  } else {
    snprintf(info, sizeof info, "%s", camclient::stateText(s.state));
  }

  // Résultat de photo
  if (gPhotoPending) {
    const String r = camclient::photoResult();
    if (r.length()) {
      gPhotoPending = false;
      gTransient = r;
      gTransientUntil = millis() + 5000;
    }
  }
  // Bouton de résolution : taille courante (« 800x600 »), sinon « Rés. ».
  if (gResLabel != nullptr) {
    String t = "Rés.";
    const int cur = camclient::currentResolutionId();
    for (int i = 0; i < camclient::resolutionCount(); i++) {
      if (camclient::resolutionId(i) == cur) {
        const String n = camclient::resolutionName(i);
        const int sp = n.indexOf(' ');
        t = sp >= 0 ? n.substring(sp + 1) : n;
        break;
      }
    }
    if (t != lv_label_get_text(gResLabel)) lv_label_set_text(gResLabel, t.c_str());
  }
  const bool transient = gTransient.length() && millis() < gTransientUntil;
  lv_label_set_text(gInfo, transient ? gTransient.c_str() : info);

  // Image absente ou ancienne : texte explicite sur la zone d'image.
  if (!gHasImage || !fresh) {
    String p;
    if (gHasImage) {
      p = "Dernière image il y a " + String((millis() - s.lastFrameMs) / 1000) + " s\n";
    }
    p += camclient::stateText(s.state);
    if (s.message.length()) p += "\n" + s.message;
    if (s.state == camclient::STATE_NOT_FOUND) {
      p += "\n\nVérifier que la caméra est allumée et sur le même réseau, ou fixer son adresse : "
           "commande série « camera <ip> » ou page web de la centrale.";
    }
    lv_label_set_text(gPlaceholder, p.c_str());
    lv_obj_clear_flag(gPlaceholder, LV_OBJ_FLAG_HIDDEN);
    if (gHasImage) lv_obj_move_foreground(gPlaceholder);
  } else {
    lv_obj_add_flag(gPlaceholder, LV_OBJ_FLAG_HIDDEN);
  }

  // Accueil
  if (gHomeState != nullptr) {
    String h = camclient::stateText(s.state);
    if (s.state == camclient::STATE_LIVE && fresh) {
      char b[24];
      snprintf(b, sizeof b, " · %.0f img/s", static_cast<double>(s.fps));
      h += b;
    }
    lv_label_set_text(gHomeState, h.c_str());
    lv_obj_set_style_text_color(gHomeState, fresh ? ui::colorAccent() : ui::colorMuted(), 0);
  }

  // Modules
  const bool known = s.host.length() > 0 && s.state != camclient::STATE_NOT_FOUND;
  const bool online = known && s.state != camclient::STATE_ERROR;
  if (gModState != nullptr) {
    lv_label_set_text(gModState, known ? (online ? "En ligne" : "Hors ligne") : camclient::stateText(s.state));
    lv_obj_set_style_text_color(gModState, online ? ui::colorAccent() : ui::colorWarning(), 0);
    String d;
    if (known) {
      d = (s.id.length() ? s.id : String("caméra")) + " · " + s.host;
      if (s.firmware.length()) d += " · " + s.firmware;
      if (s.manual) d += " (adresse fixée)";
    } else if (s.message.length()) {
      d = s.message;
    }
    lv_label_set_text(gModDetail, d.c_str());
  }
  ui::setModuleCounts(1 + (online ? 1 : 0), 1 + (known ? 1 : 0));
}

void loop() {
  if (gInfo == nullptr) return;
  const ui::Page page = ui::currentPage();
  const bool visible = (page == ui::PAGE_CAMERA || page == ui::PAGE_HOME || gFull != nullptr);
  camclient::setWanted(visible && !gPaused);

  camclient::Frame f;
  if (camclient::takeFrame(gSequence, f)) {
    gSequence = f.sequence;
    showFrame(f);
  }

  static uint32_t lastText = 0;
  if (millis() - lastText >= 500) {
    lastText = millis();
    updateTexts(camclient::status());
  }
}

}  // namespace camview
