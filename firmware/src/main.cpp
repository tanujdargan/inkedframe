// inkedframe: battery photo frame. Feather ESP32-S3 + Waveshare 7.3" Spectra 6 (E6).
//
// Wiring: CS 10, DC 9, RST 6, BUSY 5, DIN->MO(35), SCLK->SCK(36), VCC->3V
//
// Life cycle (everything happens in setup(), then deep sleep):
//   timer wake            -> show the next stored photo, radio off; with the date or artwork on,
//                            join Wi-Fi briefly, sync the clock, then draw (at 00:01 local time)
//   button / power-on     -> join Wi-Fi, serve http://frame.local for a few minutes,
//                            store uploads and settings, show the newest
//   button held 5 s       -> forget Wi-Fi and run Wi-Fi setup again
//   no Wi-Fi stored       -> Wi-Fi setup: the frame's own network, QR code on the panel
#include <Arduino.h>
#include <ESPmDNS.h>
#include <GxEPD2_7C.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <qrcode.h>
#include <rom/tjpgd.h>  // the JPEG decoder in the chip's ROM: costs no flash
#include "fonts.h"
#include "met.h"

#ifndef ROTATE_SECONDS
#define ROTATE_SECONDS 86400  // how often the stored photos rotate
#endif

#ifndef DAY_SECONDS
#define DAY_SECONDS 86400  // bench builds shorten the "day" to check the wake-time calibration
#endif
#ifndef WAKE_AT
#define WAKE_AT 60  // the nightly change: seconds after local midnight
#endif

// The frame can stand four ways (setting "rot", the GxEPD2 rotation): 0 landscape, 1 turned a quarter
// turn left, 2 upside down, 3 turned right. Photos and artwork are stored upright as the person sees
// them (480x800 in portrait, the same 128,000 bytes) and the panel driver rotates, so every overlay
// is laid out in the picture's own coordinates. W and H are the picture's width and height.
int W = 800, H = 480;
uint8_t rot;
void orient(int r) {
  rot = r & 3;
  W = rot & 1 ? 480 : 800;
  H = 1280 - W;
}
const size_t PHOTO_BYTES = 800 * 480 / 3;  // three pixels per byte: a * 36 + b * 6 + c
const int MAX_PHOTOS = 10;             // 10 x 125 KB, the artwork and an upload fit the 1.5 MB photos partition
const gpio_num_t BUTTON = GPIO_NUM_0;  // onboard BOOT button
#ifndef SESSION_SECONDS
#define SESSION_SECONDS 180
#endif
const uint32_t SESSION_MS = SESSION_SECONDS * 1000UL;  // upload window after a button press
const uint32_t SETUP_MS = 600000;      // how long Wi-Fi setup stays open
const uint32_t HOLD_RESET_MS = 3000;  // people asked to hold "5 seconds" let go at about 4 (measured), so trigger at 3
// Tune to the cell. Below LOW: one "please charge me" screen, then no refreshes until above CHARGED.
const uint16_t WARN_BATTERY_MV = 3550, LOW_BATTERY_MV = 3400, CHARGED_MV = 3900;
const uint32_t WIFI_TIMEOUT_MS = 20000;
const int AUTO_TZ = 9999;              // "tz" setting: minutes east of UTC, or this for "look it up"
const size_t MAX_JPEG = 600 * 1024;
const int LABEL_H = 50;  // the strip under museum art with its title and artist
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
volatile bool gotIp;
uint32_t lastActivity, lastUpload, byeAt;  // byeAt: when the page was closed
bool uploaded, changed, toUpdater, full;
uint32_t showSeq;  // shown when the session ends: the newest upload or the one picked with "Show now"
bool pending;  // a new firmware from the updater that has not yet proved itself
bool radioOff = true;  // light sleep is only safe while Wi-Fi is off
bool lowBattery;  // draw a small battery sign on the picture

const uint8_t PANEL_PINS[] = {10, 9, 6, SCK, MOSI};  // CS, DC, RST, clock, data

// A colour refresh takes about 30 s. Doze instead of spinning the CPU while the panel is busy.
DNSServer *setupDns;
WebServer *setupWeb;
volatile bool pressed;  // the button was pressed while the frame was busy drawing

// The onboard light: green while the page can be opened, amber (breathing) while drawing, blue
// (breathing) in Wi-Fi setup, off asleep. Dim on purpose: about 1 mA against the 90 mA the frame draws
// awake anyway. Off for the nightly timer wake, when nobody is watching.
bool watched;
void led(uint8_t r, uint8_t g, uint8_t b) {
  if (!watched) return;
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  delay(2);
  neopixelWrite(PIN_NEOPIXEL, r, g, b);
}
// One step of a slow breath, 4 s in and out, from the clock: callers step it whenever they are awake.
void breathe(uint8_t r, uint8_t g, uint8_t b) {
  static uint32_t last;
  if (!watched || millis() - last < 20) return;
  last = millis();
  float k = (1 - cosf(millis() % 4000 * (2 * PI / 4000))) / 2;
  k = 0.05f + 0.95f * k * k;  // squared: the eye is far more sensitive to steps at the dim end
  neopixelWrite(PIN_NEOPIXEL, r * k + 0.5f, g * k + 0.5f, b * k + 0.5f);
}

