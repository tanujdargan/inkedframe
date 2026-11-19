// inkedframe: battery photo frame. Feather ESP32-S3 + Waveshare 7.3" Spectra 6 (E6).
//
// Wiring: CS 10, DC 9, RST 6, BUSY 5, DIN->MO(35), SCLK->SCK(36), VCC->3V
//
// Life cycle (everything happens in setup(), then deep sleep):
//   timer wake            -> show the next stored photo, radio off; with the date or artwork on,
//                            join Wi-Fi briefly, sync the clock, then draw (at 00:30 local time)
//   button / power-on     -> join Wi-Fi, serve http://frame.local for a few minutes,
//                            store uploads and settings, show the newest
//   button held 5 s       -> forget Wi-Fi and run Bluetooth setup again
//   no Wi-Fi stored       -> Bluetooth setup, QR code on the panel
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <GxEPD2_7C.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiProv.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <qrcode.h>
#include <rom/tjpgd.h>  // the JPEG decoder in the chip's ROM: costs no flash

#ifndef ROTATE_SECONDS
#define ROTATE_SECONDS 86400  // how often the stored photos rotate
#endif

const int W = 800, H = 480;
const size_t PHOTO_BYTES = W * H / 3;  // three pixels per byte: a * 36 + b * 6 + c
const int MAX_PHOTOS = 10;             // 10 x 125 KB, the artwork and an upload fit the 1.5 MB photos partition
const gpio_num_t BUTTON = GPIO_NUM_0;  // onboard BOOT button
#ifndef SESSION_SECONDS
#define SESSION_SECONDS 180
#endif
const uint32_t SESSION_MS = SESSION_SECONDS * 1000UL;  // upload window after a button press
const uint32_t SETUP_MS = 600000;      // how long Bluetooth setup stays open
const uint32_t HOLD_RESET_MS = 5000;
const uint16_t LOW_BATTERY_MV = 3400;  // tune to the cell; below this the frame stops rotating
const uint32_t WIFI_TIMEOUT_MS = 20000;
const int AUTO_TZ = 9999;              // "tz" setting: minutes east of UTC, or this for "look it up"
const size_t MAX_JPEG = 600 * 1024;
const char *FS_ROOT = "/littlefs";

GxEPD2_7C<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT / 4>
  display(GxEPD2_730c_GDEP073E01(10, 9, 6, 5));

// Photo files hold these indexes, three per byte; same order as PALETTE in index.html.
const uint16_t COLORS[] = {GxEPD_BLACK, GxEPD_WHITE, GxEPD_YELLOW, GxEPD_RED, GxEPD_BLUE, GxEPD_GREEN};
// For dithering artwork on the frame; keep equal to PALETTE, SATURATION and CONTRAST in index.html.
const uint8_t PALETTE[6][3] = {{25, 30, 33}, {232, 232, 232}, {239, 222, 68}, {178, 19, 24}, {33, 87, 186}, {53, 120, 70}};
const float SATURATION = 1.4f, CONTRAST = 1.15f;

extern const char index_html[] asm("_binary_src_index_html_start");

Preferences prefs;
WebServer server(80);
volatile bool provStarted, gotIp, credFail;
uint32_t lastActivity, lastUpload;
bool uploaded, changed, toUpdater;
bool pending;  // a new firmware from the updater that has not yet proved itself
bool radioOff = true;  // light sleep is only safe while Wi-Fi and Bluetooth are off

const uint8_t PANEL_PINS[] = {10, 9, 6, SCK, MOSI};  // CS, DC, RST, clock, data

// A colour refresh takes about 30 s. Doze instead of spinning the CPU while the panel is busy.
void whilePanelBusy(const void *) {
  if (!radioOff) { delay(10); return; }
  gpio_wakeup_enable(GPIO_NUM_5, GPIO_INTR_HIGH_LEVEL);  // BUSY goes high when the panel is done
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(1000000);
  esp_light_sleep_start();
  gpio_wakeup_disable(GPIO_NUM_5);
}

// ---------- photo store: /<8-digit sequence>.bin, highest is newest ----------

int listPhotos(uint32_t *seqs) {  // ascending
  int n = 0;
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    const char *name = f.name();
    if (*name == '/') name++;
    if (strlen(name) == 12 && !strcmp(name + 8, ".bin") && n < MAX_PHOTOS + 1) seqs[n++] = strtoul(name, nullptr, 10);
  }
  std::sort(seqs, seqs + n);
  return n;
}

