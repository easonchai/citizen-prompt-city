// Citizen badge host: ESP32-2424S012C. Owns time, touch, storage, WiFi; the core owns the pet and pixels.
// Pins come from platformio.ini build_flags (board facts cited there).
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_system.h>
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
static char tl_line[256];
static const char *wifi_state();
static int wifi_rssi();

// Server names for enum cz_creature / enum cz_sky, as /state lists them and telemetry reports them.
static const char *const CREATURE_NAMES[CZ_CREATURES] = {"tapir", "hornbill", "tiger"};
static const char *const SKY_NAMES[CZ_SKIES] = {"kl", "sg", "tyo"};

// "kl,sg,tyo" -> bit per name. Unknown names are ignored, so the server can add some before the firmware.
static unsigned names_mask(const char *list, const char *const *names, int count) {
  unsigned mask = 0;
  for (int i = 0; i < count; i++) {
    const char *p = strstr(list, names[i]);
    size_t n = strlen(names[i]);
    while (p && !((p == list || p[-1] == ',') && (p[n] == ',' || !p[n]))) p = strstr(p + 1, names[i]);
    if (p) mask |= 1u << i;
  }
  return mask;
}

static const char *telemetry_line() {
  uint32_t el = millis() - tl_t0;
  snprintf(tl_line, sizeof tl_line,
           "{\"t\":%lu,\"fps\":%lu,\"frame_ms\":%.2f,\"push_ms\":%.2f,\"rows\":%d,\"heap\":%lu,"
           "\"screen\":%d,\"hearts\":%d,\"wifi\":\"%s\",\"rssi\":%d,\"creature\":\"%s\",\"sky\":\"%s\"}",
           (unsigned long)millis(), (unsigned long)(el ? tl_frames * 1000UL / el : 0),
           tl_frames ? tl_frame_us / 1000.0 / tl_frames : 0.0, tl_frames ? tl_push_us / 1000.0 / tl_frames : 0.0,
           tl_rows, (unsigned long)ESP.getFreeHeap(), cz_screen(), cz_hearts(), wifi_state(), wifi_rssi(),
           CREATURE_NAMES[cz_creature()], SKY_NAMES[cz_sky()]);
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

// F6: server base + slug. No slug yet: the QR points at <base>/setup and the badge skips /state and telemetry.
static String pref_base() { return prefs.getString("base", CZ_DEFAULT_BASE); }
static String pref_slug() { return prefs.getString("slug", ""); }
static String claim_url() { String slug = pref_slug(); return pref_base() + (slug.length() ? "/c/" + slug : String("/setup")); }
// ?ref=qr marks a scan of the badge (analytics qr_scan); Improv still reports the plain claim URL.
static void set_qr() { cz_set_qr_url((claim_url() + (pref_slug().length() ? "?ref=qr" : "")).c_str()); }

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

static char cmd[160];  // the serial text line being typed (Improv headers only start at its beginning)
static size_t cmd_len = 0;

// --- WiFi + remote state ---
#if CZ_WIFI
static bool wifi_started = false;     // at least one saved network: the connect cycle runs
static uint32_t last_poll_ms = 0;
static bool first_sync_done = false;  // gates deep sleep so an idle badge still syncs once per wake
static bool sync_now = false;         // poll on the next pass: Settings > Sync, every (re)connect, light-sleep wake
static bool portal_open = false;
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

// --- saved networks: NVS ssid0..4 / pass0..4, net_last = the slot that last connected (-1 none) ---
static String net_ssid_at(int i) { return prefs.getString((String("ssid") + i).c_str(), ""); }
static String net_pass_at(int i) { return prefs.getString((String("pass") + i).c_str(), ""); }

// The saved SSIDs in slot order, empty slots skipped, to the core's WiFi screen.
static void net_list() {
  String list;
  for (int i = 0; i < CZ_WIFI_NETS; i++) {
    String ssid = net_ssid_at(i);
    if (!ssid.length()) continue;
    if (list.length()) list += '\n';
    list += ssid;
  }
  cz_set_wifi_list(list.c_str());
}

// Same SSID saved: new password. Else the first empty slot. Never erases another network.
// Returns the slot, or -1 when all 5 are taken.
static int net_add(const String &ssid, const String &pass) {
  int free_slot = -1;
  for (int i = 0; i < CZ_WIFI_NETS; i++) {
    String saved = net_ssid_at(i);
    if (saved == ssid) { prefs.putString((String("pass") + i).c_str(), pass); return i; }
    if (!saved.length() && free_slot < 0) free_slot = i;
  }
  if (free_slot < 0) return -1;
  prefs.putString((String("ssid") + free_slot).c_str(), ssid);
  prefs.putString((String("pass") + free_slot).c_str(), pass);
  return free_slot;
}

// listed: an index into the list net_list() sent (empty slots skipped).
static void net_forget(int listed) {
  for (int i = 0; i < CZ_WIFI_NETS; i++) {
    if (!net_ssid_at(i).length() || listed--) continue;
    prefs.remove((String("ssid") + i).c_str());
    prefs.remove((String("pass") + i).c_str());
    if (prefs.getInt("net_last", -1) == i) prefs.putInt("net_last", -1);
    break;
  }
  net_list();
}

// 1.2.0 kept one network in ssid/pass: copy it into a slot unless one already holds it, then drop the old keys.
static void net_migrate() {
  if (!prefs.isKey("ssid")) return;
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  bool have = false;
  for (int i = 0; i < CZ_WIFI_NETS; i++) have |= net_ssid_at(i) == ssid;
  if (ssid.length() && !have) net_add(ssid, pass);
  prefs.remove("ssid");
  prefs.remove("pass");
}

// --- connect cycle (F9, non-blocking): scan, try saved networks strongest first, then the last good one;
// all failed: FAIL and the cycle runs again in 60 s. A drop from connected starts it over.
enum { NET_OFF, NET_SCAN, NET_TRY, NET_UP, NET_WAIT };
static int net_st = NET_OFF, net_cand[CZ_WIFI_NETS], net_cand_n = 0, net_cand_i = 0;
static uint32_t net_t0 = 0, net_try_ms = 8000;
static String net_ssid;  // the network being tried or connected, for cz_set_wifi
static bool improv_scan = false;
static void improv_scan_results();

static int wifi_cz_state() {
  return net_st == NET_OFF ? CZ_WIFI_OFF : net_st == NET_UP ? CZ_WIFI_OK : net_st == NET_WAIT ? CZ_WIFI_FAIL : CZ_WIFI_CONNECTING;
}
static const char *wifi_state() {
  static const char *const names[4] = {"off", "connecting", "ok", "fail"};  // by enum cz_wifi_state
  return names[wifi_cz_state()];
}
static int wifi_rssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

static void wifi_connect() {
  if (portal_open) return;  // the portal restarts the cycle when it closes
  bool any = false;
  for (int i = 0; i < CZ_WIFI_NETS; i++) any |= net_ssid_at(i).length() > 0;
  wifi_started = any;
  if (!any) { net_st = NET_OFF; net_ssid = ""; cz_set_wifi("", CZ_WIFI_OFF); first_sync_done = true; return; }
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true);
  net_st = NET_SCAN;
  cz_set_wifi(net_ssid.c_str(), CZ_WIFI_CONNECTING);
}

