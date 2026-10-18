# BigFarm

Gestionnaire de culture d'intérieur basé sur une carte **Guition ESP32-4848S040** (ESP32-S3, écran tactile IPS 4" 480×480, 16 Mo de flash, 8 Mo de PSRAM).

## Objectifs

- **Capteurs** : température, humidité de l'air, pression (BME280), humidité du sol et autres entrées analogiques (ADS1115), horloge temps réel (DS3231).
- **Actionneurs** : ventilation, irrigation, éclairage… pilotés via une extension GPIO I2C (PCF8575) et des relais.
- **Journalisation** sur carte SD.
- **Interface** : écran tactile intégré avec courbes, et interface web.

## Matériel

| Élément | Détail |
|---|---|
| Carte | Guition ESP32-4848S040 (ESP32-S3-WROOM N16R8) |
| Écran / tactile | ST7701 480×480 (bus RGB) / GT911 (I2C sur GPIO19 SDA, GPIO45 SCL) |
| Capteurs I2C | BME280, ADS1115, DS3231 |
| Extension GPIO | PCF8575 (I2C) |
| Stockage | Lecteur microSD intégré |

### Second bus I2C

Le bus du tactile (GPIO19/45) reste dédié au GT911. Les capteurs sont placés sur un **second bus I2C** (`Wire1`), repris sur les broches libérées par le retrait de la carte 3 relais (GPIO 40, 2 et 1 d'après les configurations communautaires). Le choix des deux broches est à confirmer par des mesures sur la carte réelle : voir `include/config.h`.

Les modules capteurs doivent être alimentés en **3,3 V** (l'ESP32-S3 n'est pas tolérant 5 V).

Adresses I2C attendues : BME280 `0x76`/`0x77`, ADS1115 `0x48`–`0x4B`, DS3231 `0x68` (EEPROM `0x57`), PCF8575 `0x20`–`0x27`.

## Développement

Le projet utilise [PlatformIO](https://platformio.org/).

```sh
pio run                 # compiler
pio run -t upload       # flasher
pio device monitor      # moniteur série (115200 bauds)
```

Le premier programme (`src/main.cpp`) scanne le bus I2C et affiche les adresses trouvées sur le port série.

## État

- [x] Squelette du projet
- [ ] Validation du brochage du second bus I2C
- [ ] Lecture BME280 / ADS1115 / DS3231
- [ ] Pilotage du PCF8575 (relais)
- [ ] Affichage (ST7701 + LVGL) et tactile (GT911)
- [ ] Journalisation SD
- [ ] Interface web