void whilePanelBusy(const void *) {
  if (!radioOff) {
    if (setupWeb) {  // keep answering the phone while the setup screen is still drawing
      setupDns->processNextRequest();
      setupWeb->handleClient();
    }
    breathe(40, 20, 0);
    delay(10);
    return;
  }
  if (digitalRead(BUTTON) == LOW) pressed = true;
  gpio_wakeup_enable(GPIO_NUM_5, GPIO_INTR_HIGH_LEVEL);  // BUSY goes high when the panel is done
  if (!pressed) gpio_wakeup_enable(BUTTON, GPIO_INTR_LOW_LEVEL);  // a press is remembered, see sleepNow()
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(watched ? 60000 : 1000000);  // watched: wake often enough to breathe smoothly
  esp_light_sleep_start();
  breathe(40, 20, 0);
  if (digitalRead(BUTTON) == LOW) pressed = true;
  gpio_wakeup_disable(GPIO_NUM_5);
  gpio_wakeup_disable(BUTTON);
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

// Per photo, in the settings: KEEP (never dropped for a new upload), TALL (made for portrait) and a
// pinned date, month * 100 + day (PIN).
const uint16_t KEEP = 0x8000, TALL = 0x4000, PIN = 0x3fff;
String metaKey(uint32_t seq) { return "m" + String(seq); }
uint16_t meta(uint32_t seq) { return prefs.getUShort(metaKey(seq).c_str(), 0); }
bool fits(uint32_t seq) { return !(meta(seq) & TALL) == (W > H); }  // made for the way the frame stands now

void removePhoto(uint32_t seq) {
  Serial.printf("remove %s\n", photoPath(seq).c_str());
  LittleFS.remove(photoPath(seq));
  prefs.remove(metaKey(seq).c_str());
}

// ---------- settings and clock ----------

bool online() {  // needs Wi-Fi once a day
  if (prefs.getUChar("date", 0) || prefs.getUChar("show", 0)) return true;
  uint32_t seqs[MAX_PHOTOS + 1];
  for (int i = listPhotos(seqs); i--;)
    if (meta(seqs[i]) & PIN) return true;  // a pinned photo needs the right date
  return false;
}
bool clockSet() { return time(nullptr) > 1700000000; }  // false after a power loss until NTP has run

time_t localNow() {
  int tz = prefs.getInt("tz", AUTO_TZ);
  return time(nullptr) + (tz == AUTO_TZ ? prefs.getInt("tzc", 0) : tz * 60);
}

// Each daily sleep aims at WAKE_AT from a freshly synced clock. s is the local time of day: a wake
// before noon has done today, so aim at tomorrow; one in the evening sleeps the few minutes left.
constexpr uint32_t untilWake(uint32_t s) { return DAY_SECONDS + WAKE_AT - s % DAY_SECONDS; }
static_assert(DAY_SECONDS != 86400 || (untilWake(60) == 86400 && untilWake(0) == 86460 && untilWake(86400 - 600) == 660 &&
                                       untilWake(86400 + 3600) == 82860), "");

// The deep-sleep timer and the clock kept through it both count an internal RC oscillator that is off
// by a percent or two (tens of minutes a day), the same way. So each sync after a sleep compares how
// long the kept clock says it slept with how long it really did, and sleepNow() scales the next sleep
// by that factor ("drift"). Kept in RTC memory through deep sleep:
RTC_DATA_ATTR double sleptAt, syncedAt;  // when it went to sleep and when it last synced; 0 if unknown
RTC_DATA_ATTR bool wokeEarly, artReady;  // an evening wake synced (and fetched the art) for the next
double bootClock;                        // the kept clock at boot
double clockNow() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

// NTP, then the UTC offset when the time zone is automatic. Wi-Fi must be up.
void startNtp() {  // ask early (as soon as Wi-Fi is up) so the answer is usually in before anyone waits for it
  static bool started;
  if (!started) configTime(0, 0, "pool.ntp.org", "time.google.com");
  started = true;
}

bool syncClock() {
  // lwIP waits a random 0-5 s before its first NTP request and only retries after 15 s, so one lost
  // reply used to cost the day's sync (seen on 3 of 25 wakes). Ask again ourselves at 6 s.
  uint32_t start = millis();
  startNtp();
  bool asked = false;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (millis() - start > 12000) return false;
    if (!asked && millis() - start > 6000) { asked = true; sntp_restart(); }
    delay(50);
  }
  double kept = bootClock + millis() / 1000.0, now = clockNow(), slept = kept - sleptAt;  // awake time is crystal-timed
  if (sleptAt && slept > DAY_SECONDS / 24) {
    float r = constrain((now - sleptAt) / slept, 0.95, 1.05), f = prefs.getFloat("drift", r);
    prefs.putFloat("drift", f + (r - f) * 0.75f);  // a little smoothing: the oscillator follows the temperature
    Serial.printf("drift: slept %.1f s by the kept clock, %.1f s really, factor %.5f, stored %.5f\n", slept, now - sleptAt, r,
                  prefs.getFloat("drift", 1));
  }
  sleptAt = 0;
  syncedAt = now;
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
  pressed = false;
  led(14, 7, 0);
  for (uint8_t pin : PANEL_PINS) gpio_hold_dis((gpio_num_t)pin);
  display.init(0, true, 2, false);
  display.epd2.setBusyCallback(whilePanelBusy);
  display.setRotation(rot);
  display.setFullWindow();
}

#if ROTATE_SECONDS < 86400  // bench builds: GET /render draws into memory instead, to look at it on a computer,
GFXcanvas16 *canvas;         // and with ?date=, ?t= and ?rot= in another date style, on another day and another way up,
int benchDate = -1;          // without saving them
time_t benchT;
bool benchPicsum;
#endif

int dateMode() {  // 0 off, 1 corner, 2 banner, 3 big, 4 split
#if ROTATE_SECONDS < 86400
  if (benchDate >= 0) return benchDate;
#endif
  return prefs.getUChar("date", 0);
}

// One refresh. GxEPD2 draws a quarter of the panel at a time, so draw() runs once per quarter.
template <class F> void paint(F draw) {
#if ROTATE_SECONDS < 86400
  if (canvas) return draw(*canvas);
#endif
  displayBegin();
  display.firstPage();
  do draw(display); while (display.nextPage());
  display.hibernate();
  panelOff();
  prefs.putUChar("screen", 0);  // what is on the panel: 0 the usual, 1 "plug me in", 2 a photo pinned to the day
}

void drawBattery(Adafruit_GFX &g) {  // top right: an almost empty battery
  if (!lowBattery) return;
  g.fillRect(W - 84, 16, 68, 40, GxEPD_WHITE);
  g.drawRect(W - 76, 24, 46, 24, GxEPD_BLACK);
  g.drawRect(W - 75, 25, 44, 22, GxEPD_BLACK);
  g.fillRect(W - 30, 30, 6, 12, GxEPD_BLACK);
  g.fillRect(W - 72, 28, 9, 16, GxEPD_RED);
}

// Text with its ink, not its cursor, at x: left edge (align -1), centre (0) or right edge (1). y is the
// baseline, or the top of the digits for the Num fonts. Returns the ink's width.
int ink(Adafruit_GFX &g, const GFXfont &f, const char *s, int16_t *x1 = nullptr) {  // width
  int16_t x, y1;
  uint16_t w, h;
  g.setFont(&f);
  g.setTextWrap(false);
  g.getTextBounds(s, 0, 0, x1 ? x1 : &x, &y1, &w, &h);
  return w;
}
int text(Adafruit_GFX &g, const GFXfont &f, const char *s, int x, int y, int align = -1) {
  int16_t x1;
  int w = ink(g, f, s, &x1);
  g.setTextColor(GxEPD_BLACK);
  g.setCursor(x - x1 - (align + 1) * w / 2, y);
  g.print(s);
  return w;
}
String fit(Adafruit_GFX &g, const GFXfont &f, String s, int w) {  // shortened with "..." to fit w
  while (s.length() > 4 && ink(g, f, s.c_str()) > w) {
    s = s.substring(0, s.length() - 4);
    s.trim();
    s += "...";
  }
  return s;
}
const int SMALL_CAP = 16, TEXT_CAP = 22, LARGE_CAP = 29, NUM100 = 100, NUM150 = 150;  // cap heights of fonts.h
const int SPLIT_H = 400;  // Split in portrait: the top 480x400 of the picture (where a greeting is), the date below

// Up to two lines of s, wrapped at spaces to fit w, ending at baseline y (down: starting there).
// Returns the baseline at the other end.
int wrap2(Adafruit_GFX &g, const GFXfont &f, String s, int x, int y, int w, int lead, bool down = false) {
  int cut = s.length();
  while (ink(g, f, s.substring(0, cut).c_str()) > w && s.lastIndexOf(' ', cut - 1) > 0) cut = s.lastIndexOf(' ', cut - 1);
  String rest = s.substring(cut);
  rest.trim();
  if (rest.length() && !down) y -= lead;
  text(g, f, fit(g, f, s.substring(0, cut), w).c_str(), x, y);
  if (rest.length()) text(g, f, fit(g, f, rest, w).c_str(), x, y + lead);
  return rest.length() && down ? y + lead : y;
}

