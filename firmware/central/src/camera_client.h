#pragma once

#include <Arduino.h>
#include <BigFarmCore.h>

// Client de la caméra BigFarm, côté centrale.
//
// - Découverte : mDNS (service _bigfarm._tcp, type=cam) puis /api/info du module,
//   ou adresse fixée à la main (commande série « camera <ip> », enregistrée).
// - Flux : lecture du MJPEG http://<caméra>:81/stream dans une tâche dédiée (cœur 0),
//   décodage JPEG (JPEGDEC) en RGB565 dans des tampons en PSRAM.
// - Le flux n'est lu que lorsqu'un écran en a besoin (setWanted).
namespace camclient {

enum State {
  STATE_SEARCHING = 0,  // recherche de la caméra
  STATE_NOT_FOUND,      // aucune caméra trouvée (nouvel essai périodique)
  STATE_IDLE,           // caméra connue, flux non demandé
  STATE_CONNECTING,     // connexion au flux
  STATE_LIVE,           // images reçues
  STATE_ERROR,          // flux interrompu (nouvel essai automatique)
};

struct Status {
  State state = STATE_SEARCHING;
  String host;  // adresse IP de la caméra
  String id;    // identifiant du module (bf-cam-xxxxxx)
  String firmware;
  bool manual = false;  // adresse fixée à la main
  float fps = 0;
  uint32_t decodeMs = 0;
  uint32_t frameBytes = 0;
  uint16_t sourceWidth = 0;
  uint16_t sourceHeight = 0;
  uint32_t frames = 0;
  uint32_t errors = 0;
  uint32_t lastFrameMs = 0;  // millis() de la dernière image, 0 si aucune
  String message;            // dernière erreur ou information
};

// Image décodée prête à afficher (RGB565, petit-boutiste, comme LVGL).
struct Frame {
  const uint16_t *pixels = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t sequence = 0;
};

void begin();

// Demande (ou non) la lecture du flux.
void setWanted(bool wanted);

// Adresse fixe ("192.168.1.50") ou vide pour revenir à la découverte automatique.
void setManualHost(const String &host);
// Relance immédiatement la recherche.
void rediscover();

Status status();
const char *stateText(State s);

// Récupère la dernière image décodée si elle est plus récente que `sequence`.
// Le tampon reste valide jusqu'à l'appel suivant de takeFrame().
bool takeFrame(uint32_t sequence, Frame &out);

// Demande à la caméra d'enregistrer une photo sur sa microSD (asynchrone).
void requestPhoto();
// Résultat de la dernière demande de photo ("" si aucune ou en cours).
String photoResult();

// Résolutions proposées par la caméra (lues sur /api/camera) et réglage courant.
// Le réglage est celui de la caméra : il vaut pour le flux ET pour les photos.
int resolutionCount();
String resolutionName(int index);  // ex. « SVGA 800x600 »
int resolutionId(int index);
int currentResolutionId();         // -1 si inconnue
// Demande à la caméra de changer de résolution (asynchrone).
void setResolution(int id);

void addStatus(bf::JsonWriter &json);

// Taille maximale d'une image affichée (le décodeur réduit 1/2, 1/4 ou 1/8 au besoin).
static const uint16_t kMaxWidth = 456;
static const uint16_t kMaxHeight = 300;

}  // namespace camclient
