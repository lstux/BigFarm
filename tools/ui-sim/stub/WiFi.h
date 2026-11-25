#pragma once
#include "Arduino.h"
typedef enum { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA } wifi_mode_t;
#define WL_CONNECTED 3
struct IPAddress { const char *s; String toString() const { return String(s); } };
struct WiFiClass {
  int st = WL_CONNECTED; wifi_mode_t mode = WIFI_STA;
  int status() { return st; } wifi_mode_t getMode() { return mode; }
  String SSID() { return "MaBox"; } IPAddress localIP() { return {"192.168.1.42"}; } int RSSI() { return -58; }
  String softAPSSID() { return "BigFarm-central-a1b2c3"; } IPAddress softAPIP() { return {"192.168.4.1"}; }
};
extern WiFiClass WiFi;
