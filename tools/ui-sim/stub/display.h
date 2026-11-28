#pragma once
#include <cstdint>
namespace display { uint8_t brightness(); uint16_t rotation(); void setBrightness(uint8_t); void previewBrightness(uint8_t); bool setRotation(uint16_t); void loop(); }
