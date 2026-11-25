#include "camera_client.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

namespace camclient {

static const char kPrefs[] = "bf-central";
static const uint16_t kStreamPort = 81;
static const size_t kJpegCapacity = 512 * 1024;
static const uint32_t kStallMs = 10000;
static const int kBuffers = 3;

static SemaphoreHandle_t gLock = nullptr;
static Status gStatus;
static volatile bool gWanted = false;
static volatile bool gRediscover = true;
static volatile bool gPhotoRequested = false;
static String gManualHost;
static String gPhotoResult;

// Tampons d'image (PSRAM) : un affiché, un en attente, un en cours d'écriture.
static uint16_t *gBuf[kBuffers] = {nullptr};
static uint16_t gBufW[kBuffers] = {0};
static uint16_t gBufH[kBuffers] = {0};
static uint32_t gBufSeq[kBuffers] = {0};
static int gFront = -1;
static int gPending = -1;
static uint32_t gSequence = 0;

static uint8_t *gJpeg = nullptr;
static JPEGDEC gDecoder;  // structure volumineuse : statique, pas sur la pile

// Cible du rappel de dessin du décodeur.
static uint16_t *gDrawTarget = nullptr;
static uint16_t gDrawW = 0;
static uint16_t gDrawH = 0;

class Lock {
 public:
  Lock() { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gLock); }
};

static void setState(State s, const String &message) {
  Lock l;
  gStatus.state = s;
  if (message.length()) gStatus.message = message;
}

const char *stateText(State s) {
  switch (s) {
    case STATE_SEARCHING: return "Recherche de la caméra…";
    case STATE_NOT_FOUND: return "Caméra introuvable";
    case STATE_IDLE: return "Caméra trouvée";
    case STATE_CONNECTING: return "Connexion au flux…";
    case STATE_LIVE: return "En direct";
    case STATE_ERROR: return "Flux interrompu";
    default: return "?";
  }
}

// ---------------------------------------------------------------------------
// Découverte

static String jsonField(const String &json, const char *key) {
  // Lecture minimale d'un champ texte "clé":"valeur" (réponses de nos modules).
  const String k = String("\"") + key + "\":\"";
  const int a = json.indexOf(k);
  if (a < 0) return String();
  const int start = a + k.length();
  const int end = json.indexOf('"', start);
  return end > start ? json.substring(start, end) : String();
}

static bool fetchInfo(const String &host, String &id, String &fw) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin("http://" + host + "/api/info")) return false;
  const int code = http.GET();
  bool ok = false;
  if (code == 200) {
    const String body = http.getString();
    if (jsonField(body, "type") == "cam") {
      id = jsonField(body, "identifiant");
      fw = jsonField(body, "firmware");
      ok = true;
    }
  }
  http.end();
  return ok;
}

static void discover() {
  gRediscover = false;
  if (WiFi.status() != WL_CONNECTED) {
    setState(STATE_NOT_FOUND, "Wi-Fi non connecté");
    return;
  }
  setState(STATE_SEARCHING, "");
  String host, id, fw;
  bool manual = false;
  {
    Lock l;
    host = gManualHost;
    manual = host.length() > 0;
  }
  if (!manual) {
    const int n = MDNS.queryService("bigfarm", "tcp");
    for (int i = 0; i < n; i++) {
      if (MDNS.txt(i, "type") == "cam") {
        host = MDNS.IP(i).toString();
        id = MDNS.txt(i, "id");
        fw = MDNS.txt(i, "fw");
        break;
      }
    }
  }
  if (host.length() == 0) {
    setState(STATE_NOT_FOUND, "aucune caméra annoncée sur le réseau");
    return;
  }
  String infoId, infoFw;
  if (fetchInfo(host, infoId, infoFw)) {
    id = infoId;
    fw = infoFw;
  } else if (manual) {
    setState(STATE_NOT_FOUND, "pas de réponse de " + host);
    // L'adresse manuelle est gardée : nouvel essai plus tard.
  }
  {
    Lock l;
    gStatus.host = host;
    gStatus.id = id;
    gStatus.firmware = fw;
    gStatus.manual = manual;
    gStatus.state = STATE_IDLE;
    gStatus.message = "";
  }
  Serial.printf("[camera] caméra %s à %s%s\n", id.c_str(), host.c_str(), manual ? " (adresse fixée)" : "");
}

// ---------------------------------------------------------------------------
// Photo

static void doPhoto(const String &host) {
  gPhotoRequested = false;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(8000);
  String result;
  if (!http.begin("http://" + host + "/api/photo")) {
    result = "Échec : adresse invalide";
  } else {
    const int code = http.POST("");
    const String body = code > 0 ? http.getString() : String();
    if (code == 200) {
      result = "Photo enregistrée : " + jsonField(body, "nom");
    } else if (code > 0) {
      const String err = jsonField(body, "erreur");
      result = "Échec : " + (err.length() ? err : String("code ") + String(code));
    } else {
      result = "Échec : caméra injoignable";
    }
    http.end();
  }
  Lock l;
  gPhotoResult = result;
}