String photoPath(uint32_t seq) {
  char p[16];
  snprintf(p, sizeof p, "/%08u.bin", (unsigned)seq);
  return p;
}

// ---------- settings and clock ----------

bool online() { return prefs.getUChar("date", 0) || prefs.getUChar("show", 0); }  // needs Wi-Fi once a day
bool clockSet() { return time(nullptr) > 1700000000; }  // false after a power loss until NTP has run

time_t localNow() {
  int tz = prefs.getInt("tz", AUTO_TZ);
  return time(nullptr) + (tz == AUTO_TZ ? prefs.getInt("tzc", 0) : tz * 60);
}

// The deep-sleep clock drifts by a percent or two, so each daily sleep aims at 00:30 local time from
// a freshly synced clock. s is the local time of day: before noon today is done, so aim at tomorrow.
constexpr uint32_t untilHalfPastMidnight(uint32_t s) { return 86400 + 1800 - s % 86400; }
static_assert(untilHalfPastMidnight(1800) == 86400 && untilHalfPastMidnight(0) == 88200 &&
              untilHalfPastMidnight(86400 - 600) == 2400 && untilHalfPastMidnight(86400 + 3600) == 84600, "");

// NTP, then the UTC offset when the time zone is automatic. Wi-Fi must be up.
bool syncClock() {
  uint32_t deadline = millis() + 10000;  // lwIP waits a random 0-5 s before its first NTP request
  configTime(0, 0, "pool.ntp.org");
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (millis() > deadline) return false;
    delay(50);
  }
  if (prefs.getInt("tz", AUTO_TZ) == AUTO_TZ) {
    WiFiClient tcp;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    String r = http.begin(tcp, "http://ip-api.com/json?fields=status,offset") && http.GET() == 200 ? http.getString() : "";
    http.end();
    int i = r.indexOf("\"offset\":");
    if (r.indexOf("success") > 0 && i > 0) prefs.putInt("tzc", r.substring(i + 9).toInt());  // kept for failed lookups
    Serial.printf("tz lookup %s\n", r.c_str());
  }
  return true;
}

// ---------- display ----------

// The panel's control lines must never float: left alone they pick up noise, the display board's
// booster switches on and draws about 75 mA while awake and 0.85 mA asleep (both measured).
// Held low, the controller sits in reset and the whole frame sleeps at about 22 uA.
void panelOff() {
  SPI.end();
  for (uint8_t pin : PANEL_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    gpio_hold_en((gpio_num_t)pin);
  }
}

void displayBegin() {
  for (uint8_t pin : PANEL_PINS) gpio_hold_dis((gpio_num_t)pin);
  display.init(0, true, 2, false);
  display.epd2.setBusyCallback(whilePanelBusy);
  display.setRotation(0);
  display.setFullWindow();
}

// Today's date in black on white, so it reads over any picture. Drawn on every page of the refresh.
void drawDate() {
  int mode = prefs.getUChar("date", 0);
  time_t t = localNow();
  if (!mode || !clockSet()) return;
  tm d;
  gmtime_r(&t, &d);
  char day[12], mon[12], s[40];
  strftime(day, sizeof day, "%A", &d);
  strftime(mon, sizeof mon, "%B", &d);
  int16_t x1, y1, x2, y2, x3, y3;
  uint16_t w, h, w2, h2, w3, h3;
  display.setTextColor(GxEPD_BLACK);
  if (mode == 1) {  // corner: "Thu 19 Nov", bottom right
    snprintf(s, sizeof s, "%.3s %d %.3s", day, d.tm_mday, mon);
    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    display.fillRect(W - 20 - w - 24, H - 20 - h - 24, w + 24, h + 24, GxEPD_WHITE);
    display.setCursor(W - 20 - 12 - w - x1, H - 20 - 12 - h - y1);
    display.print(s);
  } else if (mode == 2) {  // banner: full-width strip along the bottom
    snprintf(s, sizeof s, "%s %d %s %d", day, d.tm_mday, mon, d.tm_year + 1900);
    display.setFont(&FreeSansBold18pt7b);
    display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    display.fillRect(0, H - 64, W, 64, GxEPD_WHITE);
    display.setCursor((W - w) / 2 - x1, H - 32 - y1 / 2);
    display.print(s);
  } else {  // big: large day number, month and weekday beside it, bottom left
    snprintf(s, sizeof s, "%d", d.tm_mday);
    display.setFont(&FreeSansBold18pt7b);
    display.getTextBounds(mon, 0, 0, &x2, &y2, &w2, &h2);
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds(day, 0, 0, &x3, &y3, &w3, &h3);
    display.setFont(&FreeSansBold24pt7b);
    display.setTextSize(2);
    display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    int x = 20 + 16, y = H - 20 - 16 - h - y1;  // number baseline
    display.fillRect(20, H - 20 - 32 - h, 32 + w + 20 + std::max(w2, w3), 32 + h, GxEPD_WHITE);
    display.setCursor(x - x1, y);
    display.print(s);
    display.setTextSize(1);
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(x + w + 20 - x2, y + y1 - y2);  // tops level with the number's
    display.print(mon);
    display.setFont(&FreeSans12pt7b);
    display.setCursor(x + w + 20 - x3, y);
    display.print(day);
  }
  static bool logged;
  if (!logged) Serial.printf("date mode=%d \"%s\" (%s %s)\n", mode, s, day, mon);
  logged = true;
}

