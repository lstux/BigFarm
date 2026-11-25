#pragma once
#include <string>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
struct String : std::string {
  String() {}
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  const char *c_str() const { return std::string::c_str(); }
  size_t length() const { return size(); }
  void trim() {
    size_t b = find_first_not_of(" \t\r\n");
    size_t e = find_last_not_of(" \t\r\n");
    *this = (b == npos) ? String() : String(substr(b, e - b + 1));
  }
};
inline String operator+(const String &a, const String &b) { return String(static_cast<const std::string &>(a) + static_cast<const std::string &>(b)); }
inline String operator+(const String &a, const char *b) { return a + String(b); }
inline String operator+(const char *a, const String &b) { return String(a) + b; }
uint32_t millis();
struct EspClass { uint32_t getFreeHeap() { return 180000; } uint32_t getFreePsram() { return 7000000; } void restart() {} };
extern EspClass ESP;
