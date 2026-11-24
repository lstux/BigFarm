// Simulateur PC : construit l'interface avec des données fictives et enregistre chaque page.
#include <lvgl.h>
#include <cstdio>
#include <vector>
#include "Arduino.h"
#include "WiFi.h"
#include "ui.h"
EspClass ESP; WiFiClass WiFi;
static uint32_t gMs = 0;
uint32_t millis() { return gMs; }
namespace bf { bool timeValid(){return true;} String moduleId(){return "bf-central-a1b2c3";} const char* firmwareVersion(){return "0.2.0";}
 String nowText(){return "2026-11-25 07:12:03";} String humanBytes(uint64_t b){char x[32];snprintf(x,32,"%.1f Mo",b/1048576.0);return x;} }
static uint8_t gBr=200; static uint16_t gRot=0;
namespace display { uint8_t brightness(){return gBr;} uint16_t rotation(){return gRot;} void setBrightness(uint8_t v){gBr=v;} bool setRotation(uint16_t r){gRot=r;return true;} void loop(){ lv_timer_handler(); } }
static std::vector<uint16_t> fb(480*480);
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c){
  for(int y=a->y1;y<=a->y2;y++) for(int x=a->x1;x<=a->x2;x++) fb[y*480+x]=(c++)->full;
  lv_disp_flush_ready(d);}
static void run(int ms){ for(int i=0;i<ms/10;i++){ gMs+=10; lv_tick_inc(10); lv_timer_handler(); } }
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
  WiFi.st=0; WiFi.mode=WIFI_AP; ui::tick(); ui::showPage(ui::PAGE_SETTINGS); run(300); dump("out/reglages_ap.ppm");
  return 0; }