bool showFile(const String &path) {
  File f = LittleFS.open(path);
  if (!f || f.size() != PHOTO_BYTES) return false;
  Serial.printf("show %s\n", path.c_str());
  displayBegin();
  static uint8_t rows[W];  // three rows: 800 is not a multiple of 3, but 3 x 800 is
  display.firstPage();
  do {
    f.seek(0);
    for (int y = 0; y < H; y += 3) {
      f.read(rows, sizeof rows);
      for (int i = 0; i < 3 * W; i++) {
        uint8_t b = rows[i / 3], c = i % 3 == 0 ? b / 36 : i % 3 == 1 ? b / 6 % 6 : b % 6;
        display.drawPixel(i % W, y + i / W, b < 216 ? COLORS[c] : GxEPD_WHITE);  // uploads are untrusted
      }
    }
    drawDate();
  } while (display.nextPage());
  display.hibernate();
  panelOff();
  return true;
}

bool showPhoto(uint32_t seq) {
  if (!showFile(photoPath(seq))) return false;
  prefs.putUInt("cur", seq);
  return true;
}

// A plain text screen, optionally with a QR code on the right.
void showText(const char *title, const char *const *lines, int n, esp_qrcode_handle_t qr = nullptr) {
  displayBegin();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(40, 80);
    display.print(title);
    display.setFont(&FreeSans12pt7b);
    for (int i = 0; i < n; i++) {
      display.setCursor(40, 150 + i * 44);
      display.print(lines[i]);
    }
    if (qr) {
      int size = esp_qrcode_get_size(qr), px = 240 / size, x0 = W - 40 - size * px, y0 = (H - size * px) / 2;
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
          if (esp_qrcode_get_module(qr, x, y)) display.fillRect(x0 + x * px, y0 + y * px, px, px, GxEPD_BLACK);
    }
  } while (display.nextPage());
  display.hibernate();
  panelOff();
}

// ---------- power ----------

int gauge(uint8_t reg) {  // MAX17048 fuel gauge register; -1 if absent
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, HIGH);
  delay(10);
  Wire.begin();
  Wire.beginTransmission(0x36);
  Wire.write(reg);
  if (Wire.endTransmission(false) || Wire.requestFrom(0x36, 2) != 2) return -1;
  int raw = Wire.read() << 8;
  return raw | Wire.read();
}

uint16_t batteryMv() {  // 0 if absent
  int raw = gauge(0x02);  // VCELL, 78.125 uV per bit
  return raw < 0 ? 0 : raw * 5UL / 64;
}

[[noreturn]] void sleepNow() {
  // A deep-sleep wake skips the boot loader's check, so an unproven firmware would stay for good.
  if (pending) {
    Serial.println("new firmware not confirmed: rolling back");
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP.restart();  // the boot loader rolls back a pending image on any restart
  }
  uint64_t secs = ROTATE_SECONDS;
#if ROTATE_SECONDS == 86400  // bench builds keep the short fixed interval
  if (online() && clockSet()) secs = untilHalfPastMidnight(localNow() % 86400);
#endif
  Serial.printf("sleep %u s, awake %lu ms\n", (unsigned)secs, millis());
  Serial.flush();
  server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Wire.end();
  digitalWrite(PIN_I2C_POWER, LOW);
  digitalWrite(NEOPIXEL_POWER, !NEOPIXEL_POWER_ON);
  while (digitalRead(BUTTON) == LOW) delay(20);  // a held button would wake us straight away
  panelOff();
  gpio_deep_sleep_hold_en();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  rtc_gpio_pullup_en(BUTTON);
  rtc_gpio_pulldown_dis(BUTTON);
  esp_sleep_enable_ext0_wakeup(BUTTON, 0);
  esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
  esp_deep_sleep_start();
}