// Today's date in black on white, so it reads over any picture. edge: the bottom of the picture, or with
// Split in portrait where its panel starts. label: museum art's title and artist go in Split's column.
void drawDate(Adafruit_GFX &g, int edge, bool label) {
  int mode = dateMode();
  time_t t = localNow();
#if ROTATE_SECONDS < 86400
  if (benchT) t = benchT;
#endif
  if (!mode || !clockSet()) return;
  tm d;
  gmtime_r(&t, &d);
  char day[12], mon[12], num[4], s[40];
  strftime(day, sizeof day, "%A", &d);
  strftime(mon, sizeof mon, "%B", &d);
  snprintf(num, sizeof num, "%d", d.tm_mday);
  if (mode == 1) {  // corner: "Thu 19 Nov", bottom right
    snprintf(s, sizeof s, "%.3s %d %.3s", day, d.tm_mday, mon);
    int w = ink(g, Text, s);
    g.fillRect(W - 24 - 18 - w - 18, edge - 24 - 16 - TEXT_CAP - 16, w + 36, TEXT_CAP + 32, GxEPD_WHITE);
    text(g, Text, s, W - 24 - 18, edge - 24 - 16, 1);
  } else if (mode == 2) {  // banner: weekday on the left, the date on the right; two lines where too narrow
    snprintf(s, sizeof s, "%d %s %d", d.tm_mday, mon, d.tm_year + 1900);
    bool two = ink(g, Text, day) + 48 + ink(g, Text, s) > W - 64;
    int h = two ? 112 : 64;
    g.fillRect(0, edge - h, W, h, GxEPD_WHITE);
    text(g, Text, day, 32, edge - h + 32 + TEXT_CAP / 2);
    text(g, Text, s, two ? 32 : W - 32, edge - 32 + TEXT_CAP / 2, two ? -1 : 1);
  } else if (mode == 3) {  // big: the day number, month and weekday beside it, bottom left
    int top = edge - 24 - 26 - NUM100, x = 24 + 26 + ink(g, Num100, num) + 22, mw = std::max(ink(g, Large, mon), ink(g, Text, day));
    g.fillRect(24, top - 26, x + mw + 26 - 24, NUM100 + 52, GxEPD_WHITE);
    text(g, Num100, num, 24 + 26, top);
    text(g, Large, mon, x, top + LARGE_CAP);
    text(g, Text, day, x, top + NUM100);
  } else {  // split: a white panel beside the picture (landscape) or under it (portrait), the date stacked
    const bool tall = W < H;
    const int x = tall ? 48 : 480 + 40, y0 = tall ? edge : 0, room = H - y0, block = 2 * LARGE_CAP + NUM150 + TEXT_CAP,
              gap = label ? 22 : std::min(40, (room - block) / 5), top = label ? 28 : y0 + (room - 3 * gap - block) / 2;
    g.fillRect(tall ? 0 : 480, y0, W, room, GxEPD_WHITE);
    text(g, Large, day, x, top + LARGE_CAP);
    text(g, Num150, num, x, top + LARGE_CAP + gap);
    text(g, Large, mon, x, top + LARGE_CAP + 2 * gap + NUM150 + LARGE_CAP);
    snprintf(s, sizeof s, "%d", d.tm_year + 1900);
    text(g, Text, s, x, top + 3 * gap + 2 * LARGE_CAP + NUM150 + TEXT_CAP);
    if (label) {  // museum art: title and artist at the bottom of the column, under a short rule
      String l = prefs.getString("label");
      int y = wrap2(g, Small, l.substring(0, l.indexOf('|')), x, H - 36 - 28, W - 40 - x, 26);
      text(g, Small, fit(g, Small, l.substring(l.indexOf('|') + 1), W - 40 - x).c_str(), x, H - 36);
      g.fillRect(x, y - SMALL_CAP - 16, 40, 2, GxEPD_BLACK);
    }
  }
  static bool logged;
  if (!logged) Serial.printf("date mode=%d %s %s %s\n", mode, day, num, mon);
  logged = true;
}

// Museum art: "title, artist" in a white strip along the bottom.
void drawLabel(Adafruit_GFX &g) {
  String l = prefs.getString("label"), t = l.substring(0, l.indexOf('|')), a = l.substring(l.indexOf('|') + 1);
  if (a.length()) a = ", " + a;
  g.fillRect(0, H - LABEL_H, W, LABEL_H, GxEPD_WHITE);
  int aw = ink(g, Small, a.c_str());
  t = fit(g, SmallBold, t, W - 64 - aw);
  text(g, SmallBold, t.c_str(), (W - ink(g, SmallBold, t.c_str()) - aw) / 2, H - LABEL_H / 2 + SMALL_CAP / 2);
  g.setFont(&Small);
  g.print(a);  // straight after the title
}

// Museum art in portrait is shown like a print: the whole work across the top (h rows), and under it
// the title and the artist. Returns where the white below begins.
int drawCaption(Adafruit_GFX &g, int h) {
  String l = prefs.getString("label");
  int y = wrap2(g, SmallBold, l.substring(0, l.indexOf('|')), 32, h + 28 + SMALL_CAP, W - 64, 26, true);
  text(g, Small, fit(g, Small, l.substring(l.indexOf('|') + 1), W - 64).c_str(), 32, y + 28);
  return y + 28 + 24;
}

bool showFile(const String &path, bool tall) {  // tall: the file was made for portrait
  File f = LittleFS.open(path);
  if (!f || f.size() != PHOTO_BYTES || tall != (W < H)) return false;  // never sideways or squashed
  Serial.printf("show %s\n", path.c_str());
  paint([&](Adafruit_GFX &g) {
    static uint8_t rows[800];  // three rows of the picture: 800 is not a multiple of 3, but 3 x 800 is
    String label = path == "/art.bin" ? prefs.getString("label") : "";
    bool split = dateMode() == 4 && clockSet(), print = label.length() && tall;
    int dx = split && !tall ? 160 : 0;  // split in landscape: the middle 480 columns, on the left
    f.seek(0);
    for (int y = 0; y < H; y += 3) {
      f.read(rows, W);
      for (int i = 0; i < 3 * W; i++) {
        uint8_t b = rows[i / 3], c = i % 3 == 0 ? b / 36 : i % 3 == 1 ? b / 6 % 6 : b % 6;
        g.drawPixel(i % W - dx, y + i / W, b < 216 ? COLORS[c] : GxEPD_WHITE);  // uploads are untrusted
      }
    }
    int edge = H;
    if (print) {  // the white under the work: dithering leaves stray ink there, as under the landscape label
      g.fillRect(0, prefs.getUShort("arth", H), W, H, GxEPD_WHITE);
      int e = drawCaption(g, prefs.getUShort("arth", H));
      if (split) edge = e;
    } else if (label.length() && !dx) {
      drawLabel(g);
      edge = H - LABEL_H;
    } else if (split && tall) edge = SPLIT_H;
    drawDate(g, edge, label.length() && dx);
    drawBattery(g);
  });
  return true;
}