static void wifi_try_next() {
  if (net_cand_i >= net_cand_n) {
    net_st = NET_WAIT;
    net_t0 = millis();
    cz_set_wifi(net_ssid.c_str(), CZ_WIFI_FAIL);
    return;
  }
  int i = net_cand[net_cand_i];
  net_ssid = net_ssid_at(i);
  WiFi.disconnect();
  WiFi.begin(net_ssid.c_str(), net_pass_at(i).c_str());
  net_st = NET_TRY;
  net_t0 = millis();
  cz_set_wifi(net_ssid.c_str(), CZ_WIFI_CONNECTING);
}

// One saved slot only, for ms (Improv).
static void wifi_try_slot(int slot, uint32_t ms) {
  wifi_started = true;
  net_cand[0] = slot;
  net_cand_n = 1;
  net_cand_i = 0;
  net_try_ms = ms;
  wifi_try_next();
}

static void wifi_step() {
  if (net_st == NET_SCAN) {
    int n = WiFi.scanComplete(), best[CZ_WIFI_NETS], last = prefs.getInt("net_last", -1);
    if (n == WIFI_SCAN_RUNNING) return;
    if (improv_scan) improv_scan_results();
    for (int i = 0; i < CZ_WIFI_NETS; i++) {  // the strongest signal each saved network was seen at; -1000 unseen
      String ssid = net_ssid_at(i);
      best[i] = -1000;
      for (int j = 0; ssid.length() && j < n; j++)
        if (WiFi.RSSI(j) > best[i] && WiFi.SSID(j) == ssid) best[i] = WiFi.RSSI(j);
    }
    WiFi.scanDelete();
    for (net_cand_n = 0;;) {
      int k = -1;
      for (int i = 0; i < CZ_WIFI_NETS; i++) if (best[i] > -1000 && (k < 0 || best[i] > best[k])) k = i;
      if (k < 0) break;
      net_cand[net_cand_n++] = k;
      best[k] = -1000;
    }
    bool listed = false;
    for (int i = 0; i < net_cand_n; i++) listed |= net_cand[i] == last;
    if (!listed && last >= 0 && last < CZ_WIFI_NETS && net_ssid_at(last).length()) net_cand[net_cand_n++] = last;  // hidden or out of scan
    net_cand_i = 0;
    net_try_ms = 8000;
    wifi_try_next();
  } else if (net_st == NET_TRY) {
    if (WiFi.status() == WL_CONNECTED) {
      if (prefs.getInt("net_last", -1) != net_cand[net_cand_i]) prefs.putInt("net_last", net_cand[net_cand_i]);
      net_st = NET_UP;
      cz_set_wifi(net_ssid.c_str(), CZ_WIFI_OK);
      configTime(cz_timezone() * 3600L, 0, "pool.ntp.org");  // F8
      sync_now = true;
    } else if (millis() - net_t0 >= net_try_ms) {
      net_cand_i++;
      wifi_try_next();
    }
  } else if (net_st == NET_UP) {
    if (WiFi.status() != WL_CONNECTED) wifi_connect();
  } else if (net_st == NET_WAIT && millis() - net_t0 >= 60000) {
    wifi_connect();
  }
}