// ---------------------------------------------------------------------------
// Décodage

static int drawCb(JPEGDRAW *d) {
  if (gDrawTarget == nullptr) return 0;
  const int used = d->iWidthUsed > 0 ? d->iWidthUsed : d->iWidth;
  if (d->x >= gDrawW) return 1;
  const int n = (d->x + used > gDrawW) ? gDrawW - d->x : used;
  for (int r = 0; r < d->iHeight; r++) {
    const int y = d->y + r;
    if (y >= gDrawH) break;
    memcpy(gDrawTarget + y * gDrawW + d->x, d->pPixels + r * d->iWidth, n * sizeof(uint16_t));
  }
  return 1;
}

static int pickWriteBuffer() {
  Lock l;
  for (int i = 0; i < kBuffers; i++) {
    if (i != gFront && i != gPending) return i;
  }
  return -1;
}

static bool decodeFrame(size_t len) {
  if (!gDecoder.openRAM(gJpeg, static_cast<int>(len), drawCb)) return false;
  const int w = gDecoder.getWidth();
  const int h = gDecoder.getHeight();
  int div = 1;
  int opts = 0;
  if (w > kMaxWidth || h > kMaxHeight) {
    div = 2;
    opts = JPEG_SCALE_HALF;
  }
  if (w / div > kMaxWidth || h / div > kMaxHeight) {
    div = 4;
    opts = JPEG_SCALE_QUARTER;
  }
  if (w / div > kMaxWidth || h / div > kMaxHeight) {
    div = 8;
    opts = JPEG_SCALE_EIGHTH;
  }
  const int idx = pickWriteBuffer();
  if (idx < 0) {
    gDecoder.close();
    return false;
  }
  gDrawTarget = gBuf[idx];
  gDrawW = w / div;
  gDrawH = h / div;
  gDecoder.setPixelType(RGB565_LITTLE_ENDIAN);
  const uint32_t t0 = millis();
  const int ok = gDecoder.decode(0, 0, opts);
  const uint32_t dt = millis() - t0;
  gDecoder.close();
  gDrawTarget = nullptr;
  if (!ok) return false;

  Lock l;
  gBufW[idx] = gDrawW;
  gBufH[idx] = gDrawH;
  gBufSeq[idx] = ++gSequence;
  gPending = idx;  // une image en attente non affichée est simplement remplacée
  gStatus.decodeMs = dt;
  gStatus.sourceWidth = w;
  gStatus.sourceHeight = h;
  return true;
}

// ---------------------------------------------------------------------------
// Flux MJPEG

static bool readLine(WiFiClient &c, String &line) {
  line = c.readStringUntil('\n');
  line.trim();
  return c.connected() || c.available();
}

static void streamSession(const String &host) {
  setState(STATE_CONNECTING, "");
  WiFiClient c;
  c.setTimeout(5);  // secondes
  IPAddress ip;
  if (!ip.fromString(host) || !c.connect(ip, kStreamPort, 3000)) {
    setState(STATE_ERROR, "connexion impossible à " + host + ":81");
    return;
  }
  c.print("GET /stream HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");

  String line;
  if (!readLine(c, line) || line.indexOf(" 200") < 0) {
    setState(STATE_ERROR, line.length() ? "réponse : " + line : String("pas de réponse"));
    c.stop();
    return;
  }
  // En-têtes HTTP jusqu'à la ligne vide.
  while (readLine(c, line) && line.length() > 0) {
  }

  uint32_t windowStart = millis();
  uint32_t windowFrames = 0;
  uint32_t lastFrame = millis();
  while (gWanted && c.connected()) {
    if (gPhotoRequested) doPhoto(host);

    // En-têtes de la partie suivante : on cherche Content-Length puis la ligne vide.
    size_t len = 0;
    bool headerDone = false;
    while (!headerDone) {
      if (!readLine(c, line)) break;
      if (line.startsWith("Content-Length:") || line.startsWith("content-length:")) {
        len = static_cast<size_t>(line.substring(15).toInt());
      } else if (line.length() == 0 && len > 0) {
        headerDone = true;
      }
      if (millis() - lastFrame > kStallMs) break;
    }
    if (!headerDone) break;

    if (len > kJpegCapacity) {
      // Image trop grande pour le tampon : on la saute.
      size_t left = len;
      uint8_t sink[512];
      while (left > 0 && c.connected()) {
        const size_t n = c.readBytes(sink, left > sizeof sink ? sizeof sink : left);
        if (n == 0) break;
        left -= n;
      }
      Lock l;
      gStatus.message = "image trop grande, baisser la résolution de la caméra";
      continue;
    }
    if (c.readBytes(gJpeg, len) != len) break;

    if (decodeFrame(len)) {
      lastFrame = millis();
      windowFrames++;
      Lock l;
      gStatus.state = STATE_LIVE;
      gStatus.frames++;
      gStatus.frameBytes = len;
      gStatus.lastFrameMs = lastFrame;
      gStatus.message = "";
    } else {
      Lock l;
      gStatus.errors++;
    }
    const uint32_t now = millis();
    if (now - windowStart >= 2000) {
      Lock l;
      gStatus.fps = windowFrames * 1000.0f / (now - windowStart);
      windowStart = now;
      windowFrames = 0;
    }
  }
  c.stop();
  if (gWanted) {
    setState(STATE_ERROR, "flux interrompu, nouvel essai…");
    Lock l;
    gStatus.fps = 0;
  } else {
    setState(STATE_IDLE, "");
    Lock l;
    gStatus.fps = 0;
  }
}

