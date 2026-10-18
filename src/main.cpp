#include <Arduino.h>
#include <Wire.h>

#include "config.h"

// Scanne le second bus I2C et affiche les périphériques trouvés.
static void scanBus(TwoWire &bus, const char *name) {
  Serial.printf("Scan I2C (%s)...\n", name);
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      Serial.printf("  0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  aucun périphérique");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("BigFarm - scan I2C");

  Wire1.begin(I2C_SENSORS_SDA, I2C_SENSORS_SCL, I2C_SENSORS_FREQ);
}

void loop() {
  scanBus(Wire1, "capteurs");
  delay(5000);
}
