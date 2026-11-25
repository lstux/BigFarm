#pragma once
#include "Arduino.h"
namespace bf { class JsonWriter; bool timeValid(); String moduleId(); const char *firmwareVersion(); String nowText(); String humanBytes(uint64_t); }