static void task(void *) {
  uint8_t failures = 0;
  uint32_t lastDiscover = 0;
  for (;;) {
    String host;
    State state;
    {
      Lock l;
      host = gStatus.host;
      state = gStatus.state;
    }
    // Recherche : au démarrage, sur demande, après plusieurs échecs, ou toutes les 15 s si introuvable.
    if (gRediscover || failures >= 3 || (state == STATE_NOT_FOUND && millis() - lastDiscover > 15000)) {
      failures = 0;
      lastDiscover = millis();
      discover();
      continue;
    }
    if (host.length() == 0 || state == STATE_NOT_FOUND) {
      delay(200);
      continue;
    }
    if (gPhotoRequested) doPhoto(host);
    if (!gWanted) {
      if (state != STATE_IDLE) setState(STATE_IDLE, "");
      delay(100);
      continue;
    }
    const uint32_t before = gStatus.frames;
    streamSession(host);
    if (gWanted) {
      failures = (gStatus.frames == before) ? failures + 1 : 0;
      delay(1500);
    }
  }
}

// ---------------------------------------------------------------------------

void begin() {
  gLock = xSemaphoreCreateMutex();
  Preferences p;
  p.begin(kPrefs, true);
  gManualHost = p.getString("cam_host", "");
  p.end();

  for (int i = 0; i < kBuffers; i++) {
    gBuf[i] = static_cast<uint16_t *>(
        heap_caps_malloc(kMaxWidth * kMaxHeight * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  gJpeg = static_cast<uint8_t *>(heap_caps_malloc(kJpegCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (gJpeg == nullptr || gBuf[0] == nullptr || gBuf[1] == nullptr || gBuf[2] == nullptr) {
    Serial.println("[camera] mémoire PSRAM insuffisante : vue caméra désactivée");
    gStatus.state = STATE_ERROR;
    gStatus.message = "mémoire insuffisante";
    return;
  }
  xTaskCreatePinnedToCore(task, "bf-camclient", 8192, nullptr, 2, nullptr, 0);
}

void setWanted(bool wanted) { gWanted = wanted; }

void setManualHost(const String &host) {
  Preferences p;
  p.begin(kPrefs, false);
  p.putString("cam_host", host);
  p.end();
  {
    Lock l;
    gManualHost = host;
    gStatus.host = "";
  }
  gRediscover = true;
}

void rediscover() { gRediscover = true; }

Status status() {
  Lock l;
  return gStatus;
}

bool takeFrame(uint32_t sequence, Frame &out) {
  Lock l;
  if (gPending >= 0) {
    gFront = gPending;
    gPending = -1;
  }
  if (gFront < 0 || gBufSeq[gFront] <= sequence) return false;
  out.pixels = gBuf[gFront];
  out.width = gBufW[gFront];
  out.height = gBufH[gFront];
  out.sequence = gBufSeq[gFront];
  return true;
}

void requestPhoto() {
  {
    Lock l;
    gPhotoResult = "";
  }
  gPhotoRequested = true;
}

String photoResult() {
  Lock l;
  return gPhotoResult;
}

void addStatus(bf::JsonWriter &j) {
  const Status s = status();
  j.beginObject("camera_distante");
  j.add("etat", stateText(s.state));
  j.add("adresse", s.host.length() ? s.host.c_str() : nullptr);
  j.add("adresse_fixee", s.manual);
  if (s.id.length()) j.add("identifiant", s.id);
  if (s.firmware.length()) j.add("firmware", s.firmware);
  j.add("flux_demande", static_cast<bool>(gWanted));
  j.add("images", s.frames);
  j.add("erreurs_decodage", s.errors);
  j.add("images_par_seconde", static_cast<double>(s.fps), 1);
  if (s.sourceWidth) {
    char res[16];
    snprintf(res, sizeof res, "%ux%u", s.sourceWidth, s.sourceHeight);
    j.add("resolution_source", res);
    j.add("decodage_ms", s.decodeMs);
    j.add("taille_image", bf::humanBytes(s.frameBytes));
  }
  if (s.message.length()) j.add("message", s.message);
  j.endObject();
}

}  // namespace camclient