// The "Today" page: day number, weekday, month and year, and the moon's phase (beside them in
// landscape, under them in portrait).
void drawToday(Adafruit_GFX &g, time_t t) {
  tm d;
  gmtime_r(&t, &d);
  char s[24];
  const bool tall = W < H;
  const int top = tall ? 72 : 76, x = tall ? 48 : 64, base = top + 211 + 44 + LARGE_CAP + 20 + TEXT_CAP;
  g.fillScreen(GxEPD_WHITE);
  snprintf(s, sizeof s, "%d", d.tm_mday);
  text(g, Num210, s, x, top);
  strftime(s, sizeof s, "%A", &d);
  text(g, Large, s, x, top + 211 + 44 + LARGE_CAP);
  strftime(s, sizeof s, "%B %Y", &d);
  text(g, Text, s, x, base);
  // Moon: days since the new moon of 6 Jan 2000 18:14 UTC, in mean synodic months. The real moon runs
  // up to about 14 hours ahead of or behind this mean, so the phase is right to within about a day.
  float p = fmodf((t / 86400.0 - 10962.76) / 29.530589, 1);
  const int r = 120, cx = tall ? W / 2 : 600, cy = tall ? (base + H - 56) / 2 : top + 120;
  g.fillCircle(cx, cy, r, GxEPD_BLACK);
  for (int y = -r; y <= r; y++) {  // the lit part, as seen from the northern hemisphere
    float hw = sqrtf(r * r - y * y), edge = hw * cosf(2 * PI * p);
    int a = p < 0.5f ? edge : -hw, b = p < 0.5f ? hw : -edge;
    if (b > a) g.drawFastHLine(cx + a, cy + y, b - a, GxEPD_WHITE);
  }
  g.drawCircle(cx, cy, r, GxEPD_BLACK);
  g.drawCircle(cx, cy, r + 1, GxEPD_BLACK);
  static const char *const names[] = {"New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
                                      "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
  text(g, Text, names[int(p * 8 + 0.5f) % 8], cx, tall ? cy + r + 56 : base, 0);
  drawBattery(g);
}

bool showToday() {
  if (!clockSet()) return false;  // keep the picture until the clock has been set
  Serial.println("show today");
  paint([](Adafruit_GFX &g) { drawToday(g, localNow()); });
  return true;
}

bool showPhoto(uint32_t seq) {
  if (!showFile(photoPath(seq), meta(seq) & TALL)) return false;
  prefs.putUInt("cur", seq);
  return true;
}
bool showArt() { return showFile("/art.bin", prefs.getUShort("arth", 0)); }  // arth: 0 made for landscape

// A plain text screen: a title, short lines (an empty one is a small gap) and optionally a figure 240 px
// wide and fh tall, the setup QR code or a battery: beside the text in landscape, above it in portrait.
void showText(const char *title, const char *const *lines, int n, std::function<void(Adafruit_GFX &, int, int)> figure = nullptr,
              int fh = 240) {
  paint([&](Adafruit_GFX &g) {
    const int F = 240, x = 56, above = figure && W < H ? fh + 64 : 0;
    int end = 0;  // the last line's baseline, from the title's
    for (int i = 0; i < n; i++) end += !i ? 64 : *lines[i] ? 42 : 21;
    int top = (H - above - LARGE_CAP - end) / 2, y = top + above + LARGE_CAP;
    g.fillScreen(GxEPD_WHITE);
    if (figure) figure(g, above ? (W - F) / 2 : W - x - F, above ? top : (H - fh) / 2);
    text(g, Large, title, x, y);
    for (int i = 0; i < n; i++) text(g, Text, lines[i], x, y += !i ? 64 : *lines[i] ? 42 : 21);
  });
}

// When no stored photo was made for the way the frame stands now.
void showSendPhotos() {
  uint32_t seqs[MAX_PHOTOS + 1];
  bool other = false;  // some were made for the other way
  for (int i = listPhotos(seqs); i--;) other |= !fits(seqs[i]);
  const char *const lines[] = {"To add photos, press", "the button on the frame,", "then on the same Wi-Fi",
                               "open http://frame.local", "", W > H ? "Photos sent for portrait" : "Photos sent for landscape",
                               "are kept, but not shown", "while I stand this way."};
  Serial.println("show: send photos");
  showText("Send me photos", lines, other ? 8 : 4);
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
  if (pressed) {  // pressed while drawing, when the frame could not answer: open the page now instead of sleeping
    Serial.println("button was pressed while drawing: new session");
    Serial.flush();
    while (digitalRead(BUTTON) == LOW) delay(20);  // GPIO0 is a boot strapping pin: never restart while it is down
    ESP.restart();
  }
  uint64_t secs = ROTATE_SECONDS;
#if ROTATE_SECONDS == 86400  // bench builds keep the short fixed interval
  if (online() && clockSet()) secs = untilWake(localNow() % DAY_SECONDS) / prefs.getFloat("drift", 1) + 0.5f;
#endif
  // A trusted start to measure this sleep by. Not after a power-on: that first sleep measured 0.4 to 0.8%
  // fast on the bench (3 of 3), against 0.1% for the sleeps after it.
  sleptAt = esp_reset_reason() == ESP_RST_DEEPSLEEP && syncedAt && clockNow() - syncedAt < DAY_SECONDS / 8 ? clockNow() : 0;
  Serial.printf("sleep %u s, awake %lu ms, screen %d, drift %.5f\n", (unsigned)secs, millis(), prefs.getUChar("screen", 0),
                prefs.getFloat("drift", 1));
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

void showFlat() {  // a large, nearly empty battery beside the words
  static const char *const lines[] = {"with the USB-C cable.", "I'll be back once", "I'm charged."};
  showText("Please plug me in", lines, 3, [](Adafruit_GFX &g, int x, int y) {
    for (int i = 0; i < 6; i++) g.drawRect(x + 8 + i, y + i, 200 - 2 * i, 100 - 2 * i, GxEPD_BLACK);
    g.fillRect(x + 208, y + 30, 16, 40, GxEPD_BLACK);
    g.fillRect(x + 24, y + 16, 30, 68, GxEPD_RED);
  }, 100);
}

// A refresh on a flat cell can brown out half-drawn. So at LOW the frame asks once to be charged, then
// keeps that screen without waking the panel or the radio until the cell is above CHARGED.
bool flat() {
  uint16_t mv = batteryMv();  // 0: no gauge
  lowBattery = mv && mv < WARN_BATTERY_MV;
  bool was = prefs.getUChar("screen", 0) == 1;
  Serial.printf("battery %u mV%s\n", mv, was ? ", flat" : "");
  if (was && mv < CHARGED_MV) return true;
  if (!mv || mv >= LOW_BATTERY_MV) return false;
  prefs.putUChar("screen", 1);  // before the refresh too, in case the cell gives out during it
  showFlat();
  prefs.putUChar("screen", 1);
  return true;
}

uint32_t pinnedToday() {  // the newest photo pinned to today's date, or 0
  time_t t = localNow();
  tm d;
  gmtime_r(&t, &d);
  uint32_t seqs[MAX_PHOTOS + 1];
  for (int i = listPhotos(seqs); clockSet() && i--;)
    if ((meta(seqs[i]) & PIN) == (d.tm_mon + 1) * 100 + d.tm_mday && fits(seqs[i])) return seqs[i];
  return 0;
}

void rotate() {
  uint32_t seqs[MAX_PHOTOS + 1];
  int n = 0;
  for (int i = 0, all = listPhotos(seqs); i < all; i++)
    if (!(meta(seqs[i]) & PIN) && fits(seqs[i])) seqs[n++] = seqs[i];  // pinned photos wait for their day
  // Nothing to rotate to. With the date on, or after a pinned day, redraw even one.
  if (n < (prefs.getUChar("date", 0) || prefs.getUChar("screen", 0) ? 1 : 2)) return;
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
  int cw, ch, ox, oy, h;  // the part of the (scaled) picture that fills the top h rows of the frame
};

UINT jpegIn(JDEC *jd, BYTE *buf, UINT n) {
  Jpeg &j = *(Jpeg *)jd->device;
  n = std::min<size_t>(n, j.len - j.pos);
  if (buf) memcpy(buf, j.data + j.pos, n);
  j.pos += n;
  return n;
}

// Each frame pixel takes the nearest source pixel: works for enlarging (museum images are 600 wide)
// and shrinking. Only the frame pixels whose source lies in this decoded block are visited.
UINT jpegOut(JDEC *jd, void *bitmap, JRECT *r) {
  Jpeg &j = *(Jpeg *)jd->device;
  const uint8_t *p = (const uint8_t *)bitmap;
  for (int dy = std::max(0, ((r->top - j.oy) * j.h + j.ch - 1) / j.ch); dy < j.h; dy++) {
    int sy = j.oy + dy * j.ch / j.h;
    if (sy > r->bottom) break;
    for (int dx = std::max(0, ((r->left - j.ox) * W + j.cw - 1) / j.cw); dx < W; dx++) {
      int sx = j.ox + dx * j.cw / W;
      if (sx > r->right) break;
      if (sy >= r->top && sx >= r->left) memcpy(j.rgb + (dy * W + dx) * 3, p + ((sy - r->top) * (r->right - r->left + 1) + sx - r->left) * 3, 3);
    }
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
  static uint8_t rows[800];  // three rows (W bytes), three pixels per byte
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
    size_t n = (y % 3 + 1) * W / 3;  // portrait: 800 rows are not a multiple of 3, the last write has two
    if (y % 3 == 2 || y == H - 1) ok = f.write(rows, n) == n;
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
  String src = "picsum.photos/" + String(W) + "/" + String(H) + ".jpg?random=" + String((uint32_t)(time(nullptr) / ROTATE_SECONDS));
  String key = prefs.getString("key"), url, label;
#if ROTATE_SECONDS < 86400
  if (benchPicsum) key = "";
#endif
  if (!prefs.getUChar("src", 0)) {  // museum art: a different work from the list each day
    int prev = prefs.getUShort("met", 0), i = esp_random() % (MET_COUNT - 1);
    i += i >= prev;
    const char *p = MET;
    for (int k = 0; k < i; k++) p = strchr(p, '\n') + 1;
    String line(p, strchr(p, '\n') - p);
    int bar = line.indexOf('|');
    url = "https://images.metmuseum.org/CRDImages/" + line.substring(0, 2) + "/web-large" + line.substring(2, bar);
    label = line.substring(bar + 1);
    prefs.putUShort("met", i);
  } else if (key.length()) {
    String json = http.begin(tls, "https://api.unsplash.com/photos/random?query=landscape&orientation=" + String(W > H ? "landscape" : "portrait") + "&client_id=" + key) &&
                  http.GET() == 200 ? http.getString() : "";
    http.end();
    int i = json.indexOf("\"raw\":\"");
    if (i < 0) {
      Serial.println("art: unsplash request failed");
      return false;
    }
    src = json.substring(i + 7, json.indexOf('"', i + 7));
    src.replace("\\u0026", "&");
    src += "&w=" + String(W) + "&h=" + String(H) + "&fit=crop&fm=jpg&q=80";
  }
  // picsum and Unsplash only send progressive JPEGs, which the ROM decoder cannot read: wsrv.nl, a free
  // image proxy, re-encodes them as baseline. The museum's images are baseline already.
  if (!url.length()) {
    url = "https://wsrv.nl/?output=jpg&q=85&url=";
    for (char c : src) {
      char e[4];
      snprintf(e, sizeof e, "%%%02X", (uint8_t)c);
      url += isalnum(c) || strchr("-._~/:", c) ? String(c) : String(e);
    }
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
  Serial.printf("art: %s\nart: http %d, %u of %d bytes in %lu ms, heap %u, psram found %d free %u\n", url.c_str(), code, got, len,
                millis() - t0, ESP.getFreeHeap(), psramFound(), ESP.getFreePsram());
  bool ok = jpg && (len > 0 ? got == (size_t)len : got < cap);
  JDEC jd;
  Jpeg j = {jpg, got, 0, nullptr};
  void *work = malloc(4096);
  JRESULT r = ok && work ? jd_prepare(&jd, jpegIn, work, 4096, &j) : JDR_PAR;
  ok = r == JDR_OK;  // progressive JPEGs fail here (JDR_FMT3)
  if (ok) {
    // Landscape: museum art leaves a strip for its label, except with Split, which shows it beside the
    // picture. Portrait: museum art keeps its whole width like a print (with Split no taller than 340, to
    // leave room for the date under its caption); random photos fill the frame, or Split's top 400 rows.
    bool split = dateMode() == 4, tall = W < H;
    int whole = (long)jd.height * W / jd.width;
    j.h = !tall ? (label.length() && !split ? H - LABEL_H : H) : label.length() ? std::min(whole, split ? 340 : H) : split ? SPLIT_H : H;
    int scale = 0;  // the decoder can shrink by 1/2, 1/4, 1/8 while staying at least frame size
    while (scale < 3 && (jd.width >> (scale + 1)) >= W && (jd.height >> (scale + 1)) >= j.h) scale++;
    int sw = jd.width >> scale, sh = jd.height >> scale;
    j.cw = std::min(sw, sh * W / j.h);
    j.ch = j.cw * j.h / W;
    j.ox = (sw - j.cw) / 2;
    j.oy = (sh - j.ch) * 45 / 100;  // same crop as the upload page
    j.rgb = (uint8_t *)ps_malloc(W * H * 3);
    if (j.rgb) memset(j.rgb, 255, W * H * 3);  // white under the label
    setCpuFrequencyMhz(240);  // decoding and dithering are the slow part: full speed, then back to 80
    r = j.rgb ? jd_decomp(&jd, jpegOut, scale) : JDR_MEM1;
    ok = r == JDR_OK;
  }
  Serial.printf("art: decode %d (%ux%u) at %lu ms\n", r, ok ? jd.width : 0, ok ? jd.height : 0, millis() - t0);
  free(work);
  free(jpg);
  ok = ok && ditherToFile(j.rgb, "/art.bin");
  setCpuFrequencyMhz(80);
  if (ok) prefs.putString("label", label);
  if (ok) prefs.putUShort("arth", W < H ? j.h : 0);  // which way it was made for, and how tall it is
  free(j.rgb);
  Serial.printf("art: %s at %lu ms\n", ok ? "saved" : "failed, keeping the current picture", millis() - t0);
  return ok;
}

// ---------- timer wake with the date or artwork on: one short Wi-Fi visit a day ----------

void daily() {
  uint32_t deadline = millis() + 30000;  // a dead router or internet must not keep the frame awake
  int show = prefs.getUChar("show", 0);  // 0 my photos, 1 artwork, 2 today
  bool early = wokeEarly, fresh = artReady;  // the last wake came just before midnight, synced and fetched
  wokeEarly = artReady = false;
  Serial.printf("kept clock %ld%s\n", (long)time(nullptr), early ? ", synced on the wake before" : "");
  if (!early) {
    radioOff = false;
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    while (WiFi.status() != WL_CONNECTED && millis() < deadline - 20000) delay(50);  // up to 10 s
    uint32_t joined = millis();
    bool synced = WiFi.status() == WL_CONNECTED && syncClock();
    long late = (localNow() - millis() / 1000 + DAY_SECONDS / 2 - WAKE_AT) % DAY_SECONDS - DAY_SECONDS / 2;  // the wake, from the target
    Serial.printf("wifi %d at %lu ms, synced %d at %lu ms, clock %ld, local day second %ld (woke %ld s from the target), heap %u\n",
                  WiFi.status() == WL_CONNECTED, joined, synced, millis(), (long)time(nullptr), (long)(localNow() % DAY_SECONDS), late,
                  ESP.getFreeHeap());
#if ROTATE_SECONDS == 86400
    // Woke before midnight: fetch tonight's art now; sleepNow() sleeps the rest, and that wake needs no Wi-Fi.
    wokeEarly = clockSet() && localNow() % DAY_SECONDS >= DAY_SECONDS / 2;
#endif
    fresh = show == 1 && (wokeEarly || !pinnedToday()) && synced && fetchArt(deadline);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    radioOff = true;
    artReady = wokeEarly && fresh;
    if (wokeEarly) return;
  }
  uint32_t pin = pinnedToday();
  bool art = show == 1 && !pin;
  if (pin && showFile(photoPath(pin), meta(pin) & TALL)) prefs.putUChar("screen", 2);  // instead of the usual; rotation resumes tomorrow
  else if (show == 2) showToday();
  else if (!art) rotate();
  else if (fresh || prefs.getUChar("date", 0) || prefs.getUChar("screen", 0)) showArt();  // failed fetch: keep the picture
}

// ---------- upload session ----------

void onWifiEvent(arduino_event_t *e) {
  if (e->event_id == ARDUINO_EVENT_WIFI_STA_GOT_IP) gotIp = true;
}

void active() {
  lastActivity = millis();
  byeAt = 0;  // a page reload says goodbye, then asks again
}

void handleUpload() {
  static File f;
  static size_t got;
  HTTPUpload &up = server.upload();
  active();
  if (up.status == UPLOAD_FILE_START) {
    uint32_t seqs[MAX_PHOTOS + 1];
    int n = listPhotos(seqs), drop = n - MAX_PHOTOS + 1;
    for (int i = 0; i < n && drop > 0; i++)  // make room: drop the oldest that is not kept or pinned
      if (!(meta(seqs[i]) & (KEEP | PIN))) removePhoto(seqs[i]), drop--;
    full = drop > 0;  // all kept or pinned: refuse
    f = full ? File() : LittleFS.open("/up.tmp", "w");
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
      int pin = server.arg("pin").toInt();  // month * 100 + day
      if (!(pin % 100 && pin % 100 < 32 && pin > 100 && pin < 1300)) pin = 0;
      uint16_t m = pin | (server.arg("o") == "1" ? TALL : 0);  // o=1: made for portrait
      if (uploaded && m) prefs.putUShort(metaKey(seq).c_str(), m);
      if (uploaded && !pin) showSeq = seq;  // a pinned photo waits for its day
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

// ---------- first-time Wi-Fi setup: the frame's own network and a small form ----------
// No app needed: the phone's camera joins the network from the QR code, the phone then opens the
// form by itself (every DNS name answers with the frame, which phones treat as a sign-in page).

const char SETUP_SSID[] = "Photo Frame";
const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Photo frame setup</title>
<style>body{font:18px system-ui,sans-serif;margin:0 auto;padding:24px 16px;max-width:420px;color:#222;background:#faf8f4}
label{display:block;margin:16px 0 6px}select,input,button{font:inherit;width:100%;padding:14px;box-sizing:border-box;border-radius:10px;border:1px solid #999}
button{margin-top:24px;background:#222;color:#fff;border:0}</style>
<h1>Photo frame</h1><p>Choose your home Wi-Fi so the frame can receive photos.</p>
<form id="f"><label for="s">Wi-Fi network</label><select id="s" name="ssid">%OPTIONS%</select>
<label for="p">Wi-Fi password</label><input id="p" name="pass" type="text" autocapitalize="none" autocorrect="off" spellcheck="false">
<button>Connect</button></form><p id="m" role="status" aria-live="polite"></p>
<script>
const f=document.getElementById('f'),m=document.getElementById('m');
f.onsubmit=async e=>{e.preventDefault();m.textContent='Connecting. This takes up to 20 seconds.';
try{await fetch('/join',{method:'POST',body:new URLSearchParams(new FormData(f))});
for(;;){await new Promise(r=>setTimeout(r,2000));const t=await(await fetch('/state')).text();
if(t=='ok'){m.textContent='Connected. The frame will show what to do next. You can close this page.';return}
if(t=='failed'){m.textContent='That did not work. Check the password and try again.';return}}}
catch(x){m.textContent='Look at the frame: in about a minute it shows Connected. If it still shows the setup screen, try again.'}};
</script></html>)HTML";

const char *setupPass() {  // different for each frame, worked out from its MAC address
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  static char pass[9];
  snprintf(pass, sizeof pass, "%02x%02x%02x%02x", mac[2] ^ 0x5a, mac[3] ^ 0xa5, mac[4] ^ 0x3c, mac[5] ^ 0xc3);
  return pass;
}

void showSetup() {  // the QR code joins the phone to the frame's network
  static char qrText[64], passLine[40];
  snprintf(qrText, sizeof qrText, "WIFI:T:WPA;S:%s;P:%s;;", SETUP_SSID, setupPass());
  snprintf(passLine, sizeof passLine, "password %s", setupPass());
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = [](esp_qrcode_handle_t qr) {
    static const char *const lines[] = {"Point your phone's camera", "at this code and tap Join.", "Then choose your Wi-Fi",
                                        "and type its password.", "", "No camera? Join the Wi-Fi", "network Photo Frame,", passLine};
    showText("Connect me to Wi-Fi", lines, 8, [qr](Adafruit_GFX &g, int x0, int y0) {
      int size = esp_qrcode_get_size(qr), px = 240 / size;
      x0 += (240 - size * px) / 2;
      y0 += (240 - size * px) / 2;
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
          if (esp_qrcode_get_module(qr, x, y)) g.fillRect(x0 + x * px, y0 + y * px, px, px, GxEPD_BLACK);
    });
  };
  esp_qrcode_generate(&cfg, qrText);
}

void showConnected(const char *ip) {
  static char ipLine[48];
  snprintf(ipLine, sizeof ipLine, "(or http://%s)", ip);
  const char *const lines[] = {"To add photos, press", "the button on the frame,", "then on the same Wi-Fi",
                               "open http://frame.local", ipLine};
  showText("Connected", lines, 5);
}

bool wifiSetup() {  // true once the frame has joined the home network
  const char *pass = setupPass();
  WiFi.mode(WIFI_AP_STA);
  String options;
  for (int i = 0, n = WiFi.scanNetworks(); i < n; i++) {
    String name = WiFi.SSID(i);
    name.replace("&", "&amp;");
    name.replace("<", "&lt;");
    if (name.length() && options.indexOf(">" + name + "<") < 0) options += "<option>" + name + "</option>";
  }
  WiFi.softAP(SETUP_SSID, pass);
  DNSServer dns;
  WebServer portal(80);
  int state = 0;  // 0 waiting for the form, 1 trying to join, 2 failed
  uint32_t tryAt = 0;
  dns.start(53, "*", WiFi.softAPIP());
  portal.on("/state", [&] { portal.send(200, "text/plain", gotIp ? "ok" : state == 2 ? "failed" : "trying"); });
  portal.on("/join", HTTP_POST, [&] {
    portal.send(200, "text/plain", "trying");
    WiFi.begin(portal.arg("ssid").c_str(), portal.arg("pass").c_str());
    state = 1;
    tryAt = millis();
  });
  portal.onNotFound([&] {
    String page = FPSTR(SETUP_HTML);
    page.replace("%OPTIONS%", options);
    portal.send(200, "text/html", page);
  });
  portal.begin();
  setupDns = &dns;
  setupWeb = &portal;
  Serial.printf("setup network \"%s\" password %s, %d networks seen\n", SETUP_SSID, pass, WiFi.scanComplete());

  showSetup();
  led(0, 0, 20);

  uint32_t start = millis();
  while (millis() - start < SETUP_MS && !(gotIp && state != 1)) {
    dns.processNextRequest();
    portal.handleClient();
    esp_task_wdt_reset();
    breathe(0, 0, 40);
    delay(2);
    if (state == 1 && gotIp) {  // joined: give the page 6 s to hear "ok" before our network goes
      uint32_t t = millis();
      while (millis() - t < 6000) {
        dns.processNextRequest();
        portal.handleClient();
        delay(2);
      }
      state = 0;
    } else if (state == 1 && millis() - tryAt > 20000) {  // wrong password: forget it so the next wake offers setup again
      state = 2;
      WiFi.disconnect(false, true);
    }
  }
  setupDns = nullptr;
  setupWeb = nullptr;
  portal.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  return gotIp;
}

void session(bool resetWifi) {
  radioOff = false;
  WiFi.onEvent(onWifiEvent);
  WiFi.setHostname("frame");
  WiFi.mode(WIFI_STA);
  wifi_config_t stored = {};
  esp_wifi_get_config(WIFI_IF_STA, &stored);
  bool setupShown = false;
  if (stored.sta.ssid[0] && !resetWifi) {
#if TEST_BAD_FIRMWARE != 1  // bench only: 1 never joins Wi-Fi, 2 hangs, to test the rollback
    WiFi.begin();
#endif
  } else if (!pending) {  // a firmware still on trial must not sit in setup; it rolls back below
    if (resetWifi) WiFi.disconnect(false, true);
    if (!wifiSetup()) return;  // nobody set it up: keep the current picture and sleep
    setupShown = true;
  }

  uint32_t start = millis();
  while (!gotIp) {
    delay(50);
    esp_task_wdt_reset();
#if TEST_BAD_FIRMWARE == 2
    while (true) {}
#endif
    if (millis() - start > WIFI_TIMEOUT_MS) return;  // give up, keep the current picture
  }
  Serial.printf("ip %s\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(true);  // modem sleep between beacons: about a third of the idle current
  startNtp();

  if (setupShown) showConnected(WiFi.localIP().toString().c_str());

  MDNS.begin("frame");
  MDNS.addService("http", "tcp", 80);
  server.on("/", HTTP_GET, [] {
    active();
    server.send(200, "text/html", index_html);
  });
  server.on("/status", HTTP_GET, [] {
    active();
    uint32_t seqs[MAX_PHOTOS + 1];
    int soc = gauge(0x04), tz = prefs.getInt("tz", AUTO_TZ), n = listPhotos(seqs);  // SOC: the high byte is whole percent
    char json[600];
    int len = snprintf(json, sizeof json,
             "{\"photos\":%d,\"max\":%d,\"battery_mv\":%u,\"battery_pct\":%d,\"show\":%u,\"src\":%u,\"date\":%u,\"rot\":%u,\"tz\":\"%s\",\"key\":\"%s\",\"version\":\"%s\",\"list\":[",
             n, MAX_PHOTOS, batteryMv(), soc < 0 ? -1 : soc >> 8, prefs.getUChar("show", 0),
             prefs.getUChar("src", 0), prefs.getUChar("date", 0), rot, tz == AUTO_TZ ? "auto" : String(tz).c_str(), prefs.getString("key").c_str(), FW_VERSION);
    for (int i = n - 1; i >= 0; i--) len += snprintf(json + len, sizeof json - len, "[%u,%u]%s", seqs[i], meta(seqs[i]), i ? "," : "]}");
    if (!n) strcat(json, "]}");
    server.send(200, "application/json", json);
  });
  server.on("/settings", HTTP_POST, [] {
    String key;
    for (char c : server.arg("key"))
      if (isalnum(c) || c == '-' || c == '_') key += c;  // it goes into a URL and into JSON
    prefs.putUChar("show", constrain(server.arg("show").toInt(), 0, 2));
    prefs.putUChar("src", server.arg("src") == "1");  // 0 museum art, 1 random photos
    prefs.putUChar("date", constrain(server.arg("date").toInt(), 0, 4));
    if (server.hasArg("rot")) prefs.putUChar("rot", server.arg("rot").toInt() & 3);  // a page from before has none
    orient(prefs.getUChar("rot", 0));
    prefs.putInt("tz", server.arg("tz") == "auto" ? AUTO_TZ : constrain(server.arg("tz").toInt(), -720, 840));
    prefs.putString("key", key.substring(0, 80));
    changed = true;
    active();
    server.send(200, "text/plain", "saved");
  });
#if ROTATE_SECONDS < 86400  // bench builds only: fetch the frame-dithered artwork to look at it
  server.on("/art.bin", HTTP_GET, [] {
    File f = LittleFS.open("/art.bin");
    if (f) server.streamFile(f, "application/octet-stream");
    else server.send(404);
  });
#endif
#if ROTATE_SECONDS < 86400
  server.on("/render", HTTP_GET, [] {  // bench: what the panel would show, packed like a photo
    lowBattery = server.hasArg("low");
    benchDate = server.hasArg("date") ? server.arg("date").toInt() : -1;
    benchT = server.arg("t").toInt();
    if (server.hasArg("rot")) orient(server.arg("rot").toInt());
    if (server.hasArg("src")) prefs.putUChar("src", server.arg("src") == "1");
    benchPicsum = server.hasArg("picsum");
    if (server.hasArg("fetch")) Serial.printf("bench fetch %d\n", fetchArt(millis() + 30000));  // for this way up and date style
    if (server.hasArg("label")) prefs.putString("label", server.arg("label"));  // to try long titles
    canvas = new GFXcanvas16(800, 480);  // the panel as it is wired, whichever way the frame stands
    canvas->setRotation(rot);
    uint8_t *out = (uint8_t *)ps_malloc(PHOTO_BYTES);
    active();
    String w = server.arg("w");
    if (w == "flat") showFlat();
    else if (w == "art") showArt();
    else if (w == "today") paint([](Adafruit_GFX &g) { drawToday(g, benchT); });
    else if (w == "setup") showSetup();
    else if (w == "connected") showConnected("10.0.0.157");
    else if (w == "send") showSendPhotos();
    else showFile(photoPath(server.arg("seq").toInt()), meta(server.arg("seq").toInt()) & TALL);
    for (int i = 0; i < 800 * 480; i++) {
      int k = 1;
      for (int j = 0; j < 6; j++)
        if (COLORS[j] == canvas->getBuffer()[i]) k = j;
      out[i / 3] = (i % 3 ? out[i / 3] * 6 : 0) + k;
    }
    server.send_P(200, "application/octet-stream", (const char *)out, PHOTO_BYTES);
    free(out);
    benchDate = -1;
    benchT = 0;
    benchPicsum = false;
    orient(prefs.getUChar("rot", 0));
    delete canvas;
    canvas = nullptr;
  });
#endif
  server.on("/bye", HTTP_POST, [] {
    byeAt = millis();
    server.send(204);
  });
  server.on("/updater", HTTP_POST, [] {  // restart into the updater app in the other slot
    toUpdater = !esp_ota_set_boot_partition(esp_ota_get_next_update_partition(nullptr));
    server.send(toUpdater ? 200 : 500, "text/plain", toUpdater ? "restarting into the updater" : "no updater installed");
  });
  server.on("/upload", HTTP_POST, [] {
    bool ok = uploaded && millis() - lastUpload < 1000;
    server.send(ok ? 200 : 400, "text/plain", ok ? "ok" : full ? "full" : "not a frame photo");
  }, handleUpload);
  server.on("/photo", HTTP_GET, [] {  // ?seq=
    active();
    File f = LittleFS.open(photoPath(server.arg("seq").toInt()));
    if (f) server.streamFile(f, "application/octet-stream");
    else server.send(404);
  });
  server.on("/photo", HTTP_POST, [] {  // ?seq= & do=delete|show|keep|unkeep
    active();
    uint32_t seq = server.arg("seq").toInt();
    String a = server.arg("do");
    if (!LittleFS.exists(photoPath(seq))) return server.send(404);
    if (a == "delete") removePhoto(seq);
    else if (a == "show") showSeq = seq;
    else prefs.putUShort(metaKey(seq).c_str(), a == "keep" ? meta(seq) | KEEP : meta(seq) & ~KEEP);
    server.send(200, "text/plain", "ok");
  });
  server.begin();
  led(0, 12, 0);
  Serial.println("serving");
  if (pending) {  // joined Wi-Fi and serving: the new firmware is good
    confirmFirmware();
    esp_task_wdt_delete(nullptr);
  }

  // Ends when idle, 4 s after the last upload (the batch is done), 5 s after a settings change or 2 s
  // after the page was closed.
  lastActivity = millis();
  uint32_t up = millis();
  while (millis() - lastActivity < (changed ? 5000 : SESSION_MS) && !(uploaded && millis() - lastUpload > 4000) &&
         !(byeAt && millis() - byeAt > 2000)) {
    server.handleClient();
    delay(2);
    if (toUpdater) {
      delay(500);  // let the reply go out
      ESP.restart();
    }
    if (digitalRead(BUTTON) == HIGH) up = millis();
    if (millis() - up > HOLD_RESET_MS) {  // held while the page is open: forget Wi-Fi, as at wake
      led(0, 0, 40);
      while (digitalRead(BUTTON) == LOW) delay(20);  // GPIO0 is a boot strapping pin: never restart while it is down
      WiFi.disconnect(true, true);
      ESP.restart();
    }
  }
  server.stop();
  if (online()) Serial.printf("synced %d\n", syncClock());
  bool art = prefs.getUChar("show", 0) == 1 && changed && !showSeq;
  if (art) fetchArt(millis() + 30000);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  radioOff = true;
  if (showSeq && showPhoto(showSeq)) return;
  if (art && showArt()) return;
  if (changed && prefs.getUChar("show", 0) == 2 && showToday()) return;
  if (!changed && !showSeq) return;
  // Redraw with the new settings (perhaps another way up): the current photo, or the newest made for
  // this way up, or a screen asking for photos. Never a picture made for the other way.
  uint32_t seqs[MAX_PHOTOS + 1];
  if (showPhoto(prefs.getUInt("cur", 0))) return;
  for (int i = listPhotos(seqs); i--;)
    if (!(meta(seqs[i]) & PIN) && showPhoto(seqs[i])) return;
  showSendPhotos();
}

// Keep a new firmware from the updater on probation; session() confirms it, sleepNow() rolls it back.
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
  bootClock = clockNow() - millis() / 1000.0;  // before any sync: the clock kept through the sleep
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  watched = cause != ESP_SLEEP_WAKEUP_TIMER;
  setCpuFrequencyMhz(80);  // plenty for this job, and the lowest speed Wi-Fi allows
  panelOff();
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, !NEOPIXEL_POWER_ON);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block on logging when no computer is attached
  prefs.begin("frame");
  orient(prefs.getUChar("rot", 0));
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
    if (!flat()) online() ? daily() : rotate();
  } else {
    // Button still down 5 s after it woke us means "forget Wi-Fi". Count from boot, not from here: the
    // chip and file system take a moment to start, and people release at about 5 s.
    while (cause == ESP_SLEEP_WAKEUP_EXT0 && digitalRead(BUTTON) == LOW && millis() < HOLD_RESET_MS) delay(20);
    bool held = cause == ESP_SLEEP_WAKEUP_EXT0 && digitalRead(BUTTON) == LOW;  // still down at 3 s
    if (held) led(0, 0, 40);  // blue: seen, you can let go
    if (cause == ESP_SLEEP_WAKEUP_EXT0) Serial.printf("button %s at %lu ms\n", held ? "held" : "released", millis());
#ifdef TEST_FORGET_WIFI  // bench only: behave as if the button had been held
    held = true;
#endif
    session(held);
  }
  sleepNow();
}

void loop() {}
