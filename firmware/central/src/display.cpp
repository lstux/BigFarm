#include "display.h"

#include <Preferences.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

#include "board_guition_4848s040.h"

namespace display {

static const char kPrefs[] = "bf-ui";
static const uint8_t kBacklightChannel = 0;
static const uint16_t kBufferLines = 40;

static esp_lcd_panel_handle_t gPanel = nullptr;
static lv_disp_draw_buf_t gDrawBuf;
static lv_disp_drv_t gDispDrv;
static lv_indev_drv_t gIndevDrv;
static lv_disp_t *gDisp = nullptr;
static bool gReady = false;
static uint8_t gBrightness = 200;
static uint16_t gRotation = 0;
static uint8_t gTouchAddr = 0;
static uint32_t gTouches = 0;

// ---------------------------------------------------------------------------
// Initialisation du ST7701S par SPI 3 fils (9 bits : bit D/C puis 8 bits).

struct InitCmd {
  uint8_t cmd;
  uint8_t len;  // 0xFF : pause de `delayMs` millisecondes
  uint8_t delayMs;
  uint8_t data[16];
};

// Séquence « st7701_type9 » d'Arduino_GFX v1.4.9 (Guition ESP32-4848S040).
static const InitCmd kInit[] = {
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x10}},
    {0xC0, 2, 0, {0x3B, 0x00}},
    {0xC1, 2, 0, {0x0D, 0x02}},
    {0xC2, 2, 0, {0x31, 0x05}},
    {0xCD, 1, 0, {0x00}},
    {0xB0, 16, 0, {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}},
    {0xB1, 16, 0, {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x11}},
    {0xB0, 1, 0, {0x60}},
    {0xB1, 1, 0, {0x32}},
    {0xB2, 1, 0, {0x07}},
    {0xB3, 1, 0, {0x80}},
    {0xB5, 1, 0, {0x49}},
    {0xB7, 1, 0, {0x85}},
    {0xB8, 1, 0, {0x21}},
    {0xC1, 1, 0, {0x78}},
    {0xC2, 1, 0, {0x78}},
    {0xE0, 3, 0, {0x00, 0x1B, 0x02}},
    {0xE1, 11, 0, {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}},
    {0xE2, 12, 0, {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}},
    {0xE3, 4, 0, {0x00, 0x00, 0x11, 0x11}},
    {0xE4, 2, 0, {0x44, 0x44}},
    {0xE5, 16, 0, {0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}},
    {0xE6, 4, 0, {0x00, 0x00, 0x11, 0x11}},
    {0xE7, 2, 0, {0x44, 0x44}},
    {0xE8, 16, 0, {0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}},
    {0xEB, 7, 0, {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}},
    {0xEC, 2, 0, {0x3C, 0x00}},
    {0xED, 16, 0, {0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x13}},
    {0xE5, 1, 0, {0xE4}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x00}},
    {0x3A, 1, 0, {0x60}},
    {0x11, 0, 0, {}},        // sortie de veille
    {0x00, 0xFF, 120, {}},   // pause 120 ms
    {0x29, 0, 0, {}},        // affichage allumé
};

static void spiWrite9(bool isData, uint8_t value) {
  const uint16_t word = (isData ? 0x100 : 0x000) | value;
  for (int bit = 8; bit >= 0; bit--) {
    digitalWrite(BOARD_LCD_SPI_SCK, LOW);
    digitalWrite(BOARD_LCD_SPI_MOSI, (word >> bit) & 1);
    delayMicroseconds(1);
    digitalWrite(BOARD_LCD_SPI_SCK, HIGH);  // lecture sur front montant
    delayMicroseconds(1);
  }
}

static void st7701Init() {
  pinMode(BOARD_LCD_SPI_CS, OUTPUT);
  pinMode(BOARD_LCD_SPI_SCK, OUTPUT);
  pinMode(BOARD_LCD_SPI_MOSI, OUTPUT);
  digitalWrite(BOARD_LCD_SPI_CS, HIGH);
  digitalWrite(BOARD_LCD_SPI_SCK, LOW);
  delay(10);
  for (size_t i = 0; i < sizeof(kInit) / sizeof(kInit[0]); i++) {
    const InitCmd &c = kInit[i];
    if (c.len == 0xFF) {
      delay(c.delayMs);
      continue;
    }
    digitalWrite(BOARD_LCD_SPI_CS, LOW);
    spiWrite9(false, c.cmd);
    for (uint8_t d = 0; d < c.len; d++) spiWrite9(true, c.data[d]);
    digitalWrite(BOARD_LCD_SPI_SCK, LOW);
    digitalWrite(BOARD_LCD_SPI_CS, HIGH);
    delayMicroseconds(10);
  }
  // CS reste à l'état haut : le ST7701S ignore ensuite le trafic SPI de la microSD.
}

