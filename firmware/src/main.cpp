// Citizen badge host: ESP32-2424S012C. Owns time, touch, storage, WiFi; the core owns the pet and pixels.
// Pins come from platformio.ini build_flags (board facts cited there).
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
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
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#endif
#ifndef CZ_VERSION
#define CZ_VERSION "0.0.0"
#endif
#ifndef CZ_DEFAULT_BASE
#error "build with -DCZ_DEFAULT_BASE (see platformio.ini)"
#endif

// F10: colour order and orientation. If red/blue are swapped build with -DCZ_RGB_ORDER=1;
// if the image is mirrored or rotated use -DCZ_ROTATION=0..7 (4..7 are the mirrored variants).
#ifndef CZ_RGB_ORDER
#define CZ_RGB_ORDER 0
#endif
#ifndef CZ_ROTATION
#define CZ_ROTATION 0
#endif
#define BOOT_PIN 9

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
      c.rgb_order = CZ_RGB_ORDER;  // panel is BGR; LovyanGFX's GC9A01 default already accounts for it. See F10 above.
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

// F1: push only rows y0..y1 (inclusive, from cz_dirty_rows). Returns rows pushed.
static int push_rows(int y0, int y1) {
  const uint8_t *fb = cz_framebuffer();
  const uint16_t *pal = cz_palette();
  uint16_t swapped[CZ_C_COUNT];  // LovyanGFX pushes uint16_t* as big-endian RGB565 (the SPI wire order)
  for (int i = 0; i < CZ_C_COUNT; i++) swapped[i] = (uint16_t)((pal[i] << 8) | (pal[i] >> 8));
  lcd.startWrite();
  for (int y = y0, b = 0; y <= y1; y += CHUNK_ROWS, b ^= 1) {
    int h = y1 + 1 - y < CHUNK_ROWS ? y1 + 1 - y : CHUNK_ROWS;
    uint16_t *dst = line_buf[b];
    const uint8_t *src = fb + y * CZ_W;
    for (int i = 0; i < CZ_W * h; i++) dst[i] = swapped[src[i] < CZ_C_COUNT ? src[i] : 0];
    lcd.pushImageDMA(0, y, CZ_W, h, dst);  // waits for the previous transfer, which used the other buffer
  }
  lcd.endWrite();  // waits for the last DMA
  return y1 + 1 - y0;
}

// --- F2/F3 telemetry: one JSON line per second. A "frame" is a loop pass that pushed pixels;
// frame_ms = tick + render + push, push_ms = convert + SPI, rows = largest frame in the window. ---
static uint32_t tl_t0, tl_frames, tl_frame_us, tl_push_us;
static int tl_rows;
static char tl_line[200];
static const char *wifi_state();
static int wifi_rssi();

static const char *telemetry_line() {
  uint32_t el = millis() - tl_t0;
  snprintf(tl_line, sizeof tl_line,
           "{\"t\":%lu,\"fps\":%lu,\"frame_ms\":%.2f,\"push_ms\":%.2f,\"rows\":%d,\"heap\":%lu,"
           "\"screen\":%d,\"hearts\":%d,\"wifi\":\"%s\",\"rssi\":%d}",
           (unsigned long)millis(), (unsigned long)(el ? tl_frames * 1000UL / el : 0),
           tl_frames ? tl_frame_us / 1000.0 / tl_frames : 0.0, tl_frames ? tl_push_us / 1000.0 / tl_frames : 0.0,
           tl_rows, (unsigned long)ESP.getFreeHeap(), cz_screen(), cz_hearts(), wifi_state(), wifi_rssi());
  return tl_line;
}

static void telemetry_tick() {
  if (millis() - tl_t0 < 1000) return;
  Serial.println(telemetry_line());
  tl_t0 = millis();
  tl_frames = tl_frame_us = tl_push_us = 0;
  tl_rows = 0;
}

// --- touch -> raw core events ---
static bool touch_down = false;
static int touch_x, touch_y;
static uint32_t last_touch_ms = 0;  // F11: idle gate for the automatic OTA install

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
  if (now || touch_down) last_touch_ms = t;
  if (now) touch_x = tp.x, touch_y = tp.y;
  touch_down = now;
}

