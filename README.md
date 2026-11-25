# BigFarm

Système modulaire de surveillance et d'automatisation pour une petite culture indoor (mini-tente d'environ 60 × 120 cm au départ).

- **BigFarm Central** : carte Guition ESP32-4848S040 (ESP32-S3, écran tactile 4" 480×480). Interface principale (écran et web), coordination des modules, bibliothèque de médias.
- **BigFarm Cam** : ESP32-CAM HW-297. Flux, photos, timelapse. Réutilisable hors BigFarm.
- **Modules capteurs / actionneurs** : plus tard, via Wi-Fi/MQTT.

Le cadrage (v0.2) et le suivi des jalons sont dans le Notebook du projet. Les décisions techniques sont reprises dans [`docs/decisions.md`](docs/decisions.md).

## Phase actuelle : Jalon 0, inventaire matériel

On observe et on mesure le matériel avant d'automatiser. Les deux firmwares actuels sont des **firmwares de diagnostic** : ils lisent ce que la carte est vraiment (mémoire, capteur caméra, tactile, cartes SD) et l'affichent sur le port série et dans une page web. Les résultats alimentent [`docs/materiel.md`](docs/materiel.md).

## Organisation du dépôt

```
firmware/
  cam/        BigFarm Cam (ESP32-CAM)            projet PlatformIO
  central/    BigFarm Central (Guition ESP32-S3) projet PlatformIO
  common/     BigFarmCore : code partagé (identité, diagnostic, Wi-Fi, console, pages web)
docs/
  materiel.md   fiche matériel vérifiée (à compléter avec les diagnostics)
  decisions.md  décisions techniques
```

Le premier brouillon (squelette + scan I2C) est conservé dans la branche `archive/first-draft`.

## Compiler et flasher

Prérequis : [PlatformIO](https://platformio.org/) (CLI ou extension VS Code).

```sh
# Caméra (via l'adaptateur ESP32-CAM-MB)
pio run -d firmware/cam -t upload
pio device monitor -d firmware/cam

# Centrale
pio run -d firmware/central -t upload
pio device monitor -d firmware/central
```

Dans VS Code, ouvrir le dossier `firmware/cam` ou `firmware/central` comme projet PlatformIO.

## Utiliser un firmware de diagnostic

1. Flasher, puis ouvrir le moniteur série (115200 bauds) : le diagnostic complet s'affiche au démarrage.
2. Sans réseau enregistré, le module ouvre un point d'accès **`BigFarm-cam-xxxxxx`** ou **`BigFarm-central-xxxxxx`**, mot de passe **`bigfarm-setup`**. S'y connecter : la page de configuration Wi-Fi s'ouvre (sinon aller sur `http://192.168.4.1/wifi`).
   - Ou depuis le moniteur série : `wifi "Nom du réseau" motdepasse`.
3. Une fois connecté, le module est joignable sur `http://bf-cam-xxxxxx.local/` (ou son adresse IP, affichée sur le port série).
4. Page `/diag` : diagnostic lisible (sur la centrale : `/`). Page `/api/diag` : le même diagnostic en JSON.

### Module caméra

- `/` : page **Caméra** — flux en direct, photo, réglages (résolution, qualité, luminosité, contraste, saturation, miroirs, automatismes, LED blanche, limite d'images/s). Les réglages sont enregistrés dans le module et réappliqués au démarrage.
- `http://<module>:81/stream` : flux MJPEG brut (jusqu'à 3 clients simultanés, par exemple la centrale et un navigateur).
- `/capture` : une photo JPEG.
- `/api/camera` : réglages en JSON (GET) ; modification par POST (champs de formulaire : `resolution`, `qualite`, `luminosite`, `contraste`, `saturation`, `led`, `ips_max`, `miroir_vertical`, `miroir_horizontal`, `balance_blancs_auto`, `exposition_auto`, `gain_auto`).
- `/api/info` : identité et adresses du module, utilisées par la centrale. Le module s'annonce aussi en mDNS (service `_bigfarm._tcp`, champ `type=cam`).

Commandes série (taper `aide`) : `info`, `wifi`, `oublier`, `redemarrer`, plus `mesure` et `sd` sur la caméra, `i2c` et `sd` sur la centrale.

## Sécurité

Les sorties de la carte fille de la centrale (L1/L2/L3, secteur) **ne sont pas pilotées** par le code actuel. Aucune charge ne doit y être raccordée avant l'identification électrique de la carte (voir `docs/materiel.md`).
