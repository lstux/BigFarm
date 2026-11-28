#pragma once
#include "Arduino.h"
typedef enum { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA } wifi_mode_t;
#define WL_CONNECTED 3
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 } wifi_auth_mode_t;
struct IPAddress { const char *s; String toString() const { return String(s); } };
struct WiFiClass {
  int st = WL_CONNECTED; wifi_mode_t mode = WIFI_STA;
  int status() { return st; } wifi_mode_t getMode() { return mode; }
  String SSID() { return "MaBox"; } IPAddress localIP() { return {"192.168.1.42"}; } int RSSI() { return -58; }
  // Scan fictif
  int16_t scanNetworks(bool = false) { return WIFI_SCAN_RUNNING; }
  int16_t scanComplete() { return 5; }
  void scanDelete() {}
  String SSID(int i) { static const char *n[] = {"Bbox-1234ABCD", "Bbox-1234ABCD", "Freebox-Voisin", "", "Invités"}; return n[i]; }
  int32_t RSSI(int i) { static const int r[] = {-48, -70, -66, -80, -83}; return r[i]; }
  wifi_auth_mode_t encryptionType(int i) { return i == 4 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK; }
  String softAPSSID() { return "BigFarm-central-a1b2c3"; } IPAddress softAPIP() { return {"192.168.4.1"}; }
};
extern WiFiClass WiFi;