static void wifi_begin() {
  if (!rtc_last_upd[0]) strlcpy(rtc_last_upd, prefs.getString("last_upd", "").c_str(), sizeof rtc_last_upd);
  if (!rtc_last_city[0]) strlcpy(rtc_last_city, prefs.getString("last_city", "").c_str(), sizeof rtc_last_city);
  net_migrate();
  net_list();
  wifi_connect();
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

// Every HTTPS call (state, telemetry, update check, OTA) trusts only ISRG Root X1, the Let's Encrypt root behind
// connect.prmpt.city (leaf <- YR2 <- Root YR, cross-signed by X1). A custom base (serial "url") must be https with a
// Let's Encrypt certificate, or put its root here. Expires 2035-06-04.
static const char ROOT_CA[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";
static WiFiClientSecure tls;  // setup() pins ROOT_CA on it

// F11: GET <base>/api/firmware/latest -> {"version","url","sha256"}. Tells the core whether a newer build exists.
static String update_url;
static bool check_update() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.setTimeout(3000);
  bool ok = false;
  if (http.begin(tls, pref_base() + "/api/firmware/latest")) {
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
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return r = httpUpdate.update(tls, update_url, CZ_VERSION);
  if (r != HTTP_UPDATE_OK) {
    Serial.printf("update failed: %s\n", httpUpdate.getLastErrorString().c_str());
    cz_message("UPDATE FAILED");
  }
}

// F6: poll once right after connecting (or once the connect cycle has failed), then every 60 s while awake,
// and on the next pass whenever sync_now is set. Every poll ends with cz_set_wifi, which clears the core's SYNCING.
// Telemetry rides along on syncs and every 10 minutes.
static void wifi_poll() {
  if (!wifi_started) return;
  bool connected = net_st == NET_UP;
  bool first = !first_sync_done;
  if (first || sync_now) {
    if (!connected && net_st != NET_WAIT) return;  // still scanning or trying a network
  } else if (millis() - last_poll_ms < 60000) return;
  bool synced = sync_now;  // wake, reconnect or Settings > Sync: telemetry goes too
  sync_now = false;
  first_sync_done = true;
  last_poll_ms = millis();
  if (!connected) { cz_set_wifi(net_ssid.c_str(), wifi_cz_state()); return; }
  String slug = pref_slug(), api = pref_base() + "/api/c/" + slug;
  if (slug.length()) {
    HTTPClient http;
    http.setTimeout(3000);
    if (http.begin(tls, api + "/state")) {
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
        // skies and creatures the badge has unlocked; kept in prefs so a badge offline at boot still has them
        if (json_str(body, "skies", buf, sizeof buf)) {
          unsigned mask = names_mask(buf, SKY_NAMES, CZ_SKIES);
          if (mask != prefs.getUInt("skies", 1)) prefs.putUInt("skies", mask);
          cz_set_skies(mask);
        }
        if (json_str(body, "creatures", buf, sizeof buf)) {
          unsigned mask = names_mask(buf, CREATURE_NAMES, CZ_CREATURES);
          if (mask != prefs.getUInt("creatures", 0)) prefs.putUInt("creatures", mask);
          cz_set_creatures(mask);  // a new one hatches; the loop persists the creature it ends on
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
  }
  clock_from_system();  // re-anchor to SNTP once a minute
  // F11: after the first state poll of each wake (deep sleep reboots, resetting these), then at most hourly.
  static bool update_checked = false;
  static uint32_t update_check_ms = 0;
  if ((!update_checked || millis() - update_check_ms >= 3600000UL) && check_update()) {
    update_checked = true;
    update_check_ms = millis();
  }
  // F6: the last F3 line on each sync and otherwise every 10 minutes while awake (one a minute woke Postgres all day)
  static bool tl_sent = false;
  static uint32_t tl_ms = 0;
  bool tl_due = synced || !tl_sent || millis() - tl_ms >= 600000UL;
  HTTPClient http;
  http.setTimeout(3000);
  if (!first && tl_due && slug.length() && tl_line[0] && http.begin(tls, api + "/telemetry")) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Badge-Key", prefs.getString("token", ""));  // server answers 401 without it
    http.POST((uint8_t *)tl_line, strlen(tl_line));
    http.end();
    tl_sent = true;
    tl_ms = millis();
  }
  cz_set_wifi(net_ssid.c_str(), wifi_cz_state());
}

// --- F5: captive-portal provisioning. Adds one network, then the badge carries on; 5 min without a save ends it.
static String ap_name() {
  uint8_t mac[6];
  char ap[16];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(ap, sizeof ap, "CITIZEN-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return ap;
}

static String html_esc(const String &s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}

// <option>s for the networks in range, strongest first, each SSID once. Blocks a few seconds: only from the portal.
static String portal_options() {
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) delay(10);
  int n = WiFi.scanNetworks();
  if (n > 64) n = 64;
  bool used[64] = {};
  String o;
  for (;;) {
    int k = -1;
    for (int j = 0; j < n; j++) if (!used[j] && WiFi.SSID(j).length() && (k < 0 || WiFi.RSSI(j) > WiFi.RSSI(k))) k = j;
    if (k < 0) break;
    String ssid = WiFi.SSID(k), e = html_esc(ssid);
    for (int j = 0; j < n; j++) used[j] |= WiFi.SSID(j) == ssid;
    o += "<option value=\"" + e + "\">" + e + "</option>";
  }
  WiFi.scanDelete();
  return o;
}

// The portal's pages share one stylesheet (a string in flash, not RAM): the site's navy and gold, phone-sized.
static const char PORTAL_HEAD[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Citizen WiFi</title><style>"
    "body{margin:0;min-height:100vh;display:grid;place-items:center;background:#0d1330;color:#f6f1e4;"
    "font:16px/1.5 -apple-system,system-ui,sans-serif}"
    "main{width:min(360px,calc(100vw - 32px));box-sizing:border-box;background:#141c3a;border:1px solid #2a3566;"
    "border-radius:16px;padding:24px}h1{margin:0 0 6px;font-size:22px;line-height:1.25}p{margin:0 0 8px;color:#b8bdd6}"
    "label{display:block;margin:18px 0 6px;font-size:12px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;"
    "color:#9aa0bf}select,input,button{width:100%;box-sizing:border-box;font:inherit;border-radius:10px;padding:13px 12px;"
    "border:1px solid #2a3566;background:#0a0e1f;color:#f6f1e4;-webkit-appearance:none}"
    "select{padding-right:40px;background-image:linear-gradient(45deg,transparent 50%,#9aa0bf 50%),"
    "linear-gradient(135deg,#9aa0bf 50%,transparent 50%);background-position:calc(100% - 21px) 52%,calc(100% - 15px) 52%;"
    "background-size:6px 6px;background-repeat:no-repeat}"
    "button{margin-top:24px;background:#f0b840;color:#0a0e1f;border:0;font-weight:700}"
    ".sub{display:inline-block;margin-top:8px;font-size:14px;color:#9aa0bf}a.sub{color:#f0b840}"
    ".ok{width:56px;height:56px;border-radius:50%;background:#2bb3a3;color:#0a0e1f;display:grid;place-items:center;"
    "font-size:30px;font-weight:700;margin-bottom:14px}</style><main>";

static void run_portal() {
  if (portal_open) return;
  portal_open = true;
  net_st = NET_OFF;  // the connect cycle pauses until the portal closes
  String ap = ap_name();
  // a fresh WPA2 password each time (12 of 31 unambiguous characters, about 59 bits, too many to crack from a captured
  // handshake inside the window): the home WiFi password never crosses the air in the clear
  WiFi.mode(WIFI_AP_STA);  // AP for the phone, STA so it can scan; first, so the radio is on before esp_random()
  WiFi.disconnect();       // a station retrying networks would move the AP's channel
  static const char abc[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
  char pw[13];
  for (int i = 0; i < 12; i++) pw[i] = abc[esp_random() % (sizeof abc - 1)];  // true RNG only while RF is on (ESP-IDF)
  pw[12] = 0;
  WiFi.softAP(ap.c_str(), pw);
  DNSServer dns;
  dns.start(53, "*", WiFi.softAPIP());  // every hostname resolves to 192.168.4.1
  WebServer web(80);
  String opts = portal_options();
  bool saved = false;
  web.on("/", HTTP_GET, [&] {
    if (web.hasArg("rescan")) opts = portal_options();
    web.send(200, "text/html",
             String(PORTAL_HEAD) +
                 "<h1>Connect your Citizen</h1><p>Pick your WiFi and enter its password. Your badge remembers up to 5 networks.</p>"
                 "<form method=post action=/save><label for=s>WiFi network</label>"
                 "<select id=s name=ssid onchange=\"document.getElementById('o').hidden=this.value!=''\">" +
                 opts + "<option value=''>Other network\u2026</option></select>"
                 "<a class=sub href='/?rescan=1'>Scan again</a>"
                 "<div id=o" + (opts.length() ? " hidden" : "") + "><label for=n>Network name</label>"
                 "<input id=n name=other maxlength=32 autocapitalize=off autocorrect=off></div>"
                 "<label for=p>Password</label><input id=p name=pass type=password maxlength=64>"
                 "<button>Save and connect</button></form></main>");
  });
  web.on("/save", HTTP_POST, [&] {
    String ssid = web.arg("ssid"), pass = web.arg("pass");
    if (!ssid.length()) ssid = web.arg("other");
    if (!ssid.length() || ssid.length() > 32 || pass.length() > 64 || ssid.indexOf('\n') >= 0) {  // net_list joins SSIDs with '\n'
      web.send(400, "text/plain", "Check the fields and try again.");
      return;
    }
    if (net_add(ssid, pass) < 0) {
      web.send(200, "text/html", String(PORTAL_HEAD) + "<h1>No room for another network</h1><p>This badge already "
               "remembers 5. On the badge, open Settings &gt; WiFi, forget one, then try again.</p></main>");
      return;
    }
    web.send(200, "text/html", String(PORTAL_HEAD) + "<div class=ok>&#10003;</div><h1>Saved</h1><p>Your Citizen is "
             "connecting to your WiFi now. You can close this page; your phone will go back to its usual network.</p>"
             "<p class=sub>The badge shows CONNECTED in Settings &gt; WiFi once it is online.</p></main>");
    saved = true;
  });
  web.onNotFound([&] {  // captive-portal probes (generate_204, hotspot-detect) land here
    web.sendHeader("Location", "http://192.168.4.1/");
    web.send(302, "text/plain", "");
  });
  web.begin();
  Serial.printf("portal: join %s, open http://192.168.4.1\n", ap.c_str());
  cz_portal(ap.c_str(), pw);
  int y0, y1;
  uint32_t t0 = millis();
  while (!saved && millis() - t0 < 5 * 60 * 1000UL) {
    dns.processNextRequest();
    web.handleClient();
    poll_serial();
    poll_touch();  // a double tap (or hold) on the portal screen cancels it
    if (cz_host_request() == CZ_REQ_PORTAL_CANCEL) break;
    cz_tick(millis());
    if (cz_dirty_rows(&y0, &y1)) push_rows(y0, y1);
    delay(10);
  }
  if (saved) for (uint32_t t = millis(); millis() - t < 500;) { web.handleClient(); delay(10); }  // let the reply reach the phone
  web.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  portal_open = false;
  cz_portal("", "");
  net_list();
  wifi_connect();
}

// --- Improv WiFi over serial (https://www.improv-wifi.com/serial/): esp-web-tools on /flash offers "Connect to Wi-Fi".
// Packet: "IMPROV", version 1, type, length, data, checksum = sum of every previous byte & 0xFF.
enum { IMP_STATE = 1, IMP_ERROR = 2, IMP_RPC = 3, IMP_RESULT = 4 };
enum { IMP_READY = 2, IMP_PROVISIONING = 3, IMP_PROVISIONED = 4 };
enum { IMP_ERR_INVALID = 1, IMP_ERR_UNKNOWN = 2, IMP_ERR_CONNECT = 3 };
static uint8_t imp_buf[256];
static int imp_n = 0;
static bool imp_drop_nl = false;  // a newline right after a packet belongs to it
static bool imp_wait = false;     // "send WiFi settings" is being tried

static void improv_send(uint8_t type, const uint8_t *data, int len) {
  uint8_t head[9] = {'I', 'M', 'P', 'R', 'O', 'V', 1, type, (uint8_t)len}, sum = 0;
  for (int i = 0; i < 9; i++) sum += head[i];
  for (int i = 0; i < len; i++) sum += data[i];
  Serial.write(head, 9);
  Serial.write(data, len);
  Serial.write(sum);
  Serial.write('\n');
}
static void improv_send1(uint8_t type, uint8_t v) { improv_send(type, &v, 1); }

// RPC result: command, data length, then each string prefixed by its length.
static void improv_result(uint8_t cmd, const String *s, int count) {
  uint8_t d[240];
  int len = 2;
  for (int i = 0; i < count && len + 1 + (int)s[i].length() <= (int)sizeof d; i++) {
    d[len++] = (uint8_t)s[i].length();
    memcpy(d + len, s[i].c_str(), s[i].length());
    len += s[i].length();
  }
  d[0] = cmd;
  d[1] = (uint8_t)(len - 2);
  improv_send(IMP_RESULT, d, len);
}

static void improv_state() { improv_send1(IMP_STATE, net_st == NET_UP ? IMP_PROVISIONED : IMP_READY); }

static void improv_scan_results() {
  int n = WiFi.scanComplete();
  for (int j = 0; j < n; j++) {
    if (!WiFi.SSID(j).length()) continue;
    String r[3] = {WiFi.SSID(j), String(WiFi.RSSI(j)), WiFi.encryptionType(j) == WIFI_AUTH_OPEN ? "NO" : "YES"};
    improv_result(0x04, r, 3);
  }
  improv_result(0x04, nullptr, 0);
  improv_scan = false;
}

static void improv_rpc(const uint8_t *d, int len) {
  if (len < 2 || d[1] != len - 2) { improv_send1(IMP_ERROR, IMP_ERR_INVALID); return; }
  switch (d[0]) {
    case 0x01: {  // send WiFi settings: ssid_len, ssid, pass_len, pass
      int sl = len > 2 ? d[2] : 0, pl = len > 3 + sl ? d[3 + sl] : -1;
      if (sl < 1 || sl > 32 || pl < 0 || pl > 64 || 4 + sl + pl != len || memchr(d + 3, '\n', sl)) { improv_send1(IMP_ERROR, IMP_ERR_INVALID); return; }
      char ssid[33], pass[65];
      memcpy(ssid, d + 3, sl);
      ssid[sl] = 0;
      memcpy(pass, d + 4 + sl, pl);
      pass[pl] = 0;
      int slot = net_add(ssid, pass);
      if (slot < 0) { improv_send1(IMP_ERROR, IMP_ERR_CONNECT); return; }  // 5 saved: forget one first
      net_list();
      if (portal_open) { improv_send1(IMP_ERROR, IMP_ERR_CONNECT); return; }  // saved; the portal's close tries it
      improv_send1(IMP_STATE, IMP_PROVISIONING);
      wifi_try_slot(slot, 15000);
      imp_wait = true;  // improv_tick answers once it connects or gives up
      return;
    }
    case 0x02: {  // request current state
      improv_state();
      String url = claim_url();
      if (net_st == NET_UP) improv_result(0x02, &url, 1);
      return;
    }
    case 0x03: {  // device info
      String r[4] = {"Citizen", CZ_VERSION, "ESP32-C3", ap_name()};
      improv_result(0x03, r, 4);
      return;
    }
    case 0x04:  // scanned networks: async, answered from improv_tick or the connect cycle's scan
      if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true);
      improv_scan = true;
      return;
    default: improv_send1(IMP_ERROR, IMP_ERR_UNKNOWN);
  }
}

static void improv_tick() {
  if (improv_scan && net_st != NET_SCAN && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    improv_scan_results();
    WiFi.scanDelete();
  }
  if (!imp_wait || net_st == NET_TRY || net_st == NET_SCAN) return;
  imp_wait = false;
  if (net_st != NET_UP) { improv_send1(IMP_ERROR, IMP_ERR_CONNECT); return; }  // the network stays saved
  String url = claim_url();
  improv_send1(IMP_STATE, IMP_PROVISIONED);
  improv_result(0x01, &url, 1);
}

// Improv and the text commands share the port. Bytes that match a prefix of "IMPROV" are held; on a mismatch the
// held bytes go to the text path, followed by the current one (returns false). After the header the packet owns
// the bytes through its checksum. A header only starts a line; text commands are lower case, so none starts with 'I'.
static void text_byte(char c);
static bool improv_byte(uint8_t c) {
  static const char head[] = "IMPROV";
  static uint32_t imp_ms = 0;
  uint32_t now = millis();
  if (imp_n && now - imp_ms > 200) imp_n = 0;  // stalled mid-packet: drop it, so later text commands stay text
  imp_ms = now;
  if (imp_n == 0 && cmd_len != 0) return false;  // mid-line: "IMPROV" inside a text command is text
  if (imp_n < 6) {
    if (c == (uint8_t)head[imp_n]) { imp_buf[imp_n++] = c; return true; }
    for (int i = 0; i < imp_n; i++) text_byte((char)imp_buf[i]);
    imp_n = 0;
    return false;
  }
  imp_buf[imp_n++] = c;
  if (imp_n == 9 && imp_buf[8] > sizeof imp_buf - 10) { imp_n = 0; improv_send1(IMP_ERROR, IMP_ERR_INVALID); return true; }
  if (imp_n < 10 || imp_n < 10 + imp_buf[8]) return true;
  uint8_t sum = 0;
  for (int i = 0; i < imp_n - 1; i++) sum += imp_buf[i];
  if (sum != imp_buf[imp_n - 1]) improv_send1(IMP_ERROR, IMP_ERR_INVALID);
  else if (imp_buf[7] == IMP_RPC) improv_rpc(imp_buf + 9, imp_buf[8]);
  imp_n = 0;
  imp_drop_nl = true;
  return true;
}
#else
static const char *wifi_state() { return "off"; }
static int wifi_rssi() { return 0; }
static const bool first_sync_done = true;
#endif

// --- serial config over USB CDC ---

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
  else if (!strcmp(line, "skies")) { unsigned m = names_mask(arg, SKY_NAMES, CZ_SKIES); prefs.putUInt("skies", m); cz_set_skies(m); }
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
  else if (!strcmp(line, "wifi")) {  // adds a network (or a new password for a saved one) and starts the connect cycle
    char *pass = strchr(arg, ' ');
    if (pass) *pass++ = 0; else pass = arg + strlen(arg);
    if (!*arg || strlen(arg) > 32 || strlen(pass) > 64) { Serial.println("wifi SSID PASS"); return; }
    if (net_add(arg, pass) < 0) { Serial.println("wifi list full: forget one first"); return; }
    net_list();
    wifi_connect();
  }
  else if (!strcmp(line, "nets")) {  // saved SSIDs, never passwords
    for (int i = 0; i < CZ_WIFI_NETS; i++) if (net_ssid_at(i).length()) Serial.printf("%d %s\n", i, net_ssid_at(i).c_str());
    return;
  }
  else if (!strcmp(line, "portal")) run_portal();
  else if (!strcmp(line, "update")) { update_url = ""; install_update(); }
  else if (!strcmp(line, "autoupdate")) { prefs.putInt("autoupd", atoi(arg) ? 1 : 0); }
#endif
  else if (!strcmp(line, "reset")) { prefs.clear(); rtc_sleep_stamp_s = 0;
#if CZ_WIFI
    rtc_last_upd[0] = rtc_last_city[0] = 0;
#endif
    Serial.println("cleared, restarting"); delay(100); ESP.restart(); }
  else { Serial.println("commands: name X | creature N | prompt ... | wifi SSID PASS | nets | portal | url BASE | slug S | token KEY | msg ... | checkin [CITY] | skies kl,sg,tyo | brightness 100|40|10 | time HH:MM | tz N | version | update | autoupdate 0|1 | stats | reset"); return; }
  Serial.println("ok");
}

static void text_byte(char c) {
  if (c == '\r') return;
  if (c == '\n') { cmd[cmd_len] = 0; if (cmd_len) run_command(cmd); cmd_len = 0; }
  else if (cmd_len < sizeof cmd - 1) cmd[cmd_len++] = c;
}

static void poll_serial() {
  while (Serial.available()) {
    uint8_t c = Serial.read();
#if CZ_WIFI
    if (imp_drop_nl) { imp_drop_nl = false; if (c == '\n') continue; }
    if (improv_byte(c)) continue;
#endif
    text_byte((char)c);
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
  cz_set_creatures(prefs.getUInt("creatures", 0));  // likewise which creatures already hatched; 0 (never synced) is ignored
#if CZ_WIFI
  net_list();  // cz_init cleared the WiFi screen's list
#endif
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
#if CZ_WIFI
  sync_now = true;
#endif
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
  tls.setCACert(ROOT_CA);
  wifi_begin();
  if (!pref_slug().length()) Serial.println("no slug: serial 'slug <s>' links this badge");
  // F5: BOOT held for 2 s right after boot opens the portal. Holding it *through* power-on puts the
  // ESP32-C3 into ROM download mode (GPIO 9 is a strapping pin), so press it once the screen is lit.
  if (digitalRead(BOOT_PIN) == LOW) {
    uint32_t t0 = millis();
    while (digitalRead(BOOT_PIN) == LOW && millis() - t0 < 2000) delay(10);
    if (millis() - t0 >= 2000) run_portal();
  }
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
  wifi_step();
  improv_tick();
  wifi_poll();  // outside frame_ms: an HTTP sync is a radio burst, not frame work
  switch (cz_host_request()) {  // F8: actions picked in Settings
    case CZ_REQ_PORTAL: run_portal(); break;  // blocks until a network is saved or 5 min pass
    case CZ_REQ_CHECK_UPDATE: check_update(); break;
    case CZ_REQ_INSTALL_UPDATE: install_update(); break;
    case CZ_REQ_SYNC: sync_now = true; if (!wifi_started) cz_set_wifi("", CZ_WIFI_OFF); break;  // nothing to poll: clear SYNCING
    case CZ_REQ_WIFI_FORGET: { int i = cz_wifi_forget(); if (i >= 0) net_forget(i); } break;
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
