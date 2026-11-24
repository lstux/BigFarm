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

// Écran : ST7701S 480x480, bus RGB 16 bits (RGB565) + bus SPI 3 fils pour l'initialisation.
// Brochage et timings repris de la définition « ESP32_4848S040_86BOX_GUITION »
// d'Arduino_GFX (v1.4.9), séquence d'initialisation « st7701_type9 ».
#define BOARD_LCD_WIDTH 480
#define BOARD_LCD_HEIGHT 480
#define BOARD_BACKLIGHT 38
// SPI 3 fils de configuration du ST7701S (partage SCK/MOSI avec la microSD,
// utilisé uniquement au démarrage, avant la microSD).
#define BOARD_LCD_SPI_CS 39
#define BOARD_LCD_SPI_SCK 48
#define BOARD_LCD_SPI_MOSI 47
// Bus RGB
#define BOARD_LCD_DE 18
#define BOARD_LCD_VSYNC 17
#define BOARD_LCD_HSYNC 16
#define BOARD_LCD_PCLK 21
#define BOARD_LCD_R0 11
#define BOARD_LCD_R1 12
#define BOARD_LCD_R2 13
#define BOARD_LCD_R3 14
#define BOARD_LCD_R4 0
#define BOARD_LCD_G0 8
#define BOARD_LCD_G1 20
#define BOARD_LCD_G2 3
#define BOARD_LCD_G3 46
#define BOARD_LCD_G4 9
#define BOARD_LCD_G5 10
#define BOARD_LCD_B0 4
#define BOARD_LCD_B1 5
#define BOARD_LCD_B2 6
#define BOARD_LCD_B3 7
#define BOARD_LCD_B4 15
#define BOARD_LCD_PCLK_HZ 12000000
#define BOARD_LCD_HSYNC_PULSE 8
#define BOARD_LCD_HSYNC_BACK 50
#define BOARD_LCD_HSYNC_FRONT 10
#define BOARD_LCD_VSYNC_PULSE 8
#define BOARD_LCD_VSYNC_BACK 20
#define BOARD_LCD_VSYNC_FRONT 10

// Tactile : miroirs éventuels par rapport à l'écran (à ajuster si le toucher est inversé).
#define BOARD_TOUCH_MIRROR_X 0
#define BOARD_TOUCH_MIRROR_Y 0
#define BOARD_TOUCH_SWAP_XY 0

// Sorties de la carte fille 3 voies (L1, L2, L3) d'après les configurations
// communautaires : GPIO 40, 2 et 1.
// SÉCURITÉ : le diagnostic ne touche PAS à ces broches. Rien ne sera piloté
// avant l'identification électrique de la carte fille (Jalon 0 / Jalon 6).
#define BOARD_OUTPUT_1 40
#define BOARD_OUTPUT_2 2
#define BOARD_OUTPUT_3 1
