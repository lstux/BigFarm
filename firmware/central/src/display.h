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

// Luminosité du rétroéclairage en pourcentage (5 à 100), enregistrée.
// Le pourcentage est réparti entre le seuil minimal (en dessous, l'écran reste noir)
// et le maximum : voir setBacklightMin().
void setBrightness(uint8_t percent);
uint8_t brightness();
// Application immédiate sans enregistrement (pendant qu'on bouge le curseur).
void previewBrightness(uint8_t percent);

// Calibrage du rétroéclairage (commande série « retro ») :
// - rapport cyclique brut 0-255, temporaire, pour trouver le seuil ;
void setBacklightRaw(uint8_t duty);
// - seuil minimal (rapport cyclique correspondant à 0 %), enregistré ;
void setBacklightMin(uint8_t duty);
uint8_t backlightMin();
// - fréquence du signal PWM en Hz (100 à 40000), enregistrée.
bool setBacklightFrequency(uint32_t hz);
uint32_t backlightFrequency();

// Rotation de l'affichage en degrés (0, 90, 180, 270), enregistrée.
bool setRotation(uint16_t degrees);
uint16_t rotation();

void addStatus(bf::JsonWriter &json);

}  // namespace display
