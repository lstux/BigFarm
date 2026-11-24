#pragma once

// Brochage de l'ESP32-CAM HW-297.
//
// Hypothèse : brochage AI Thinker, le plus courant pour cette famille de cartes.
// À CONFIRMER : si la caméra ne s'initialise pas (erreur 0x105, capteur non
// trouvé), le brochage diffère et il faudra l'identifier sur la carte.

// Caméra
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1  // non câblé
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26  // SCCB (I2C de la caméra), données
#define CAM_PIN_SIOC 27  // SCCB, horloge
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

// LED blanche (flash). Elle partage GPIO4 avec la ligne DATA1 du lecteur microSD :
// la carte est utilisée en mode SDMMC 1 bit pour laisser la LED libre.
#define CAM_PIN_FLASH_LED 4

// LED rouge au dos de la carte, active à l'état bas.
#define CAM_PIN_STATUS_LED 33