// ---------- timer wake: next photo, no radio ----------

void rotate() {
  uint32_t seqs[MAX_PHOTOS + 1];
  int n = listPhotos(seqs);
  if (n < (prefs.getUChar("date", 0) ? 1 : 2)) return;  // nothing to rotate to (with the date on, redraw even one)
  uint16_t mv = batteryMv();
  if (mv && mv < LOW_BATTERY_MV) return;  // a refresh on a flat cell can brown out half-drawn; keep the picture
  uint32_t cur = prefs.getUInt("cur", 0), next = seqs[n - 1];
  for (int i = n - 1; i >= 0; i--)
    if (seqs[i] < cur) { next = seqs[i]; break; }  // step to the next older, wrap to newest
  showPhoto(next);
}

// ---------- artwork: one picture a day, decoded and dithered here into the photo format ----------

struct Jpeg {
  const uint8_t *data;
  size_t len, pos;
  uint8_t *rgb;  // W x H x 3
  int cw, ch, ox, oy;  // the part of the (scaled) picture that fills the frame
};

UINT jpegIn(JDEC *jd, BYTE *buf, UINT n) {
  Jpeg &j = *(Jpeg *)jd->device;
  n = std::min<size_t>(n, j.len - j.pos);
  if (buf) memcpy(buf, j.data + j.pos, n);
  j.pos += n;
  return n;
}

// The picture is at least frame size, so every frame pixel gets the nearest source pixel.
UINT jpegOut(JDEC *jd, void *bitmap, JRECT *r) {
  Jpeg &j = *(Jpeg *)jd->device;
  const uint8_t *p = (const uint8_t *)bitmap;
  for (int y = r->top; y <= r->bottom; y++)
    for (int x = r->left; x <= r->right; x++, p += 3) {
      int dx = (x - j.ox) * W / j.cw, dy = (y - j.oy) * H / j.ch;
      if (x >= j.ox && y >= j.oy && dx < W && dy < H) memcpy(j.rgb + (dy * W + dx) * 3, p, 3);
    }
  return 1;
}

// Same steps as dither() in index.html (saturation, contrast, Floyd-Steinberg), packed like an upload.
bool ditherToFile(uint8_t *rgb, const char *path) {
  double sum = 0;
  for (uint8_t *p = rgb; p < rgb + W * H * 3; p += 3) {
    float grey = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
    for (int c = 0; c < 3; c++) p[c] = constrain(grey + (p[c] - grey) * SATURATION, 0.0f, 255.0f) + 0.5f;
    sum += 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
  }
  float mean = sum / (W * H);
  float *err = (float *)calloc(2 * (W + 2) * 3, sizeof(float));  // this row and the next, a spare pixel each side
  LittleFS.remove(path);  // the partition has no room for a second copy when the photo store is full
  File f = LittleFS.open(path, "w");
  bool ok = err && f;
  static uint8_t rows[W];  // three rows, three pixels per byte
  for (int y = 0; ok && y < H; y++) {
    float *cur = err + (y & 1) * (W + 2) * 3, *next = err + (~y & 1) * (W + 2) * 3;
    memset(next, 0, (W + 2) * 3 * sizeof(float));
    for (int x = 0; x < W; x++) {
      const uint8_t *p = rgb + (y * W + x) * 3;
      float v[3], bestD = 1e9f;
      int best = 0;
      for (int c = 0; c < 3; c++) v[c] = constrain(mean + (p[c] - mean) * CONTRAST, 0.0f, 255.0f) + cur[(x + 1) * 3 + c];
      for (int i = 0; i < 6; i++) {
        float d = 0;
        for (int c = 0; c < 3; c++) d += (v[c] - PALETTE[i][c]) * (v[c] - PALETTE[i][c]);
        if (d < bestD) bestD = d, best = i;
      }
      int i = y % 3 * W + x;
      rows[i / 3] = (i % 3 ? rows[i / 3] * 6 : 0) + best;
      for (int c = 0; c < 3; c++) {
        float e = v[c] - PALETTE[best][c];
        cur[(x + 2) * 3 + c] += e * 7 / 16;
        next[x * 3 + c] += e * 3 / 16;
        next[(x + 1) * 3 + c] += e * 5 / 16;
        next[(x + 2) * 3 + c] += e / 16;
      }
    }
    if (y % 3 == 2) ok = f.write(rows, sizeof rows) == sizeof rows;
  }
  f.close();
  free(err);
  if (!ok) LittleFS.remove(path);
  return ok;
}

