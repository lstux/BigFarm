#pragma once

#include <Arduino.h>

#include <type_traits>

namespace bf {

// Petit générateur JSON sans dépendance externe.
// Les clés sont passées à nullptr pour les éléments d'un tableau.
class JsonWriter {
 public:
  JsonWriter() {
    out_.reserve(1024);
    first_[0] = true;
  }

  JsonWriter &beginObject(const char *key = nullptr) {
    open(key, '{');
    return *this;
  }
  JsonWriter &endObject() {
    close('}');
    return *this;
  }
  JsonWriter &beginArray(const char *key = nullptr) {
    open(key, '[');
    return *this;
  }
  JsonWriter &endArray() {
    close(']');
    return *this;
  }

  JsonWriter &add(const char *key, const char *value) {
    prefix(key);
    if (value == nullptr) {
      out_ += "null";
    } else {
      appendEscaped(value);
    }
    return *this;
  }
  JsonWriter &add(const char *key, const String &value) { return add(key, value.c_str()); }
  JsonWriter &add(const char *key, bool value) {
    prefix(key);
    out_ += value ? "true" : "false";
    return *this;
  }
  JsonWriter &add(const char *key, double value, uint8_t decimals = 2) {
    prefix(key);
    char buf[32];
    snprintf(buf, sizeof buf, "%.*f", decimals, value);
    out_ += buf;
    return *this;
  }
  // Tous les entiers (signés ou non), sauf bool.
  template <typename T>
  typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, JsonWriter &>::type
  add(const char *key, T value) {
    prefix(key);
    char buf[24];
    if (std::is_signed<T>::value) {
      snprintf(buf, sizeof buf, "%lld", static_cast<long long>(value));
    } else {
      snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(value));
    }
    out_ += buf;
    return *this;
  }
  JsonWriter &addNull(const char *key) {
    prefix(key);
    out_ += "null";
    return *this;
  }

  const String &str() const { return out_; }

 private:
  static const uint8_t kMaxDepth = 12;

  void prefix(const char *key) {
    if (!first_[depth_]) out_ += ',';
    first_[depth_] = false;
    if (key != nullptr) {
      appendEscaped(key);
      out_ += ':';
    }
  }
  void open(const char *key, char c) {
    prefix(key);
    out_ += c;
    if (depth_ < kMaxDepth - 1) depth_++;
    first_[depth_] = true;
  }
  void close(char c) {
    out_ += c;
    if (depth_ > 0) depth_--;
  }
  void appendEscaped(const char *s) {
    out_ += '"';
    for (; *s; ++s) {
      const char c = *s;
      if (c == '"') {
        out_ += "\\\"";
      } else if (c == '\\') {
        out_ += "\\\\";
      } else if (static_cast<uint8_t>(c) < 0x20) {
        char buf[8];
        snprintf(buf, sizeof buf, "\\u%04x", static_cast<uint8_t>(c));
        out_ += buf;
      } else {
        out_ += c;
      }
    }
    out_ += '"';
  }

  String out_;
  bool first_[kMaxDepth];
  uint8_t depth_ = 0;
};

// Affiche un JSON compact sous une forme lisible (indentation) sur le port série.
void printJsonPretty(Print &out, const String &json);

}  // namespace bf