static void poll_serial();

// F6: server base + slug. Default slug is the creature's default name, lowercase (mirrors DEFAULT_NAME in core/citizen.c).
static const char *const DEFAULT_SLUG[CZ_CREATURES] = {"tenuk", "kenyalang", "harimau"};
static String pref_base() { return prefs.getString("base", CZ_DEFAULT_BASE); }
static String pref_slug() { return prefs.getString("slug", DEFAULT_SLUG[cz_creature()]); }
static void set_qr() { cz_set_qr_url((pref_base() + "/c/" + pref_slug()).c_str()); }

// --- F8 clock: system time is UTC epoch once SNTP has synced (0-based on cold boot until then). ---
static bool time_valid() { return time(nullptr) > 1700000000; }
static int local_minute(long utc_minute) { return (int)((((utc_minute + cz_timezone() * 60L) % 1440) + 1440) % 1440); }
static void clock_from_system() {
  if (time_valid()) cz_set_clock(local_minute(time(nullptr) / 60));
}
static int known_tz = 0;
static void set_tz(int h) {
  cz_set_timezone(h);
  known_tz = cz_timezone();
  prefs.putInt("tz", known_tz);
  clock_from_system();
}

// --- WiFi + remote state ---
#if CZ_WIFI
static bool wifi_started = false;
static uint32_t wifi_t0 = 0;
static uint32_t last_poll_ms = 0;
static bool first_sync_done = false;  // gates deep sleep so an idle badge still syncs once per wake
// Re-fire guard: last seen msg updatedAt and checkin survive deep sleep (RTC) and power loss (Preferences).
RTC_DATA_ATTR static char rtc_last_upd[48];
RTC_DATA_ATTR static char rtc_last_city[CZ_MSG_MAX];

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

// "kl,sg,tyo" -> bit per enum cz_sky. Unknown names are ignored, so the server can add skies before the firmware.
static unsigned skies_mask(const char *list) {
  static const char *const names[CZ_SKIES] = {"kl", "sg", "tyo"};
  unsigned mask = 0;
  for (int i = 0; i < CZ_SKIES; i++) {
    const char *p = strstr(list, names[i]);
    size_t n = strlen(names[i]);
    while (p && !((p == list || p[-1] == ',') && (p[n] == ',' || !p[n]))) p = strstr(p + 1, names[i]);
    if (p) mask |= 1u << i;
  }
  return mask;
}

// Non-blocking (F9): the pet runs while WiFi connects. "fail" after 8 s; WiFi keeps retrying in the background.
static void wifi_begin() {
  if (!rtc_last_upd[0]) strlcpy(rtc_last_upd, prefs.getString("last_upd", "").c_str(), sizeof rtc_last_upd);
  if (!rtc_last_city[0]) strlcpy(rtc_last_city, prefs.getString("last_city", "").c_str(), sizeof rtc_last_city);
  String ssid = prefs.getString("ssid", "");
  if (!ssid.length()) { first_sync_done = true; return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), prefs.getString("pass", "").c_str());
  wifi_started = true;
  wifi_t0 = millis();
}

static const char *wifi_state() {
  if (!wifi_started) return "off";
  if (WiFi.status() == WL_CONNECTED) return "ok";
  return millis() - wifi_t0 < 8000 ? "connecting" : "fail";
}
static int wifi_rssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

// F8: tells the core when the WiFi state changes; on connect starts SNTP.
static void wifi_watch() {
  static int last = -1;
  const char *s = wifi_state();
  int st = s[0] == 'o' ? (s[1] == 'k' ? CZ_WIFI_OK : CZ_WIFI_OFF) : s[0] == 'c' ? CZ_WIFI_CONNECTING : CZ_WIFI_FAIL;
  if (st == last) return;
  last = st;
  cz_set_wifi(prefs.getString("ssid", "").c_str(), st);
  if (st == CZ_WIFI_OK) configTime(cz_timezone() * 3600L, 0, "pool.ntp.org");
}