bool fetchArt(uint32_t deadline) {
  uint32_t t0 = millis();
  WiFiClientSecure tls;
  // No certificate check: these are public pictures, and the CA bundle would not fit comfortably in
  // flash. The one secret sent is the optional Unsplash key, which an attacker on the path could read.
  tls.setInsecure();
  tls.setHandshakeTimeout(8);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  http.useHTTP10(true);  // no chunked replies: the body is just the bytes
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  String src = "picsum.photos/800/480.jpg?random=" + String((uint32_t)(time(nullptr) / ROTATE_SECONDS));
  String key = prefs.getString("key");
  if (key.length()) {
    String json = http.begin(tls, "https://api.unsplash.com/photos/random?query=landscape&orientation=landscape&client_id=" + key) &&
                  http.GET() == 200 ? http.getString() : "";
    http.end();
    int i = json.indexOf("\"raw\":\"");
    if (i < 0) {
      Serial.println("art: unsplash request failed");
      return false;
    }
    src = json.substring(i + 7, json.indexOf('"', i + 7));
    src.replace("\\u0026", "&");
    src += "&w=800&h=480&fit=crop&fm=jpg&q=80";
  }
  // picsum and Unsplash only send progressive JPEGs, which the ROM decoder cannot read: wsrv.nl, a free
  // image proxy, re-encodes them as baseline.
  String url = "https://wsrv.nl/?output=jpg&q=85&url=";
  for (char c : src) {
    char e[4];
    snprintf(e, sizeof e, "%%%02X", (uint8_t)c);
    url += isalnum(c) || strchr("-._~/:", c) ? String(c) : String(e);
  }
  int code = http.begin(tls, url) ? http.GET() : -1, len = http.getSize();
  size_t got = 0, cap = len > 0 ? len : MAX_JPEG;
  uint8_t *jpg = code == 200 && cap <= MAX_JPEG ? (uint8_t *)ps_malloc(cap) : nullptr;
  WiFiClient *s = http.getStreamPtr();
  while (jpg && got < cap && (s->connected() || s->available()) && millis() < deadline) {
    int n = s->read(jpg + got, cap - got);
    if (n > 0) got += n;
    else delay(5);
  }
  http.end();
  Serial.printf("art: http %d, %u of %d bytes in %lu ms, heap %u, psram found %d free %u\n", code, got, len,
                millis() - t0, ESP.getFreeHeap(), psramFound(), ESP.getFreePsram());
  bool ok = jpg && (len > 0 ? got == (size_t)len : got < cap);
  JDEC jd;
  Jpeg j = {jpg, got, 0, nullptr};
  void *work = malloc(4096);
  JRESULT r = ok && work ? jd_prepare(&jd, jpegIn, work, 4096, &j) : JDR_PAR;
  ok = r == JDR_OK && jd.width >= W && jd.height >= H;  // progressive JPEGs fail here (JDR_FMT3)
  if (ok) {
    int scale = 0;  // the decoder can shrink by 1/2, 1/4, 1/8 while staying at least frame size
    while (scale < 3 && (jd.width >> (scale + 1)) >= W && (jd.height >> (scale + 1)) >= H) scale++;
    int sw = jd.width >> scale, sh = jd.height >> scale;
    j.cw = std::min(sw, sh * W / H);
    j.ch = j.cw * H / W;
    j.ox = (sw - j.cw) / 2;
    j.oy = (sh - j.ch) * 45 / 100;  // same crop as the upload page
    j.rgb = (uint8_t *)ps_malloc(W * H * 3);
    r = j.rgb ? jd_decomp(&jd, jpegOut, scale) : JDR_MEM1;
    ok = r == JDR_OK;
  }
  Serial.printf("art: decode %d (%ux%u) at %lu ms\n", r, ok ? jd.width : 0, ok ? jd.height : 0, millis() - t0);
  free(work);
  free(jpg);
  ok = ok && ditherToFile(j.rgb, "/art.bin");
  free(j.rgb);
  Serial.printf("art: %s at %lu ms\n", ok ? "saved" : "failed, keeping the current picture", millis() - t0);
  return ok;
}