static bool rgbPanelInit() {
  esp_lcd_rgb_panel_config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.clk_src = LCD_CLK_SRC_PLL160M;
  cfg.timings.pclk_hz = BOARD_LCD_PCLK_HZ;
  cfg.timings.h_res = BOARD_LCD_WIDTH;
  cfg.timings.v_res = BOARD_LCD_HEIGHT;
  cfg.timings.hsync_pulse_width = BOARD_LCD_HSYNC_PULSE;
  cfg.timings.hsync_back_porch = BOARD_LCD_HSYNC_BACK;
  cfg.timings.hsync_front_porch = BOARD_LCD_HSYNC_FRONT;
  cfg.timings.vsync_pulse_width = BOARD_LCD_VSYNC_PULSE;
  cfg.timings.vsync_back_porch = BOARD_LCD_VSYNC_BACK;
  cfg.timings.vsync_front_porch = BOARD_LCD_VSYNC_FRONT;
  cfg.timings.flags.hsync_idle_low = 0;
  cfg.timings.flags.vsync_idle_low = 0;
  cfg.timings.flags.de_idle_high = 0;
  cfg.timings.flags.pclk_active_neg = 0;
  cfg.timings.flags.pclk_idle_high = 0;
  cfg.data_width = 16;
  cfg.sram_trans_align = 8;
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = BOARD_LCD_HSYNC;
  cfg.vsync_gpio_num = BOARD_LCD_VSYNC;
  cfg.de_gpio_num = BOARD_LCD_DE;
  cfg.pclk_gpio_num = BOARD_LCD_PCLK;
  // RGB565 : bits 0-4 bleu, 5-10 vert, 11-15 rouge.
  const int data[16] = {BOARD_LCD_B0, BOARD_LCD_B1, BOARD_LCD_B2, BOARD_LCD_B3, BOARD_LCD_B4, BOARD_LCD_G0,
                        BOARD_LCD_G1, BOARD_LCD_G2, BOARD_LCD_G3, BOARD_LCD_G4, BOARD_LCD_G5, BOARD_LCD_R0,
                        BOARD_LCD_R1, BOARD_LCD_R2, BOARD_LCD_R3, BOARD_LCD_R4};
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
  cfg.disp_gpio_num = -1;
  cfg.flags.disp_active_low = 0;
  cfg.flags.relax_on_idle = 0;
  cfg.flags.fb_in_psram = 1;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &gPanel);
  if (err == ESP_OK) err = esp_lcd_panel_reset(gPanel);
  if (err == ESP_OK) err = esp_lcd_panel_init(gPanel);
  if (err != ESP_OK) {
    Serial.printf("[ecran] ÉCHEC du bus RGB : %s\n", esp_err_to_name(err));
    gPanel = nullptr;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// LVGL

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color) {
  esp_lcd_panel_draw_bitmap(gPanel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color);
  lv_disp_flush_ready(drv);
}

// GT911 : registre 0x814E = état (bit 7 : données prêtes, bits 0-3 : nombre de points),
// premier point à partir de 0x8150 (X puis Y, poids faible d'abord).
static bool gt911Read(uint16_t reg, uint8_t *out, uint8_t len) {
  Wire.beginTransmission(gTouchAddr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gTouchAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

static void gt911Clear() {
  Wire.beginTransmission(gTouchAddr);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0x00);
  Wire.endTransmission();
}

static void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  static int16_t lastX = 0;
  static int16_t lastY = 0;
  data->state = LV_INDEV_STATE_REL;
  if (gTouchAddr != 0) {
    uint8_t status = 0;
    if (gt911Read(0x814E, &status, 1) && (status & 0x80)) {
      const uint8_t points = status & 0x0F;
      if (points > 0 && points <= 5) {
        uint8_t p[4];
        if (gt911Read(0x8150, p, 4)) {
          int16_t x = p[0] | (p[1] << 8);
          int16_t y = p[2] | (p[3] << 8);
#if BOARD_TOUCH_SWAP_XY
          const int16_t t = x;
          x = y;
          y = t;
#endif
#if BOARD_TOUCH_MIRROR_X
          x = BOARD_LCD_WIDTH - 1 - x;
#endif
#if BOARD_TOUCH_MIRROR_Y
          y = BOARD_LCD_HEIGHT - 1 - y;
#endif
          lastX = constrain(x, 0, BOARD_LCD_WIDTH - 1);
          lastY = constrain(y, 0, BOARD_LCD_HEIGHT - 1);
          data->state = LV_INDEV_STATE_PR;
          gTouches++;
        }
      }
      gt911Clear();
    } else if (status & 0x80) {
      gt911Clear();
    }
  }
  data->point.x = lastX;
  data->point.y = lastY;
}

