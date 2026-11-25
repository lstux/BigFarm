# Fiche matériel

À compléter avec les sorties des firmwares de diagnostic (`info` sur le port série ou `/api/diag`).
Légende : ✅ vérifié sur la carte · ❔ supposé (documentation communautaire) · ❌ inconnu.

## BigFarm Central — Guition ESP32-4848S040C_I_Y_V3

| Élément | Valeur | État | Source |
|---|---|---|---|
| Puce | ESP32-S3 | ❔ | étiquette du module |
| Flash | 16 Mo | ❔ | diag : `flash_detectee` |
| PSRAM | 8 Mo octale | ❔ | diag : `psram`, `test_psram` |
| Écran | ST7701S, 480×480, bus RGB | ❔ | fiche ESPHome |
| Tactile | GT911 sur I2C (SDA 19, SCL 45) | ❔ | diag : `tactile` |
| Rétroéclairage | GPIO 38 | ❔ | fiche ESPHome |
| microSD | SPI : CS 42, SCK 48, MOSI 47, MISO 41 | ❔ | diag : `microsd` |
| Lecteur SD (grand format) | rôle et brochage inconnus | ❌ | |
| Port série | convertisseur USB-UART | ❔ | |
| Alimentation, limites thermiques | | ❌ | |

## Carte fille 3 sorties (L1/L2/L3 + L/N)

**Ne rien raccorder au secteur avant que ce tableau soit complet.**

| Élément | Valeur | État |
|---|---|---|
| Technologie de commutation (relais, triacs…) | | ❌ |
| Références des composants de puissance (Q1-Q3) | | ❌ |
| Tension / courant admissibles | | ❌ |
| Isolation, protections (fusible, varistance) | | ❌ |
| Connecteur 8 broches : correspondance | GPIO 40, 2, 1 supposés | ❔ |
| Polarité de commande (actif haut/bas) | | ❌ |
| État des sorties au démarrage et au reset | à mesurer | ❌ |

## BigFarm Cam — ESP32-CAM HW-297

| Élément | Valeur | État | Source |
|---|---|---|---|
| Brochage caméra | AI Thinker | ❔ | diag : `camera.etat` |
| Capteur | OV2640 ? | ❔ | diag : `camera.capteur` |
| PSRAM | 4 Mo ? | ❔ | diag : `psram`, `test_psram` |
| Flash | 4 Mo ? | ❔ | diag : `flash_detectee` |
| microSD | SDMMC 1 bit | ❔ | diag : `microsd` |
| Flux SVGA (800×600) | plus de 15 img/s | ✅ | page Caméra, 2026-11-25 |
| Flux UXGA (1600×1200) | environ 2 à 3 img/s | ✅ (approx.) | page Caméra, 2026-11-25 |
| LED blanche | GPIO 4 | ❔ | |
| LED rouge | GPIO 33, active à l'état bas | ❔ | |

## Cartes mémoire

| Carte | Capacité | Format | Utilisée dans |
|---|---|---|---|
| | | | |

## Mesures caméra (Jalon 2)

Relevés sur la page Caméra du module (compteur img/s), Wi-Fi domestique.

| Date | Résolution | Images/s | Remarques |
|---|---|---|---|
| 2026-11-25 | SVGA 800×600 | > 15 | fluide |
| 2026-11-25 | UXGA 1600×1200 | ~2-3 | maximum du capteur, valeur approximative |

À compléter : VGA, XGA, HD, qualité JPEG, taille moyenne des images, et cadence affichée sur la centrale.