// ---------- timer wake with the date or artwork on: one short Wi-Fi visit a day ----------

void daily() {
  uint16_t mv = batteryMv();
  if (mv && mv < LOW_BATTERY_MV) return;
  uint32_t deadline = millis() + 30000;  // a dead router or internet must not keep the frame awake
  Serial.printf("kept clock %ld\n", (long)time(nullptr));
  radioOff = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  while (WiFi.status() != WL_CONNECTED && millis() < deadline - 20000) delay(50);  // up to 10 s
  bool synced = WiFi.status() == WL_CONNECTED && syncClock();
  Serial.printf("wifi %d, synced %d at %lu ms, clock %ld, local day second %ld, heap %u\n", WiFi.status() == WL_CONNECTED,
                synced, millis(), (long)time(nullptr), (long)(localNow() % 86400), ESP.getFreeHeap());
#if ROTATE_SECONDS == 86400
  if (clockSet() && localNow() % 86400 >= 43200) return;  // woke before midnight: sleepNow() sleeps the rest
#endif
  bool art = prefs.getUChar("show", 0), fresh = art && synced && fetchArt(deadline);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  radioOff = true;
  if (!art) rotate();
  else if (fresh || prefs.getUChar("date", 0)) showFile("/art.bin");  // failed fetch: keep the picture, new date
}

// ---------- upload session ----------

void onWifiEvent(arduino_event_t *e) {
  if (e->event_id == ARDUINO_EVENT_PROV_START) provStarted = true;
  if (e->event_id == ARDUINO_EVENT_PROV_CRED_FAIL) credFail = true;
  if (e->event_id == ARDUINO_EVENT_WIFI_STA_GOT_IP) gotIp = true;
}

void handleUpload() {
  static File f;
  static size_t got;
  HTTPUpload &up = server.upload();
  lastActivity = millis();
  if (up.status == UPLOAD_FILE_START) {
    uint32_t seqs[MAX_PHOTOS + 1];
    int n = listPhotos(seqs);
    for (int i = 0; i <= n - MAX_PHOTOS; i++) {  // make room: drop the oldest
      Serial.printf("drop %s\n", photoPath(seqs[i]).c_str());
      LittleFS.remove(photoPath(seqs[i]));
    }
    f = LittleFS.open("/up.tmp", "w");
    got = 0;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    got += up.currentSize;
    if (got <= PHOTO_BYTES && !(f && f.write(up.buf, up.currentSize) == up.currentSize)) got = SIZE_MAX / 2;  // file system full
  } else {  // END or ABORTED
    if (f) f.close();
    if (up.status == UPLOAD_FILE_END && got == PHOTO_BYTES) {
      uint32_t seq = prefs.getUInt("seq", 0) + 1;
      prefs.putUInt("seq", seq);
      uploaded = LittleFS.rename("/up.tmp", photoPath(seq));
      lastUpload = millis();
      Serial.printf("stored %s, file system %u of %u bytes used\n", photoPath(seq).c_str(), LittleFS.usedBytes(), LittleFS.totalBytes());
    } else {
      Serial.printf("rejected upload of %u bytes\n", got);
      LittleFS.remove("/up.tmp");
    }
    got = SIZE_MAX;  // marks "this request is finished" for the reply below
  }
}

void confirmFirmware() {
  esp_ota_mark_app_valid_cancel_rollback();
  prefs.putBytes("fw", esp_ota_get_app_description()->app_elf_sha256, 32);
  pending = false;
  Serial.println("firmware confirmed");
}

