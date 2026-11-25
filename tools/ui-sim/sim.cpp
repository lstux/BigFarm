// Simulateur PC : construit l'interface avec des données fictives et enregistre chaque page.
#include <lvgl.h>
#include <cstdio>
#include <vector>
#include "Arduino.h"
#include "WiFi.h"
#include "ui.h"
#include "camera_client.h"
EspClass ESP; WiFiClass WiFi;
static uint32_t gMs = 0;
uint32_t millis() { return gMs; }
namespace bf { bool timeValid(){return true;} String moduleId(){return "bf-central-a1b2c3";} const char* firmwareVersion(){return "0.2.0";}
 String nowText(){return "2026-11-25 07:12:03";} String hostName(){return "bigfarm";} String humanBytes(uint64_t b){char x[32];snprintf(x,32,"%.1f Mo",b/1048576.0);return x;} }
// Caméra fictive : mire colorée 400x300, flux « en direct ».
namespace camclient {
static std::vector<uint16_t> pix(400 * 300);
static bool live = true;
void begin() {}
void setWanted(bool) {}
void setManualHost(const String &) {}
void rediscover() {}
const char *stateText(State s) {
  static const char *t[] = {"Recherche de la caméra…", "Caméra introuvable", "Caméra trouvée", "Connexion au flux…",
                            "En direct", "Flux interrompu"};
  return t[s];
}
Status status() {
  Status s;
  if (live) {
    s.state = STATE_LIVE; s.host = "192.168.1.50"; s.id = "bf-cam-d4e5f6"; s.firmware = "0.1.0-diag";
    s.fps = 9.6f; s.decodeMs = 38; s.sourceWidth = 800; s.sourceHeight = 600; s.lastFrameMs = millis();
  } else {
    s.state = STATE_NOT_FOUND; s.message = "aucune caméra annoncée sur le réseau";
  }
  return s;
}
bool takeFrame(uint32_t seq, Frame &out) {
  if (!live || seq >= 1) return false;
  for (int y = 0; y < 300; y++)
    for (int x = 0; x < 400; x++) {
      const int r = x * 31 / 399, g = y * 63 / 299, b = ((x / 50 + y / 50) & 1) ? 31 : 8;
      pix[y * 400 + x] = (r << 11) | (g << 5) | b;
    }
  out.pixels = pix.data(); out.width = 400; out.height = 300; out.sequence = 1;
  return true;
}
int resolutionCount() { return 3; }
String resolutionName(int i) { static const char *n[] = {"VGA 640x480", "SVGA 800x600", "UXGA 1600x1200"}; return n[i]; }
int resolutionId(int i) { static const int id[] = {8, 9, 13}; return id[i]; }
int currentResolutionId() { return 9; }
void setResolution(int) {}
void requestPhoto() {}
String photoResult() { return ""; }
void addStatus(bf::JsonWriter &) {}
}  // namespace camclient
#include "camera_view.h"
#include "wifi_panel.h"
static void noText(const char *) {}

static uint8_t gBr=80; static uint16_t gRot=0;
namespace display { uint8_t brightness(){return gBr;} uint16_t rotation(){return gRot;} void setBrightness(uint8_t v){gBr=v;} void previewBrightness(uint8_t){} bool setRotation(uint16_t r){gRot=r;return true;} void loop(){ lv_timer_handler(); } }
static std::vector<uint16_t> fb(480*480);
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c){
  for(int y=a->y1;y<=a->y2;y++) for(int x=a->x1;x<=a->x2;x++) fb[y*480+x]=(c++)->full;
  lv_disp_flush_ready(d);}
static void run(int ms){ for(int i=0;i<ms/10;i++){ gMs+=10; lv_tick_inc(10); camview::loop(); lv_timer_handler(); } }
static void dump(const char *name){
  FILE *f=fopen(name,"wb"); fprintf(f,"P6 480 480 255\n");
  for(auto p:fb){ unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)}; fwrite(rgb,1,3,f);} fclose(f);}
int main(int argc,char**argv){
  lv_init();
  static lv_disp_draw_buf_t db; static std::vector<lv_color_t> b1(480*40);
  lv_disp_draw_buf_init(&db,b1.data(),nullptr,480*40);
  static lv_disp_drv_t dd; lv_disp_drv_init(&dd); dd.hor_res=480; dd.ver_res=480; dd.flush_cb=flush; dd.draw_buf=&db; lv_disp_drv_register(&dd);
  ui::showSplash("Connexion au Wi-Fi…"); run(100); dump("out/splash.ppm");
  ui::begin(); run(200);
  const char* names[]={"accueil","camera","mesures","commandes","modules","reglages"};
  for(int i=0;i<6;i++){ ui::showPage((ui::Page)i); run(300); char n[64]; snprintf(n,64,"out/%d_%s.ppm",i,names[i]); dump(n);} 
  camclient::live=false; ui::showPage(ui::PAGE_CAMERA); run(1200); dump("out/camera_introuvable.ppm");
  ui::showPage(ui::PAGE_SETTINGS); wifipanel::open(); run(800); dump("out/wifi_liste.ppm"); wifipanel::close();
  ui::openTextInput("Mot de passe", "Réseau « Bbox-1234ABCD »", "", true, 63, noText); run(300); dump("out/clavier.ppm"); ui::closeTextInput();
  ui::showOverlay("Mise à jour", "Réception du nouveau firmware… 42 %\nNe pas couper l'alimentation.", 42); run(100); dump("out/maj.ppm"); ui::hideOverlay();
  WiFi.st=0; WiFi.mode=WIFI_AP; ui::tick(); ui::showPage(ui::PAGE_SETTINGS); run(300); dump("out/reglages_ap.ppm");
  return 0; }
