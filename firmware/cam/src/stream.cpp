#include "stream.h"

#include <WiFi.h>
#include <esp_camera.h>

#include "camera.h"

namespace stream {

static const char kBoundary[] = "bigfarmframe";

struct Slot {
  volatile bool used = false;
  IPAddress ip;
  uint32_t startedMs = 0;
  volatile uint32_t frames = 0;
  volatile float fps = 0;
};

static WiFiServer gServer(kPort);
static Slot gSlots[kMaxClients];
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

struct ClientArgs {
  WiFiClient client;
  int slot;
};

static int takeSlot() {
  int found = -1;
  portENTER_CRITICAL(&gMux);
  for (int i = 0; i < kMaxClients; i++) {
    if (!gSlots[i].used) {
      gSlots[i].used = true;
      found = i;
      break;
    }
  }
  portEXIT_CRITICAL(&gMux);
  return found;
}

static void releaseSlot(int i) {
  portENTER_CRITICAL(&gMux);
  gSlots[i].used = false;
  gSlots[i].fps = 0;
  portEXIT_CRITICAL(&gMux);
}

// Lit la ligne de requête ("GET /stream HTTP/1.1") et ignore les en-têtes.
static bool readRequest(WiFiClient &c, String &path) {
  const uint32_t start = millis();
  String line;
  bool firstLine = true;
  while (c.connected() && millis() - start < 3000) {
    if (!c.available()) {
      delay(5);
      continue;
    }
    const char ch = static_cast<char>(c.read());
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (line.length() < 256) line += ch;
      continue;
    }
    if (firstLine) {
      const int a = line.indexOf(' ');
      const int b = line.indexOf(' ', a + 1);
      if (a < 0 || b < 0) return false;
      path = line.substring(a + 1, b);
      firstLine = false;
    } else if (line.length() == 0) {
      return true;  // fin des en-têtes
    }
    line = "";
  }
  return false;
}

static bool writeAll(WiFiClient &c, const uint8_t *data, size_t len) {
  while (len > 0) {
    const size_t chunk = len > 8192 ? 8192 : len;
    const size_t n = c.write(data, chunk);
    if (n == 0) return false;
    data += n;
    len -= n;
  }
  return true;
}

static void clientTask(void *param) {
  ClientArgs *args = static_cast<ClientArgs *>(param);
  WiFiClient &c = args->client;
  Slot &slot = gSlots[args->slot];
  c.setNoDelay(true);

  String path;
  if (!readRequest(c, path)) {
    c.stop();
  } else if (!path.startsWith("/stream")) {
    c.print("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nUtiliser /stream\n");
    c.stop();
  } else if (!cam::ready()) {
    c.print("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "Camera indisponible\n");
    c.stop();
  } else {
    c.printf("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=%s\r\n"
             "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
             kBoundary);
    slot.ip = c.remoteIP();
    slot.startedMs = millis();
    slot.frames = 0;
    Serial.printf("[flux] client %s connecté\n", slot.ip.toString().c_str());

    uint32_t windowStart = millis();
    uint32_t windowFrames = 0;
    uint32_t lastFrame = 0;
    char header[96];
    while (c.connected()) {
      const uint8_t maxFps = cam::settings().maxFps;
      if (maxFps > 0) {
        const uint32_t interval = 1000 / maxFps;
        const uint32_t elapsed = millis() - lastFrame;
        if (elapsed < interval) delay(interval - elapsed);
      }
      lastFrame = millis();

      camera_fb_t *fb = esp_camera_fb_get();
      if (fb == nullptr) {
        delay(50);
        continue;
      }
      const int hlen = snprintf(header, sizeof header,
                                "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", kBoundary,
                                static_cast<unsigned>(fb->len));
      bool ok = writeAll(c, reinterpret_cast<const uint8_t *>(header), hlen);
      ok = ok && writeAll(c, fb->buf, fb->len);
      ok = ok && writeAll(c, reinterpret_cast<const uint8_t *>("\r\n"), 2);
      esp_camera_fb_return(fb);
      if (!ok) break;

      slot.frames++;
      windowFrames++;
      const uint32_t now = millis();
      if (now - windowStart >= 2000) {
        slot.fps = windowFrames * 1000.0f / (now - windowStart);
        windowStart = now;
        windowFrames = 0;
      }
    }
    c.stop();
    Serial.printf("[flux] client %s déconnecté après %lu images\n", slot.ip.toString().c_str(),
                  static_cast<unsigned long>(slot.frames));
  }

  releaseSlot(args->slot);
  delete args;
  vTaskDelete(nullptr);
}

static void acceptTask(void *) {
  for (;;) {
    WiFiClient c = gServer.available();
    if (c) {
      const int slot = takeSlot();
      if (slot < 0) {
        c.print("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
                "Trop de clients\n");
        c.stop();
      } else {
        ClientArgs *args = new ClientArgs();
        args->client = c;
        args->slot = slot;
        if (xTaskCreatePinnedToCore(clientTask, "bf-flux", 6144, args, 2, nullptr, 0) != pdPASS) {
          c.stop();
          releaseSlot(slot);
          delete args;
        }
      }
    }
    delay(20);
  }
}

void begin() {
  gServer.begin();
  xTaskCreatePinnedToCore(acceptTask, "bf-flux-accept", 4096, nullptr, 2, nullptr, 0);
  Serial.printf("[flux] serveur MJPEG sur le port %u (/stream)\n", kPort);
}

uint8_t clientCount() {
  uint8_t n = 0;
  for (int i = 0; i < kMaxClients; i++) {
    if (gSlots[i].used) n++;
  }
  return n;
}

void addStatus(bf::JsonWriter &j) {
  j.beginObject("flux");
  j.add("port", kPort);
  j.add("chemin", "/stream");
  j.add("clients", clientCount());
  j.beginArray("connexions");
  for (int i = 0; i < kMaxClients; i++) {
    if (!gSlots[i].used) continue;
    j.beginObject();
    j.add("ip", gSlots[i].ip.toString());
    j.add("images", static_cast<uint32_t>(gSlots[i].frames));
    j.add("ips", static_cast<double>(gSlots[i].fps), 1);
    j.add("depuis_s", (millis() - gSlots[i].startedMs) / 1000);
    j.endObject();
  }
  j.endArray();
  j.endObject();
}

}  // namespace stream
