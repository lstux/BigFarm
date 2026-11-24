#pragma once

// Brochage de la Guition ESP32-4848S040.
//
// Source : fiches communautaires (ESPHome, HomeDing), à CONFIRMER sur notre
// révision ESP32-4848S040C_I_Y_V3. Le firmware de diagnostic vérifie ce qu'il
// peut (tactile, microSD) sans rien piloter de risqué.

// Bus I2C du tactile (GT911). Ce bus est dédié au tactile.
#define BOARD_TOUCH_SDA 19
#define BOARD_TOUCH_SCL 45
#define BOARD_TOUCH_I2C_FREQ 400000
// Adresses possibles du GT911 (dépend de l'état de la broche INT au reset).
#define BOARD_GT911_ADDR_A 0x5D
#define BOARD_GT911_ADDR_B 0x14

// Lecteur microSD (bus SPI).
#define BOARD_SD_CS 42
#define BOARD_SD_MOSI 47
#define BOARD_SD_SCK 48
#define BOARD_SD_MISO 41

// Rétroéclairage de l'écran (non piloté par le diagnostic).
#define BOARD_BACKLIGHT 38

// Sorties de la carte fille 3 voies (L1, L2, L3) d'après les configurations
// communautaires : GPIO 40, 2 et 1.
// SÉCURITÉ : le diagnostic ne touche PAS à ces broches. Rien ne sera piloté
// avant l'identification électrique de la carte fille (Jalon 0 / Jalon 6).
#define BOARD_OUTPUT_1 40
#define BOARD_OUTPUT_2 2
#define BOARD_OUTPUT_3 1
