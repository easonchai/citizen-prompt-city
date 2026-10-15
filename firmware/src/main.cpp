// Citizen badge host: ESP32-2424S012C. Owns time, touch, storage, WiFi; the core owns the pet and pixels.
// Pins come from platformio.ini build_flags (board facts cited there).
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <sys/time.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
extern "C" {
#include "citizen.h"
}

#ifndef CZ_WIFI
#define CZ_WIFI 1
#endif
#if CZ_WIFI
#include <WiFi.h>
#include <HTTPClient.h>
#endif

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel;
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;
  lgfx::Touch_CST816S touch;

 public:
  LGFX() {
    {
      auto c = bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = LCD_SPI_HZ;
      c.freq_read = 16000000;
      c.pin_sclk = LCD_SCLK;
      c.pin_mosi = LCD_MOSI;
      c.pin_miso = -1;
      c.pin_dc = LCD_DC;
      c.dma_channel = SPI_DMA_CH_AUTO;
      bus.config(c);
      panel.setBus(&bus);
    }
    {
      auto c = panel.config();
      c.pin_cs = LCD_CS;
      c.pin_rst = -1;
      c.pin_busy = -1;
      c.panel_width = CZ_W;
      c.panel_height = CZ_H;
      c.readable = false;
      c.invert = true;      // GC9A01 IPS panels need inversion on
      c.rgb_order = false;  // panel is BGR; LovyanGFX's GC9A01 default already accounts for it. Flip if red/blue swap.
      c.bus_shared = false;
      panel.config(c);
    }
    {
      auto c = light.config();
      c.pin_bl = LCD_BL;
      c.invert = false;  // active high
      c.freq = 44100;
      c.pwm_channel = 0;
      light.config(c);
      panel.setLight(&light);
    }
    {
      auto c = touch.config();
      c.x_min = 0;
      c.x_max = CZ_W - 1;
      c.y_min = 0;
      c.y_max = CZ_H - 1;
      c.pin_int = TOUCH_INT;
      c.pin_rst = TOUCH_RST;
      c.bus_shared = false;
      c.offset_rotation = 0;
      c.i2c_port = 0;
      c.i2c_addr = TOUCH_ADDR;
      c.pin_sda = TOUCH_SDA;
      c.pin_scl = TOUCH_SCL;
      c.freq = TOUCH_HZ;
      touch.config(c);
      panel.setTouch(&touch);
    }
    setPanel(&panel);
  }
};

static LGFX lcd;
static Preferences prefs;

// Slept time: the ESP32 RTC timer keeps gettimeofday() running through deep sleep (not through power loss),
// and RTC_DATA_ATTR survives deep sleep. So we stamp the RTC clock before sleeping and diff it on a
// deep-sleep wake. Cold boot or power loss: unknown, pass 0 (the save blob alone resumes the pet).
RTC_DATA_ATTR static int64_t rtc_sleep_stamp_s = 0;

static int64_t rtc_now_s() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec;
}

static uint8_t last_save[CZ_SAVE_SIZE];
static int last_save_len = 0;
static uint32_t last_save_ms = 0;
static int last_hearts = -1;

static void save_now() {
  uint8_t blob[CZ_SAVE_SIZE];
  int n = cz_save(blob, sizeof blob);
  last_save_ms = millis();
  if (n <= 0 || (n == last_save_len && memcmp(blob, last_save, n) == 0)) return;  // spare the flash
  prefs.putBytes("save", blob, n);
  memcpy(last_save, blob, n);
  last_save_len = n;
}

// --- display push: 8-bit indices -> RGB565 in 8-row chunks, ping-pong so DMA overlaps conversion ---
#define CHUNK_ROWS 8
static uint16_t line_buf[2][CZ_W * CHUNK_ROWS];  // 2 x 3,840 bytes

static void push_frame() {
  const uint8_t *fb = cz_framebuffer();
  const uint16_t *pal = cz_palette();
  uint16_t swapped[CZ_C_COUNT];  // LovyanGFX pushes uint16_t* as big-endian RGB565 (the SPI wire order)
  for (int i = 0; i < CZ_C_COUNT; i++) swapped[i] = (uint16_t)((pal[i] << 8) | (pal[i] >> 8));
  lcd.startWrite();
  for (int y = 0, b = 0; y < CZ_H; y += CHUNK_ROWS, b ^= 1) {
    uint16_t *dst = line_buf[b];
    const uint8_t *src = fb + y * CZ_W;
    for (int i = 0; i < CZ_W * CHUNK_ROWS; i++) dst[i] = swapped[src[i] < CZ_C_COUNT ? src[i] : 0];
    lcd.pushImageDMA(0, y, CZ_W, CHUNK_ROWS, dst);  // waits for the previous transfer, which used the other buffer
  }
  lcd.endWrite();  // waits for the last DMA
}

// --- touch -> raw core events ---
static bool touch_down = false;
static int touch_x, touch_y;

static void poll_touch() {
  lgfx::touch_point_t tp;
  bool now = lcd.getTouch(&tp, 1) > 0;
  uint32_t t = millis();
  if (now && !touch_down) {
    cz_touch(CZ_TOUCH_DOWN, tp.x, tp.y, t);
  } else if (now && (tp.x != touch_x || tp.y != touch_y)) {
    cz_touch(CZ_TOUCH_MOVE, tp.x, tp.y, t);
  } else if (!now && touch_down) {
    cz_touch(CZ_TOUCH_UP, touch_x, touch_y, t);
  }
  if (now) touch_x = tp.x, touch_y = tp.y;
  touch_down = now;
}

// --- WiFi + remote state ---
#if CZ_WIFI
static String state_url;
static String last_msg, last_city;
static uint32_t last_poll_ms = 0;