// 3-part numeric semver compare: >0 if a is newer than b. Missing parts count as 0; suffixes are ignored.
static int semver_cmp(const char *a, const char *b) {
  int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
  sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] > y[i] ? 1 : -1;
  return 0;
}

// F11: GET <base>/api/firmware/latest -> {"version","url","sha256"}. Tells the core whether a newer build exists.
static String update_url;
static bool check_update() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.setTimeout(3000);
  bool ok = false;
  if (http.begin(pref_base() + "/api/firmware/latest")) {
    if (http.GET() == 200) {
      String body = http.getString();
      char ver[16], url[200];
      if (json_str(body, "version", ver, sizeof ver) && json_str(body, "url", url, sizeof url)) {
        bool newer = semver_cmp(ver, CZ_VERSION) > 0;  // never offer an older or equal build
        update_url = newer ? url : "";
        cz_set_update(newer ? ver : "");
        ok = true;
      }
    }
    http.end();
  }
  return ok;
}

// F11 OTA into the other app slot, then reboot. Prefs live in nvs, untouched by OTA.
// shortcut: setInsecure() skips TLS cert checks (no CA bundle in V1); pin a CA bundle before wider release.
// shortcut: latest.json's sha256 is not checked (HTTPUpdate streams straight into Update with no hash hook);
// the ESP-IDF image header + appended SHA-256 are still validated by Update before the slot is switched.
static void install_update() {
  if (!update_url.length()) {
    if (!check_update()) { cz_message("UPDATE FAILED"); return; }
    if (!update_url.length()) return;  // up to date; Settings already says so
  }
  save_now();
  cz_message("UPDATING...");
  cz_tick(millis());
  int y0, y1;
  if (cz_dirty_rows(&y0, &y1)) push_rows(y0, y1);
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return r = httpUpdate.update(client, update_url, CZ_VERSION);
  if (r != HTTP_UPDATE_OK) {
    Serial.printf("update failed: %s\n", httpUpdate.getLastErrorString().c_str());
    cz_message("UPDATE FAILED");
  }
}

// F6: poll once right after connecting (or after the 8 s connect timeout), then every 60 s while awake.
static void wifi_poll() {
  if (!wifi_started) return;
  bool connected = WiFi.status() == WL_CONNECTED;
  bool first = !first_sync_done;
  if (first) {
    if (!connected && millis() - wifi_t0 < 8000) return;
  } else if (millis() - last_poll_ms < 60000) return;
  first_sync_done = true;
  last_poll_ms = millis();
  if (!connected) return;
  String api = pref_base() + "/api/c/" + pref_slug();
  HTTPClient http;
  http.setTimeout(3000);
  if (http.begin(api + "/state")) {
    if (http.GET() == 200) {
      String body = http.getString();
      char buf[CZ_MSG_MAX], upd[sizeof rtc_last_upd];
      // Fire only when updatedAt / checkin change, or an old message would re-open Talk every poll and wake.
      if (json_str(body, "updatedAt", upd, sizeof upd) && upd[0] && strcmp(upd, rtc_last_upd)) {
        strlcpy(rtc_last_upd, upd, sizeof rtc_last_upd);
        prefs.putString("last_upd", upd);
        if (json_str(body, "msg", buf, sizeof buf) && buf[0]) cz_message(buf);
      }
      if (json_str(body, "checkin", buf, sizeof buf) && buf[0] && strcmp(buf, rtc_last_city)) {
        strlcpy(rtc_last_city, buf, sizeof rtc_last_city);
        prefs.putString("last_city", buf);
        cz_checkin(buf);
      }
      // skies the badge has unlocked; kept in prefs so a badge offline at boot still draws them
      if (json_str(body, "skies", buf, sizeof buf)) {
        unsigned mask = skies_mask(buf);
        if (mask != prefs.getUInt("skies", 1)) prefs.putUInt("skies", mask);
        cz_set_skies(mask);
      }
      if (json_str(body, "name", buf, sizeof buf) && buf[0] && prefs.getString("name", "") != buf) {
        prefs.putString("name", buf);
        cz_set_name(buf);
      }
      // "now": ISO 8601 UTC, e.g. 2026-10-31T09:41:00.000Z. Fallback clock until SNTP syncs.
      if (!time_valid() && json_str(body, "now", buf, sizeof buf) && strlen(buf) >= 16 && buf[10] == 'T' &&
          isdigit(buf[11]) && isdigit(buf[12]) && buf[13] == ':' && isdigit(buf[14]) && isdigit(buf[15]))
        cz_set_clock(local_minute(((buf[11] - '0') * 10 + buf[12] - '0') * 60 + (buf[14] - '0') * 10 + buf[15] - '0'));
    }
    http.end();
  }
  clock_from_system();  // re-anchor to SNTP once a minute
  // F11: after the first state poll of each wake (deep sleep reboots, resetting these), then at most hourly.
  static bool update_checked = false;
  static uint32_t update_check_ms = 0;
  if ((!update_checked || millis() - update_check_ms >= 3600000UL) && check_update()) {
    update_checked = true;
    update_check_ms = millis();
  }
  if (!first && tl_line[0] && http.begin(api + "/telemetry")) {  // F6: last F3 line, once a minute while awake
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Badge-Key", prefs.getString("token", ""));  // server answers 401 without it
    http.POST((uint8_t *)tl_line, strlen(tl_line));
    http.end();
  }
}