static lv_disp_rot_t toLvRotation(uint16_t deg) {
  switch (deg) {
    case 90: return LV_DISP_ROT_90;
    case 180: return LV_DISP_ROT_180;
    case 270: return LV_DISP_ROT_270;
    default: return LV_DISP_ROT_NONE;
  }
}

// ---------------------------------------------------------------------------

bool ready() { return gReady; }
uint8_t brightness() { return gBrightness; }
uint16_t rotation() { return gRotation; }

void setBrightness(uint8_t level) {
  if (level < 10) level = 10;
  gBrightness = level;
  ledcWrite(kBacklightChannel, level);
  Preferences p;
  p.begin(kPrefs, false);
  p.putUChar("lum", level);
  p.end();
}

bool setRotation(uint16_t deg) {
  if (deg != 0 && deg != 90 && deg != 180 && deg != 270) return false;
  gRotation = deg;
  if (gDisp != nullptr) lv_disp_set_rotation(gDisp, toLvRotation(deg));
  Preferences p;
  p.begin(kPrefs, false);
  p.putUShort("rot", deg);
  p.end();
  return true;
}

bool begin() {
  Preferences p;
  p.begin(kPrefs, true);
  gBrightness = p.getUChar("lum", 200);
  gRotation = p.getUShort("rot", 0);
  p.end();

  // Rétroéclairage éteint pendant l'initialisation (évite le flash d'image aléatoire).
  ledcSetup(kBacklightChannel, 5000, 8);
  ledcAttachPin(BOARD_BACKLIGHT, kBacklightChannel);
  ledcWrite(kBacklightChannel, 0);

  Serial.println("[ecran] initialisation du ST7701S...");
  st7701Init();
  if (!rgbPanelInit()) return false;

  lv_init();
  const size_t pixels = BOARD_LCD_WIDTH * kBufferLines;
  lv_color_t *buf1 = static_cast<lv_color_t *>(heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  lv_color_t *buf2 = static_cast<lv_color_t *>(heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (buf1 == nullptr || buf2 == nullptr) {
    // Repli : tampons en PSRAM (plus lent mais fonctionnel).
    if (buf1 == nullptr) buf1 = static_cast<lv_color_t *>(heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    if (buf2 == nullptr) buf2 = static_cast<lv_color_t *>(heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
  }
  if (buf1 == nullptr) {
    Serial.println("[ecran] mémoire insuffisante pour LVGL");
    return false;
  }
  lv_disp_draw_buf_init(&gDrawBuf, buf1, buf2, pixels);

  lv_disp_drv_init(&gDispDrv);
  gDispDrv.hor_res = BOARD_LCD_WIDTH;
  gDispDrv.ver_res = BOARD_LCD_HEIGHT;
  gDispDrv.flush_cb = flushCb;
  gDispDrv.draw_buf = &gDrawBuf;
  gDispDrv.sw_rotate = 1;
  gDispDrv.rotated = toLvRotation(gRotation);
  gDisp = lv_disp_drv_register(&gDispDrv);

  // Tactile : GT911 à l'une de ses deux adresses possibles.
  const uint8_t candidates[2] = {BOARD_GT911_ADDR_A, BOARD_GT911_ADDR_B};
  for (int i = 0; i < 2 && gTouchAddr == 0; i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) gTouchAddr = candidates[i];
  }
  if (gTouchAddr) {
    Serial.printf("[ecran] tactile GT911 à 0x%02x\n", gTouchAddr);
  } else {
    Serial.println("[ecran] tactile GT911 non trouvé");
  }
  lv_indev_drv_init(&gIndevDrv);
  gIndevDrv.type = LV_INDEV_TYPE_POINTER;
  gIndevDrv.read_cb = touchCb;
  lv_indev_drv_register(&gIndevDrv);

  gReady = true;
  // Premier rendu (écran noir), puis rétroéclairage.
  lv_timer_handler();
  ledcWrite(kBacklightChannel, gBrightness);
  Serial.println("[ecran] prêt");
  return true;
}

void loop() {
  if (gReady) lv_timer_handler();
}

void addStatus(bf::JsonWriter &j) {
  j.beginObject("ecran");
  j.add("etat", gReady ? "OK" : "non initialisé");
  j.add("controleur", "ST7701S (RGB 16 bits)");
  j.add("resolution", "480x480");
  j.add("luminosite", gBrightness);
  j.add("rotation", gRotation);
  if (gTouchAddr) {
    char a[8];
    snprintf(a, sizeof a, "0x%02x", gTouchAddr);
    j.add("tactile", a);
  } else {
    j.add("tactile", "non trouvé");
  }
  j.add("lectures_tactiles", gTouches);
  j.endObject();
}

}  // namespace display
