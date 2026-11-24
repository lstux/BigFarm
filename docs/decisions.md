# Décisions techniques

Les décisions sont datées. Une décision peut être revue : on ajoute alors une nouvelle entrée plutôt que de réécrire l'ancienne.

## 2026-11-25 — Reprise du dépôt sur le cadrage v0.2

- **Répartition du travail** : le code est écrit par Claude ; l'UX/UI est travaillée avec ChatGPT ; le propriétaire du projet teste sur le matériel et tranche.
- **Ancien contenu** : le premier brouillon (squelette PlatformIO + scan I2C) est archivé dans la branche `archive/first-draft`. `main` repart sur une structure propre.
- **Pile logicielle** : PlatformIO + framework Arduino-ESP32 pour tous les modules. LVGL pour l'écran tactile de la centrale (à venir). Pilote `esp32-camera` d'Espressif pour la caméra.
- **Structure** : un projet PlatformIO par module (`firmware/cam`, `firmware/central`) et une bibliothèque partagée `firmware/common/BigFarmCore`, liée par `symlink://`.
- **Carte fille des 3 sorties** : on la **garde** en v1 (3 actionneurs internes une fois validés électriquement). Le second bus I2C sur ses GPIO (40, 2, 1) est abandonné ; les capteurs iront sur un module de mesure séparé (MQTT). Le second bus I2C reste un plan B.
- **Identité des modules** : identifiant `bf-<type>-<6 derniers chiffres hexa de la MAC>`, ex. `bf-cam-a1b2c3`. Sert de nom réseau (mDNS) et de nom de point d'accès (`BigFarm-cam-a1b2c3`).
- **Wi-Fi** : identifiants en NVS. Point d'accès de configuration (mot de passe `bigfarm-setup`) quand aucun réseau n'est connu ou joignable ; retour automatique en mode station quand le réseau revient.
- **Langue** : français dans le code commenté, la documentation et les interfaces.
- **Premier pas** : firmwares de diagnostic pour la caméra et la centrale (Jalon 0).

## 2026-11-25 — Écran de la centrale

- **Pilote d'écran maison** sur `esp_lcd` (bus RGB d'ESP-IDF 4.4), sans Arduino_GFX : initialisation du ST7701S par SPI 3 fils logiciel, séquence et timings repris de la définition Guition d'Arduino_GFX 1.4.9. Moins de dépendances, et contrôle direct du tampon d'image en PSRAM.
- **LVGL 8.3.11** (version figée), polices Montserrat régénérées avec les accents français et les symboles LVGL (14, 18, 24, 32 px).
- **Thème sombre**, couleur d'accent du projet (#1FBF8F), orange pour « attention », rouge pour « erreur ». Les états ne reposent jamais sur la couleur seule (texte toujours présent).
- **Rotation logicielle** réglable (0/90/180/270°) : l'orientation physique du boîtier n'est pas connue à l'avance.
- **Sorties de la carte fille** affichées mais désactivées tant que la carte n'est pas validée.
- **Simulateur PC** (`tools/ui-sim`) : rend l'interface en PNG sans la carte, pour itérer sur l'UI.
