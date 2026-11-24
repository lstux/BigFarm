#include "camera.h"

#include <Preferences.h>

#include "board_esp32cam.h"

namespace cam {

static const char kPrefs[] = "bf-cam";
static const uint8_t kLedChannel = 2;  // canal LEDC distinct de celui de l'horloge caméra (canal 0)

struct Diag {
  bool ok = false;
  esp_err_t error = ESP_OK;
  String model;
  uint16_t pid = 0;
  uint8_t sccbAddress = 0;
  framesize_t sensorMax = FRAMESIZE_UXGA;
  framesize_t bufferMax = FRAMESIZE_CIF;
  bool jpeg = false;
  bool buffersInPsram = false;
  uint8_t bufferCount = 0;
};

struct Bench {
  bool done = false;
  framesize_t size = FRAMESIZE_SVGA;
  uint8_t requested = 0;
  uint8_t captured = 0;
  uint32_t avgMs = 0;
  size_t minBytes = 0;
  size_t maxBytes = 0;
  size_t avgBytes = 0;
};

static Diag gDiag;
static Bench gBench;
static Settings gSettings;

// Résolutions proposées dans l'interface, de la plus petite à la plus grande.
static const framesize_t kOffered[] = {FRAMESIZE_QQVGA, FRAMESIZE_QVGA, FRAMESIZE_CIF,  FRAMESIZE_HVGA,
                                       FRAMESIZE_VGA,   FRAMESIZE_SVGA, FRAMESIZE_XGA,  FRAMESIZE_HD,
                                       FRAMESIZE_SXGA,  FRAMESIZE_UXGA};

const char *resolutionName(framesize_t size) {
  switch (size) {
    case FRAMESIZE_QQVGA: return "QQVGA";
    case FRAMESIZE_QVGA: return "QVGA";
    case FRAMESIZE_CIF: return "CIF";
    case FRAMESIZE_HVGA: return "HVGA";
    case FRAMESIZE_VGA: return "VGA";
    case FRAMESIZE_SVGA: return "SVGA";
    case FRAMESIZE_XGA: return "XGA";
    case FRAMESIZE_HD: return "HD";
    case FRAMESIZE_SXGA: return "SXGA";
    case FRAMESIZE_UXGA: return "UXGA";
    default: return "?";
  }
}

String resolutionText(framesize_t size) {
  char buf[32];
  snprintf(buf, sizeof buf, "%ux%u", resolution[size].width, resolution[size].height);
  return String(buf);
}

bool ready() { return gDiag.ok; }
const Settings &settings() { return gSettings; }

framesize_t maxFrameSize() { return gDiag.sensorMax < gDiag.bufferMax ? gDiag.sensorMax : gDiag.bufferMax; }

static void loadSettings() {
  Preferences p;
  p.begin(kPrefs, true);
  Settings d;
  gSettings.frameSize = p.getUChar("taille", d.frameSize);
  gSettings.quality = p.getUChar("qualite", d.quality);
  gSettings.brightness = p.getChar("lum", d.brightness);
  gSettings.contrast = p.getChar("contr", d.contrast);
  gSettings.saturation = p.getChar("satur", d.saturation);
  gSettings.vflip = p.getBool("vflip", d.vflip);
  gSettings.hmirror = p.getBool("hmirror", d.hmirror);
  gSettings.awb = p.getBool("awb", d.awb);
  gSettings.aec = p.getBool("aec", d.aec);
  gSettings.agc = p.getBool("agc", d.agc);
  gSettings.led = p.getUChar("led", d.led);
  gSettings.maxFps = p.getUChar("ipsmax", d.maxFps);
  p.end();
  if (gSettings.frameSize > maxFrameSize()) gSettings.frameSize = maxFrameSize();
}

static void saveSettings() {
  Preferences p;
  p.begin(kPrefs, false);
  p.putUChar("taille", gSettings.frameSize);
  p.putUChar("qualite", gSettings.quality);
  p.putChar("lum", gSettings.brightness);
  p.putChar("contr", gSettings.contrast);
  p.putChar("satur", gSettings.saturation);
  p.putBool("vflip", gSettings.vflip);
  p.putBool("hmirror", gSettings.hmirror);
  p.putBool("awb", gSettings.awb);
  p.putBool("aec", gSettings.aec);
  p.putBool("agc", gSettings.agc);
  p.putUChar("led", gSettings.led);
  p.putUChar("ipsmax", gSettings.maxFps);
  p.end();
}

static void applySettings() {
  ledcWrite(kLedChannel, gSettings.led);
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) return;
  s->set_framesize(s, static_cast<framesize_t>(gSettings.frameSize));
  s->set_quality(s, gSettings.quality);
  s->set_brightness(s, gSettings.brightness);
  s->set_contrast(s, gSettings.contrast);
  s->set_saturation(s, gSettings.saturation);
  s->set_vflip(s, gSettings.vflip ? 1 : 0);
  s->set_hmirror(s, gSettings.hmirror ? 1 : 0);
  s->set_whitebal(s, gSettings.awb ? 1 : 0);
  s->set_exposure_ctrl(s, gSettings.aec ? 1 : 0);
  s->set_gain_ctrl(s, gSettings.agc ? 1 : 0);
}