// --- F5: captive-portal provisioning. Blocks until the form is saved, then reboots. ---
static void run_portal() {
  uint8_t mac[6];
  WiFi.mode(WIFI_AP);
  WiFi.softAPmacAddress(mac);
  char ap[16];
  snprintf(ap, sizeof ap, "CITIZEN-%02X%02X", mac[4], mac[5]);
  WiFi.softAP(ap);
  DNSServer dns;
  dns.start(53, "*", WiFi.softAPIP());  // every hostname resolves to 192.168.4.1
  WebServer web(80);
  web.on("/", HTTP_GET, [&] {
    web.send(200, "text/html",
             "<!doctype html><meta name=viewport content='width=device-width'><title>Citizen</title>"
             "<form method=post action=/save style='font:18px sans-serif;max-width:320px;margin:auto'>"
             "<h2>Citizen WiFi</h2><p>WiFi name<br><input name=ssid maxlength=32 required></p>"
             "<p>Password<br><input name=pass type=password maxlength=64></p>"
             "<p>Server<br><input name=base type=url maxlength=128 placeholder='" CZ_DEFAULT_BASE "'></p>"
             "<p>Slug<br><input name=slug maxlength=32 placeholder='tenuk'></p>"
             "<p>Device key<br><input name=token maxlength=64 placeholder='from the admin badge page'></p>"
             "<button>Save</button></form>");
  });
  web.on("/save", HTTP_POST, [&] {
    String ssid = web.arg("ssid"), pass = web.arg("pass"), base = web.arg("base"), slug = web.arg("slug"), token = web.arg("token");
    if (!ssid.length() || ssid.length() > 32 || pass.length() > 64 || base.length() > 128 || slug.length() > 32 || token.length() > 64) {
      web.send(400, "text/plain", "Check the fields and try again.");
      return;
    }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    if (base.length()) prefs.putString("base", base);
    if (slug.length()) prefs.putString("slug", slug);
    if (token.length()) prefs.putString("token", token);
    web.send(200, "text/html", "<meta name=viewport content='width=device-width'><h2>Saved. Citizen is restarting.</h2>");
    delay(500);
    ESP.restart();
  });
  web.onNotFound([&] {  // captive-portal probes (generate_204, hotspot-detect) land here
    web.sendHeader("Location", "http://192.168.4.1/");
    web.send(302, "text/plain", "");
  });
  web.begin();
  Serial.printf("portal: join %s, open http://192.168.4.1\n", ap);
  char msg[CZ_MSG_MAX];
  snprintf(msg, sizeof msg, "JOIN WIFI %s", ap);
  int y0, y1;
  for (;;) {
    if (cz_screen() != CZ_S_TALK) cz_message(msg);  // keep the AP name on screen
    dns.processNextRequest();
    web.handleClient();
    poll_serial();
    cz_tick(millis());
    if (cz_dirty_rows(&y0, &y1)) push_rows(y0, y1);
    delay(10);
  }
}
#else
static const char *wifi_state() { return "off"; }
static int wifi_rssi() { return 0; }
static const bool first_sync_done = true;
#endif

