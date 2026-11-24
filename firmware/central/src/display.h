#pragma once

#include <Arduino.h>
#include <BigFarmCore.h>

// Écran ST7701S (bus RGB) + tactile GT911 + LVGL 8.3.
namespace display {

// Initialise l'écran, le rétroéclairage, LVGL et le tactile.
// Le bus I2C du tactile (Wire) doit déjà être démarré.
// À appeler AVANT la microSD : les broches SCK/MOSI sont partagées.
bool begin();
// À appeler souvent dans loop() (moteur LVGL).
void loop();
bool ready();

// Luminosité du rétroéclairage, 10 à 255, enregistrée.
void setBrightness(uint8_t level);
uint8_t brightness();

// Rotation de l'affichage en degrés (0, 90, 180, 270), enregistrée.
bool setRotation(uint16_t degrees);
uint16_t rotation();

void addStatus(bf::JsonWriter &json);

}  // namespace display