void session(bool resetWifi) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char name[20], pop[9];
  snprintf(name, sizeof name, "PROV_frame_%02X%02X", mac[4], mac[5]);
  snprintf(pop, sizeof pop, "%02x%02x%02x%02x", mac[2] ^ 0x5a, mac[3] ^ 0xa5, mac[4] ^ 0x3c, mac[5] ^ 0xc3);

  radioOff = false;
  WiFi.onEvent(onWifiEvent);
  WiFi.setHostname("frame");
  WiFi.mode(WIFI_STA);
  wifi_config_t stored = {};
  esp_wifi_get_config(WIFI_IF_STA, &stored);
  if (stored.sta.ssid[0] && !resetWifi) {
#if TEST_BAD_FIRMWARE != 1  // bench only: 1 never joins Wi-Fi, 2 hangs, to test the rollback
    WiFi.begin();  // already paired: plain connect, Bluetooth never starts
#endif
  } else {
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM, WIFI_PROV_SECURITY_1, pop, name,
                            nullptr, nullptr, resetWifi);
  }

  bool setupShown = false;
  uint32_t start = millis();
  while (!gotIp) {
    delay(50);
    esp_task_wdt_reset();
#if TEST_BAD_FIRMWARE == 2
    while (true) {}
#endif
    if (provStarted && !setupShown) {
      setupShown = true;
      Serial.printf("setup name=%s pop=%s\n", name, pop);
      static char payload[120];
      snprintf(payload, sizeof payload, "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}", name, pop);
      esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
      cfg.display_func = [](esp_qrcode_handle_t qr) {
        static const char *const lines[] = {"1. On your phone, install the app", "    \"ESP BLE Provisioning\"",
                                            "2. In the app, scan this code", "3. Choose your Wi-Fi and",
                                            "    enter its password"};
        showText("Connect me to Wi-Fi", lines, 5, qr);
      };
      esp_qrcode_generate(&cfg, payload);
    }
    if (credFail) {  // wrong password: forget it and offer setup again
      WiFi.disconnect(true, true);
      ESP.restart();
    }
    if (millis() - start > (provStarted && !pending ? SETUP_MS : WIFI_TIMEOUT_MS)) return;  // give up, keep the current picture
  }
  Serial.printf("ip %s\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(true);  // modem sleep between beacons: about a third of the idle current

  if (setupShown) {
    static char ipLine[48];
    snprintf(ipLine, sizeof ipLine, "    (or http://%s)", WiFi.localIP().toString().c_str());
    const char *const lines[] = {"To add photos:", "1. Press the button on the frame",
                                 "2. On the same Wi-Fi, open", "    http://frame.local", ipLine};
    showText("Connected", lines, 5);
  }

  MDNS.begin("frame");
  MDNS.addService("http", "tcp", 80);
  server.on("/", HTTP_GET, [] {
    lastActivity = millis();
    server.send(200, "text/html", index_html);
  });
  server.on("/status", HTTP_GET, [] {
    uint32_t seqs[MAX_PHOTOS + 1];
    int soc = gauge(0x04), tz = prefs.getInt("tz", AUTO_TZ);  // SOC: the high byte is whole percent
    char json[300];
    snprintf(json, sizeof json,
             "{\"photos\":%d,\"max\":%d,\"battery_mv\":%u,\"battery_pct\":%d,\"show\":%u,\"date\":%u,\"tz\":\"%s\",\"key\":\"%s\",\"version\":\"%s\"}",
             listPhotos(seqs), MAX_PHOTOS, batteryMv(), soc < 0 ? -1 : soc >> 8, prefs.getUChar("show", 0),
             prefs.getUChar("date", 0), tz == AUTO_TZ ? "auto" : String(tz).c_str(), prefs.getString("key").c_str(), FW_VERSION);
    server.send(200, "application/json", json);
  });
  server.on("/settings", HTTP_POST, [] {
    String key;
    for (char c : server.arg("key"))
      if (isalnum(c) || c == '-' || c == '_') key += c;  // it goes into a URL and into JSON
    prefs.putUChar("show", server.arg("show") == "1");
    prefs.putUChar("date", constrain(server.arg("date").toInt(), 0, 3));
    prefs.putInt("tz", server.arg("tz") == "auto" ? AUTO_TZ : constrain(server.arg("tz").toInt(), -720, 840));
    prefs.putString("key", key.substring(0, 80));
    changed = true;
    lastActivity = millis();
    server.send(200, "text/plain", "saved");
  });
#if ROTATE_SECONDS < 86400  // bench builds only: fetch the frame-dithered artwork to look at it
  server.on("/art.bin", HTTP_GET, [] {
    File f = LittleFS.open("/art.bin");
    if (f) server.streamFile(f, "application/octet-stream");
    else server.send(404);
  });
#endif
  server.on("/updater", HTTP_POST, [] {  // restart into the updater app in the other slot
    toUpdater = !esp_ota_set_boot_partition(esp_ota_get_next_update_partition(nullptr));
    server.send(toUpdater ? 200 : 500, "text/plain", toUpdater ? "restarting into the updater" : "no updater installed");
  });
  server.on("/upload", HTTP_POST, [] {
    bool ok = uploaded && millis() - lastUpload < 1000;
    server.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "not a frame photo");
  }, handleUpload);
  server.begin();
  Serial.println("serving");
  if (pending) {  // joined Wi-Fi and serving: the new firmware is good
    confirmFirmware();
    esp_task_wdt_delete(nullptr);
  }

  // Ends when idle, 4 s after the last upload (the batch is done) or 30 s after a settings change.
  lastActivity = millis();
  uint32_t up = millis();
  while (millis() - lastActivity < (changed ? 30000 : SESSION_MS) && !(uploaded && millis() - lastUpload > 4000)) {
    server.handleClient();
    delay(2);
    if (toUpdater) {
      delay(500);  // let the reply go out
      ESP.restart();
    }
    if (digitalRead(BUTTON) == HIGH) up = millis();
    if (millis() - up > HOLD_RESET_MS) {  // held 5 s while the page is open: forget Wi-Fi, as at wake
      while (digitalRead(BUTTON) == LOW) delay(20);  // GPIO0 is a boot strapping pin: never restart while it is down
      WiFi.disconnect(true, true);
      ESP.restart();
    }
  }
  server.stop();
  if (online()) Serial.printf("synced %d\n", syncClock());
  bool art = prefs.getUChar("show", 0) && changed && !uploaded;
  if (art) fetchArt(millis() + 30000);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  radioOff = true;
  uint32_t seqs[MAX_PHOTOS + 1];
  int n = listPhotos(seqs);
  if (uploaded && n) showPhoto(seqs[n - 1]);
  else if (art) showFile("/art.bin");
  else if (changed) showPhoto(prefs.getUInt("cur", 0));  // redraw with the new date setting
}