// --- serial config over USB CDC ---
static char cmd[160];
static size_t cmd_len = 0;

static void run_command(char *line) {
  char *arg = strchr(line, ' ');
  if (arg) *arg++ = 0; else arg = line + strlen(line);
  if (!strcmp(line, "name")) { prefs.putString("name", arg); cz_set_name(arg); }
  else if (!strcmp(line, "creature")) { int c = atoi(arg); if (c < 0 || c >= CZ_CREATURES) { Serial.println("creature 0..2"); return; } prefs.putInt("creature", c); cz_set_creature(c); set_qr(); save_now(); }
  else if (!strcmp(line, "prompt")) { prefs.putString("prompt", arg); cz_set_prompt(arg); }
  else if (!strcmp(line, "url")) { prefs.putString("base", arg); set_qr(); }
  else if (!strcmp(line, "slug")) { prefs.putString("slug", arg); set_qr(); }
  else if (!strcmp(line, "token")) { prefs.putString("token", arg); }  // device key from /admin/badges/<slug>, sent as X-Badge-Key
  else if (!strcmp(line, "msg")) cz_message(arg);
  else if (!strcmp(line, "checkin")) cz_checkin(*arg ? arg : "KL");
  else if (!strcmp(line, "skies")) { unsigned m = skies_mask(arg); prefs.putUInt("skies", m); cz_set_skies(m); }
  else if (!strcmp(line, "wifi")) {
    char *pass = strchr(arg, ' ');
    if (pass) *pass++ = 0; else pass = arg + strlen(arg);
    prefs.putString("ssid", arg);
    prefs.putString("pass", pass);
    Serial.println("ok, restarting");
    delay(100);
    ESP.restart();
  }
  else if (!strcmp(line, "brightness")) { cz_set_brightness(atoi(arg)); save_now(); }  // core keeps it in the save blob
  else if (!strcmp(line, "stats")) { Serial.println(telemetry_line()); return; }
  else if (!strcmp(line, "time")) {
    int h, m;
    if (sscanf(arg, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) { Serial.println("time HH:MM"); return; }
    cz_set_clock(h * 60 + m); save_now();
  }
  else if (!strcmp(line, "tz")) {
    int h = atoi(arg);
    if (!*arg || h < -12 || h > 14) { Serial.println("tz -12..14"); return; }
    set_tz(h); save_now();
  }
  else if (!strcmp(line, "version")) { Serial.println(CZ_VERSION); return; }
#if CZ_WIFI
  else if (!strcmp(line, "update")) { update_url = ""; install_update(); }
  else if (!strcmp(line, "autoupdate")) { prefs.putInt("autoupd", atoi(arg) ? 1 : 0); }
#endif
  else if (!strcmp(line, "reset")) { prefs.clear(); rtc_sleep_stamp_s = 0;
#if CZ_WIFI
    rtc_last_upd[0] = rtc_last_city[0] = 0;
#endif
    Serial.println("cleared, restarting"); delay(100); ESP.restart(); }
  else { Serial.println("commands: name X | creature N | prompt ... | wifi SSID PASS | url BASE | slug S | token KEY | msg ... | checkin [CITY] | skies kl,sg,tyo | brightness 100|40|10 | time HH:MM | tz N | version | update | autoupdate 0|1 | stats | reset"); return; }
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

// Applies the core's brightness (F8) to the backlight PWM when it changes. -1 forces a re-apply.
static int applied_brightness = -1;
// Settings changes the host must persist: load_pet re-applies the 'creature'/'name' prefs on every wake.
static int known_creature = -1, known_brightness = -1, known_always_on = -1, known_sky = -1;
static void apply_brightness() {
  int b = cz_brightness();
  if (b == applied_brightness) return;
  applied_brightness = b;
  lcd.setBrightness(b * 255 / 100);
}

// Restores the pet from Preferences and re-applies the host-owned content (name, prompt, ...).
static void load_pet(uint32_t slept) {
  uint8_t blob[CZ_SAVE_SIZE];
  size_t n = prefs.getBytesLength("save");
  if (n > sizeof blob) n = 0;
  if (n) n = prefs.getBytes("save", blob, n);
  cz_init(esp_random(), n ? blob : nullptr, (int)n, slept);
  if (n) memcpy(last_save, blob, n), last_save_len = (int)n;
  if (prefs.isKey("creature")) cz_set_creature(prefs.getInt("creature", 0));
  if (prefs.isKey("name")) cz_set_name(prefs.getString("name").c_str());
  if (prefs.isKey("prompt")) cz_set_prompt(prefs.getString("prompt").c_str());
  cz_set_timezone(prefs.getInt("tz", 8));
  cz_set_skies(prefs.getUInt("skies", 1));  // after cz_init: the blob remembers which skies were already shown
  known_tz = cz_timezone();
  cz_set_version(CZ_VERSION);
  known_creature = cz_creature();
  known_brightness = cz_brightness();
  known_always_on = cz_always_on();
  known_sky = cz_sky();
  clock_from_system();  // after deep sleep the RTC kept SNTP time
  set_qr();
  last_hearts = cz_hearts();
  last_save_ms = millis();
}

// F4 sleep. Default: deep sleep (~5 uA), woken by the CST816S INT line going low; the chip reboots
// and setup() resumes the pet with the slept seconds. ESP32-C3 deep sleep can only wake on GPIO 0..5,
// so BOOT (GPIO 9) cannot wake deep sleep; RESET does.
// -DCZ_LIGHT_SLEEP_FALLBACK: use if level wake from deep sleep proves unreliable on hardware.
// Light sleep (~0.13 mA est., RAM kept) wakes on touch INT low, BOOT low, or a 2 s timer; on a timer
// wake we poll the touch controller and go back to sleep if nothing is pressed. USB CDC drops while asleep.
static void go_to_sleep() {
  save_now();
  rtc_sleep_stamp_s = rtc_now_s();
  lcd.setBrightness(0);
  lcd.sleep();
#ifdef CZ_LIGHT_SLEEP_FALLBACK
  for (;;) {
    esp_sleep_enable_timer_wakeup(2000000ULL);
    gpio_wakeup_enable((gpio_num_t)TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BOOT_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    lgfx::touch_point_t tp;
    if (digitalRead(TOUCH_INT) == LOW || digitalRead(BOOT_PIN) == LOW || lcd.getTouch(&tp, 1) > 0) break;
  }
  gpio_wakeup_disable((gpio_num_t)TOUCH_INT);
  gpio_wakeup_disable((gpio_num_t)BOOT_PIN);
  int64_t d = rtc_now_s() - rtc_sleep_stamp_s;
  rtc_sleep_stamp_s = 0;
  lcd.wakeup();
  load_pet(d > 0 ? (uint32_t)d : 0);
  applied_brightness = -1;  // loop() turns the backlight back on after the next push
#else
  esp_deep_sleep_enable_gpio_wakeup(1ULL << TOUCH_INT, ESP_GPIO_WAKEUP_GPIO_LOW);  // CST816S INT pulls low on touch
  esp_deep_sleep_start();
#endif
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  lcd.init();
  lcd.setRotation(CZ_ROTATION);
  prefs.begin("citizen", false);

  uint32_t slept = 0;
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED && rtc_sleep_stamp_s > 0) {
    int64_t d = rtc_now_s() - rtc_sleep_stamp_s;
    if (d > 0) slept = (uint32_t)d;
  }
  rtc_sleep_stamp_s = 0;
  load_pet(slept);

  // F9: first frame before the backlight, WiFi or anything that waits.
  cz_tick(millis());
  int y0, y1;
  cz_dirty_rows(&y0, &y1);  // clear the flag; the first frame is always full
  push_rows(0, CZ_H - 1);
  apply_brightness();
  Serial.printf("boot: first frame at %lu ms\n", (unsigned long)millis());
  tl_t0 = millis();

#if CZ_WIFI
  // F5: BOOT held for 2 s right after boot opens the portal. Holding it *through* power-on puts the
  // ESP32-C3 into ROM download mode (GPIO 9 is a strapping pin), so press it once the screen is lit.
  if (digitalRead(BOOT_PIN) == LOW) {
    uint32_t t0 = millis();
    while (digitalRead(BOOT_PIN) == LOW && millis() - t0 < 2000) delay(10);
    if (millis() - t0 >= 2000) run_portal();
  }
  wifi_begin();
#endif
}

void loop() {
  static uint32_t next_frame_ms = millis();  // static per-frame timer: one pass every 16 ms
  poll_serial();
  poll_touch();
  uint32_t t0 = micros();
  cz_tick(millis());  // the core renders inside tick
  uint32_t tick_us = micros() - t0;
#if CZ_WIFI
  wifi_watch();
  wifi_poll();  // outside frame_ms: an HTTP sync is a radio burst, not frame work
  switch (cz_host_request()) {  // F8: actions picked in Settings
    case CZ_REQ_PORTAL: run_portal(); break;  // blocks, reboots on save
    case CZ_REQ_CHECK_UPDATE: check_update(); break;
    case CZ_REQ_INSTALL_UPDATE: install_update(); break;
  }
  // F11: a newer build is waiting (update_url set by check_update) and the badge sat idle on Home/Sulk for 10 s.
  // One try per wake so a failed download cannot loop; Settings -> Update stays the manual path.
  static bool auto_tried = false;
  if (!auto_tried && update_url.length() && prefs.getInt("autoupd", 1) && millis() - last_touch_ms >= 10000 &&
      (cz_screen() == CZ_S_HOME || cz_screen() == CZ_S_SULK)) {
    auto_tried = true;
    cz_message("UPDATING");
    install_update();
  }
#endif
  if (cz_timezone() != known_tz) set_tz(cz_timezone());  // changed in Settings: persist and re-anchor
  if (cz_creature() != known_creature) {  // Settings -> Citizen: store creature + the core's name so wake keeps it
    known_creature = cz_creature();
    uint8_t blob[CZ_SAVE_SIZE];
    cz_save(blob, sizeof blob);  // the core has no name getter; the blob holds it NUL-terminated at 16..26
    prefs.putInt("creature", known_creature);
    prefs.putString("name", (const char *)blob + 16);
    set_qr();  // the default slug follows the creature
    save_now();
  }
  // Settings -> Brightness, Screen, Sky: all live in the save blob, so write it now
  if (cz_brightness() != known_brightness || cz_always_on() != known_always_on || cz_sky() != known_sky) {
    known_brightness = cz_brightness();
    known_always_on = cz_always_on();
    known_sky = cz_sky();
    save_now();
  }
  int y0, y1;
  if (cz_dirty_rows(&y0, &y1)) {
    uint32_t p0 = micros();
    int rows = push_rows(y0, y1);
    uint32_t p1 = micros();
    tl_push_us += p1 - p0;
    tl_frame_us += tick_us + (p1 - p0);
    tl_frames++;
    if (rows > tl_rows) tl_rows = rows;
  }
  apply_brightness();
  telemetry_tick();

  int h = cz_hearts();
  if (h != last_hearts || millis() - last_save_ms >= 5 * 60 * 1000UL) {
    last_hearts = h;
    save_now();
  }
  if (cz_wants_sleep() && first_sync_done) go_to_sleep();  // F6: never sleep before this wake's first sync attempt

  next_frame_ms += 16;
  int32_t wait = (int32_t)(next_frame_ms - millis());
  if (wait > 0) delay(wait);
  else next_frame_ms = millis();  // overran: don't try to catch up
}