bool begin() {
  // LED blanche pilotée en PWM (éteinte au départ).
  ledcSetup(kLedChannel, 5000, 8);
  ledcAttachPin(CAM_PIN_FLASH_LED, kLedChannel);
  ledcWrite(kLedChannel, 0);

  camera_config_t c = {};
  c.pin_pwdn = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;
  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_d7 = CAM_PIN_D7;
  c.pin_d6 = CAM_PIN_D6;
  c.pin_d5 = CAM_PIN_D5;
  c.pin_d4 = CAM_PIN_D4;
  c.pin_d3 = CAM_PIN_D3;
  c.pin_d2 = CAM_PIN_D2;
  c.pin_d1 = CAM_PIN_D1;
  c.pin_d0 = CAM_PIN_D0;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href = CAM_PIN_HREF;
  c.pin_pclk = CAM_PIN_PCLK;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    // Tampons dimensionnés pour UXGA : toutes les résolutions proposées passent.
    c.frame_size = FRAMESIZE_UXGA;
    c.jpeg_quality = 10;
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    c.frame_size = FRAMESIZE_CIF;
    c.jpeg_quality = 12;
    c.fb_count = 1;
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }
  gDiag.bufferMax = c.frame_size;
  gDiag.buffersInPsram = (c.fb_location == CAMERA_FB_IN_PSRAM);
  gDiag.bufferCount = c.fb_count;

  Serial.println("[cam] initialisation de la caméra...");
  gDiag.error = esp_camera_init(&c);
  if (gDiag.error != ESP_OK) {
    Serial.printf("[cam] ÉCHEC : 0x%x (%s)\n", gDiag.error, esp_err_to_name(gDiag.error));
    return false;
  }
  gDiag.ok = true;

  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {
    gDiag.pid = s->id.PID;
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&s->id);
    if (info != nullptr) {
      gDiag.model = info->name;
      gDiag.sccbAddress = info->sccb_addr;
      gDiag.sensorMax = info->max_size;
      gDiag.jpeg = info->support_jpeg;
    } else {
      gDiag.model = "capteur non répertorié";
    }
  }
  Serial.printf("[cam] capteur : %s (PID 0x%04x)\n", gDiag.model.c_str(), gDiag.pid);

  loadSettings();
  applySettings();
  return true;
}

bool update(const Settings &n, String &error) {
  if (n.frameSize > maxFrameSize()) {
    error = "résolution trop grande pour ce module";
    return false;
  }
  bool offered = false;
  for (size_t i = 0; i < sizeof(kOffered) / sizeof(kOffered[0]); i++) {
    if (kOffered[i] == n.frameSize) offered = true;
  }
  if (!offered) {
    error = "résolution inconnue";
    return false;
  }
  if (n.quality < 4 || n.quality > 63) {
    error = "qualité hors limites (4 à 63)";
    return false;
  }
  if (n.brightness < -2 || n.brightness > 2 || n.contrast < -2 || n.contrast > 2 || n.saturation < -2 ||
      n.saturation > 2) {
    error = "luminosité, contraste et saturation : -2 à 2";
    return false;
  }
  if (n.maxFps > 30) {
    error = "images/s max : 0 à 30";
    return false;
  }
  gSettings = n;
  applySettings();
  saveSettings();
  return true;
}

