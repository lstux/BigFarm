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
