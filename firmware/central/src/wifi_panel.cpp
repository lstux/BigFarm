#include "wifi_panel.h"

#include <WiFi.h>
#include <lvgl.h>

#include "ui.h"

namespace wifipanel {

static const lv_coord_t kW = 480;

static SaveFn gSave = nullptr;
static lv_obj_t *gPanel = nullptr;
static lv_obj_t *gStatus = nullptr;
static lv_obj_t *gList = nullptr;
static lv_timer_t *gTimer = nullptr;
static String gSsid;

void setSaveHandler(SaveFn fn) { gSave = fn; }

void close() {
  if (gTimer != nullptr) {
    lv_timer_del(gTimer);
    gTimer = nullptr;
  }
  if (gPanel != nullptr) {
    lv_obj_del(gPanel);
    gPanel = nullptr;
    gStatus = nullptr;
    gList = nullptr;
  }
}

static void save(const String &ssid, const String &pass) {
  close();
  String msg = "Réseau « " + ssid + " » enregistré.\nRedémarrage et connexion…";
  ui::showOverlay("Wi-Fi", msg.c_str(), -1);
  if (gSave != nullptr) gSave(ssid, pass);
}

static void passwordDone(const char *text) { save(gSsid, String(text)); }

static void askPassword() {
  String hint = "Réseau « " + gSsid + " »";
  ui::openTextInput("Mot de passe", hint.c_str(), "", true, 63, passwordDone);
}

static void ssidDone(const char *text) {
  gSsid = text;
  gSsid.trim();
  if (gSsid.length() == 0) return;
  askPassword();
}

static const char *bars(int32_t rssi) {
  if (rssi >= -55) return "▂▄▆█";
  if (rssi >= -67) return "▂▄▆";
  if (rssi >= -78) return "▂▄";
  return "▂";
}

static void networkCb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);
  const bool open = lv_obj_get_user_data(btn) != nullptr;
  lv_obj_t *label = lv_obj_get_child(btn, 0);
  gSsid = lv_label_get_text(label);
  if (open) {
    save(gSsid, "");
  } else {
    askPassword();
  }
}

static void addNetwork(const String &ssid, int32_t rssi, bool open) {
  lv_obj_t *b = lv_btn_create(gList);
  lv_obj_set_size(b, kW - 24, 52);
  lv_obj_set_style_bg_color(b, ui::colorCard(), 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t *name = ui::makeLabel(b, ssid.c_str(), &bf_font_18, ui::colorText());
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, kW - 170);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
  char info[48];
  snprintf(info, sizeof info, "%s%s %ld dBm", open ? "ouvert  " : "", bars(rssi),
           static_cast<long>(rssi));
  lv_obj_t *sig = ui::makeLabel(b, info, &bf_font_14, ui::colorAccent());
  lv_obj_align(sig, LV_ALIGN_RIGHT_MID, 0, 0);
  // Réseau ouvert : pas de mot de passe à demander.
  lv_obj_set_user_data(b, open ? reinterpret_cast<void *>(1) : nullptr);
  lv_obj_add_event_cb(b, networkCb, LV_EVENT_CLICKED, nullptr);
}

static void pollScan(lv_timer_t *) {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  lv_timer_pause(gTimer);
  lv_obj_clean(gList);
  if (n < 0) {
    lv_label_set_text(gStatus, "La recherche a échoué. Toucher « Actualiser » pour réessayer.");
    WiFi.scanDelete();
    return;
  }
  int shown = 0;
  for (int16_t i = 0; i < n && shown < 20; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;  // réseau masqué
    bool duplicate = false;
    for (int16_t k = 0; k < i; k++) {
      if (WiFi.SSID(k) == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    addNetwork(ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    shown++;
  }
  WiFi.scanDelete();
  char buf[64];
  snprintf(buf, sizeof buf, shown ? "%d réseau(x). Toucher un réseau pour le choisir." : "Aucun réseau détecté.",
           shown);
  lv_label_set_text(gStatus, buf);
}

static void startScan() {
  lv_label_set_text(gStatus, "Recherche des réseaux…");
  lv_obj_clean(gList);
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    lv_label_set_text(gStatus, "La recherche n'a pas pu démarrer. Toucher « Actualiser ».");
    return;
  }
  lv_timer_resume(gTimer);
}

static void closeCb(lv_event_t *) { close(); }
static void refreshCb(lv_event_t *) { startScan(); }
static void otherCb(lv_event_t *) {
  ui::openTextInput("Nom du réseau", "Réseau masqué ou absent de la liste", "", false, 32, ssidDone);
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_color_t color) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_height(b, 44);
  lv_obj_set_style_bg_color(b, color, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_t *l = ui::makeLabel(b, text, &bf_font_18, ui::colorText());
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

void open() {
  close();
  gPanel = lv_obj_create(lv_layer_top());
  lv_obj_set_size(gPanel, kW, 480);
  lv_obj_set_style_bg_color(gPanel, ui::colorBg(), 0);
  lv_obj_set_style_bg_opa(gPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gPanel, 0, 0);
  lv_obj_set_style_radius(gPanel, 0, 0);
  lv_obj_set_style_pad_all(gPanel, 12, 0);
  lv_obj_clear_flag(gPanel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = ui::makeLabel(gPanel, LV_SYMBOL_WIFI "  Choisir un réseau", &bf_font_24, ui::colorText());
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 4);
  lv_obj_t *x = button(gPanel, LV_SYMBOL_CLOSE, closeCb, lv_color_hex(0x2A363D));
  lv_obj_set_width(x, 48);
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 0);

  gStatus = ui::makeLabel(gPanel, "", &bf_font_14, ui::colorMuted());
  lv_label_set_long_mode(gStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(gStatus, kW - 24);
  lv_obj_align(gStatus, LV_ALIGN_TOP_LEFT, 0, 50);

  gList = lv_obj_create(gPanel);
  lv_obj_set_size(gList, kW - 24, 300);
  lv_obj_align(gList, LV_ALIGN_TOP_LEFT, 0, 76);
  lv_obj_set_style_bg_opa(gList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(gList, 0, 0);
  lv_obj_set_style_pad_all(gList, 0, 0);
  lv_obj_set_style_pad_row(gList, 6, 0);
  lv_obj_set_flex_flow(gList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(gList, LV_DIR_VER);

  lv_obj_t *refresh = button(gPanel, LV_SYMBOL_REFRESH " Actualiser", refreshCb, lv_color_hex(0x2A363D));
  lv_obj_set_width(refresh, 200);
  lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *other = button(gPanel, LV_SYMBOL_EDIT " Autre réseau", otherCb, ui::colorAccent());
  lv_obj_set_width(other, 220);
  lv_obj_align(other, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  gTimer = lv_timer_create(pollScan, 300, nullptr);
  lv_timer_pause(gTimer);
  startScan();
}

}  // namespace wifipanel