void benchmark(framesize_t size, uint8_t frames) {
  gBench = Bench();
  gBench.done = true;
  gBench.size = size;
  gBench.requested = frames;
  sensor_t *s = esp_camera_sensor_get();
  if (!gDiag.ok || s == nullptr) return;

  s->set_framesize(s, size);
  // Les premières images après un changement de résolution sont à jeter.
  for (int i = 0; i < 2; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }
  uint32_t totalMs = 0;
  size_t totalBytes = 0;
  gBench.minBytes = SIZE_MAX;
  for (uint8_t i = 0; i < frames; i++) {
    const uint32_t t0 = millis();
    camera_fb_t *fb = esp_camera_fb_get();
    const uint32_t dt = millis() - t0;
    if (fb == nullptr) continue;
    gBench.captured++;
    totalMs += dt;
    totalBytes += fb->len;
    if (fb->len < gBench.minBytes) gBench.minBytes = fb->len;
    if (fb->len > gBench.maxBytes) gBench.maxBytes = fb->len;
    esp_camera_fb_return(fb);
  }
  if (gBench.captured) {
    gBench.avgMs = totalMs / gBench.captured;
    gBench.avgBytes = totalBytes / gBench.captured;
  } else {
    gBench.minBytes = 0;
  }
  s->set_framesize(s, static_cast<framesize_t>(gSettings.frameSize));
  Serial.printf("[cam] mesure %s : %u/%u images, %lu ms/image, %u octets en moyenne\n",
                resolutionText(size).c_str(), gBench.captured, gBench.requested,
                static_cast<unsigned long>(gBench.avgMs), static_cast<unsigned>(gBench.avgBytes));
}

void addDiag(bf::JsonWriter &j) {
  j.beginObject("camera");
  j.add("etat", gDiag.ok ? "OK" : "ÉCHEC");
  if (!gDiag.ok) {
    j.add("erreur", esp_err_to_name(gDiag.error));
    j.endObject();
    return;
  }
  j.add("capteur", gDiag.model);
  char buf[8];
  snprintf(buf, sizeof buf, "0x%04x", gDiag.pid);
  j.add("pid", buf);
  snprintf(buf, sizeof buf, "0x%02x", gDiag.sccbAddress);
  j.add("adresse_sccb", buf);
  j.add("resolution_max_capteur", resolutionText(gDiag.sensorMax));
  j.add("resolution_max_utilisable", resolutionText(maxFrameSize()));
  j.add("jpeg_materiel", gDiag.jpeg);
  j.add("tampons", gDiag.bufferCount);
  j.add("tampons_en_psram", gDiag.buffersInPsram);
  j.add("resolution_reglee", resolutionText(static_cast<framesize_t>(gSettings.frameSize)));
  if (gBench.done) {
    j.beginObject("mesure_capture");
    j.add("resolution", resolutionText(gBench.size));
    j.add("images", String(gBench.captured) + "/" + String(gBench.requested));
    j.add("ms_par_image", gBench.avgMs);
    if (gBench.avgMs) j.add("images_par_seconde", 1000.0 / gBench.avgMs, 1);
    j.add("taille_moyenne", bf::humanBytes(gBench.avgBytes));
    j.add("taille_min", bf::humanBytes(gBench.minBytes));
    j.add("taille_max", bf::humanBytes(gBench.maxBytes));
    j.endObject();
  }
  j.endObject();
}

void addSettings(bf::JsonWriter &j) {
  j.beginObject("reglages");
  j.add("resolution", gSettings.frameSize);
  j.add("qualite", gSettings.quality);
  j.add("luminosite", gSettings.brightness);
  j.add("contraste", gSettings.contrast);
  j.add("saturation", gSettings.saturation);
  j.add("miroir_vertical", gSettings.vflip);
  j.add("miroir_horizontal", gSettings.hmirror);
  j.add("balance_blancs_auto", gSettings.awb);
  j.add("exposition_auto", gSettings.aec);
  j.add("gain_auto", gSettings.agc);
  j.add("led", gSettings.led);
  j.add("ips_max", gSettings.maxFps);
  j.endObject();
  j.beginArray("resolutions");
  for (size_t i = 0; i < sizeof(kOffered) / sizeof(kOffered[0]); i++) {
    if (kOffered[i] > maxFrameSize()) break;
    j.beginObject();
    j.add("id", static_cast<int>(kOffered[i]));
    j.add("nom", String(resolutionName(kOffered[i])) + " " + resolutionText(kOffered[i]));
    j.endObject();
  }
  j.endArray();
}

}  // namespace cam