// Keep a new firmware from the updater on probation; session() confirms it, sleepNow() rolls it back.
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  setCpuFrequencyMhz(80);  // plenty for this job, and the lowest speed Wi-Fi allows
  panelOff();
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, !NEOPIXEL_POWER_ON);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block on logging when no computer is attached
  prefs.begin("frame");
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
  pending = state == ESP_OTA_IMG_PENDING_VERIFY;
  uint8_t good[32] = {};
  prefs.getBytes("fw", good, sizeof good);
  // Leaving the updater without an upload also restarts this app as pending. If this exact build has
  // worked here before, trust it, or a Wi-Fi outage at that moment would roll back a good firmware.
  bool known = !memcmp(good, esp_ota_get_app_description()->app_elf_sha256, sizeof good);
  if (pending ? known : !known) confirmFirmware();
  if (pending) {  // a hang before it has proved itself also rolls back
    esp_task_wdt_init(60, true);
    esp_task_wdt_add(nullptr);
  }
  Serial.printf("wake cause=%d button=%d firmware %s ota state %d\n", (int)cause, digitalRead(BUTTON), FW_VERSION, (int)state);
  LittleFS.begin(true, FS_ROOT, 5, "photos");
  LittleFS.remove("/up.tmp");

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    online() ? daily() : rotate();
  } else {
    // Button still down 5 s after it woke us means "forget Wi-Fi". Count from boot, not from here: the
    // chip and file system take a moment to start, and people release at about 5 s.
    while (cause == ESP_SLEEP_WAKEUP_EXT0 && digitalRead(BUTTON) == LOW && millis() < HOLD_RESET_MS) delay(20);
    bool held = cause == ESP_SLEEP_WAKEUP_EXT0 && digitalRead(BUTTON) == LOW;  // still down at 5 s
    if (cause == ESP_SLEEP_WAKEUP_EXT0) Serial.printf("button %s at %lu ms\n", held ? "held" : "released", millis());
#ifdef TEST_FORGET_WIFI  // bench only: behave as if the button had been held
    held = true;
#endif
    session(held);
  }
  sleepNow();
}

void loop() {}