// Copies the string value of "key" from a flat JSON object into out. No escapes handled.
// shortcut: tiny hand parser for {"msg":"...","checkin":"KL"}; swap for a real parser if the payload grows.
static bool json_str(const String &body, const char *key, char *out, size_t cap) {
  String k = String("\"") + key + "\"";
  int i = body.indexOf(k);
  if (i < 0) return false;
  i = body.indexOf(':', i + k.length());
  if (i < 0) return false;
  i = body.indexOf('"', i);
  if (i < 0) return false;
  int j = body.indexOf('"', i + 1);
  if (j < 0) return false;
  String v = body.substring(i + 1, j);
  strlcpy(out, v.c_str(), cap);
  return true;
}

static void wifi_begin() {
  String ssid = prefs.getString("ssid", "");
  String url = prefs.getString("url", "");
  if (url.length()) state_url = url + "/state";
  if (!ssid.length()) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), prefs.getString("pass", "").c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(100);
  Serial.printf("wifi %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
}

static void wifi_poll() {
  if (!state_url.length() || millis() - last_poll_ms < 60000) return;
  last_poll_ms = millis();
  if (WiFi.status() != WL_CONNECTED) return;  // WiFi auto-reconnects in the background
  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(state_url)) return;
  if (http.GET() == 200) {
    String body = http.getString();
    char buf[CZ_MSG_MAX];
    // Only fire on change, or the same message would re-open Talk every minute.
    if (json_str(body, "msg", buf, sizeof buf) && buf[0] && last_msg != buf) last_msg = buf, cz_message(buf);
    if (json_str(body, "checkin", buf, sizeof buf) && buf[0] && last_city != buf) last_city = buf, cz_checkin(buf);
  }
  http.end();
}
#endif

// --- serial config over USB CDC ---
static char cmd[160];
static size_t cmd_len = 0;

static void run_command(char *line) {
  char *arg = strchr(line, ' ');
  if (arg) *arg++ = 0; else arg = line + strlen(line);
  if (!strcmp(line, "name")) { prefs.putString("name", arg); cz_set_name(arg); }
  else if (!strcmp(line, "creature")) { int c = atoi(arg); if (c < 0 || c >= CZ_CREATURES) { Serial.println("creature 0..2"); return; } prefs.putInt("creature", c); cz_set_creature(c); save_now(); }
  else if (!strcmp(line, "prompt")) { prefs.putString("prompt", arg); cz_set_prompt(arg); }
  else if (!strcmp(line, "url")) { prefs.putString("url", arg); cz_set_qr_url(arg); }
  else if (!strcmp(line, "msg")) cz_message(arg);
  else if (!strcmp(line, "checkin")) cz_checkin(*arg ? arg : "KL");
  else if (!strcmp(line, "wifi")) {
    char *pass = strchr(arg, ' ');
    if (pass) *pass++ = 0; else pass = arg + strlen(arg);
    prefs.putString("ssid", arg);
    prefs.putString("pass", pass);
    Serial.println("ok, restarting");
    delay(100);
    ESP.restart();
  }
  else if (!strcmp(line, "reset")) { prefs.clear(); rtc_sleep_stamp_s = 0; Serial.println("cleared, restarting"); delay(100); ESP.restart(); }
  else { Serial.println("commands: name X | creature N | prompt ... | wifi SSID PASS | url ... | msg ... | checkin [CITY] | reset"); return; }
  Serial.println("ok");
}

static void poll_serial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { cmd[cmd_len] = 0; if (cmd_len) run_command(cmd); cmd_len = 0; }
    else if (cmd_len < sizeof cmd - 1) cmd[cmd_len++] = c;
  }
}

static void go_to_sleep() {
  save_now();
  rtc_sleep_stamp_s = rtc_now_s();
  lcd.setBrightness(0);
  lcd.sleep();
  esp_deep_sleep_enable_gpio_wakeup(1ULL << TOUCH_INT, ESP_GPIO_WAKEUP_GPIO_LOW);  // CST816S INT pulls low on touch
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.setRotation(LCD_ROTATION);  // 4..7 are the mirrored variants if the panel shows mirrored
  lcd.setBrightness(255);
  prefs.begin("citizen", false);

  uint32_t slept = 0;
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED && rtc_sleep_stamp_s > 0) {
    int64_t d = rtc_now_s() - rtc_sleep_stamp_s;
    if (d > 0) slept = (uint32_t)d;
  }
  rtc_sleep_stamp_s = 0;

  uint8_t blob[CZ_SAVE_SIZE];
  size_t n = prefs.getBytesLength("save");
  if (n > sizeof blob) n = 0;
  if (n) n = prefs.getBytes("save", blob, n);
  cz_init(esp_random(), n ? blob : nullptr, (int)n, slept);
  if (n) memcpy(last_save, blob, n), last_save_len = (int)n;

  if (prefs.isKey("creature")) cz_set_creature(prefs.getInt("creature", 0));
  if (prefs.isKey("name")) cz_set_name(prefs.getString("name").c_str());
  if (prefs.isKey("prompt")) cz_set_prompt(prefs.getString("prompt").c_str());
  if (prefs.isKey("url")) cz_set_qr_url(prefs.getString("url").c_str());
  last_hearts = cz_hearts();
  last_save_ms = millis();

#if CZ_WIFI
  wifi_begin();
#endif
}

void loop() {
  poll_serial();
  poll_touch();
  cz_tick(millis());
#if CZ_WIFI
  wifi_poll();
#endif
  if (cz_dirty()) push_frame();

  int h = cz_hearts();
  if (h != last_hearts || millis() - last_save_ms >= 5 * 60 * 1000UL) {
    last_hearts = h;
    save_now();
  }
  if (cz_wants_sleep()) go_to_sleep();
  delay(16);
}
