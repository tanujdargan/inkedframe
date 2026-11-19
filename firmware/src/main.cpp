// inkedframe: battery photo frame. Feather ESP32-S3 + Waveshare 7.3" Spectra 6 (E6).
//
// Wiring: CS 10, DC 9, RST 6, BUSY 5, DIN->MO(35), SCLK->SCK(36), VCC->3V
//
// Life cycle (everything happens in setup(), then deep sleep):
//   timer wake            -> show the next stored photo, radio off
//   button / power-on     -> join Wi-Fi, serve http://frame.local for a few minutes,
//                            store uploads, show the newest
//   button held 5 s       -> forget Wi-Fi and run Bluetooth setup again
//   no Wi-Fi stored       -> Bluetooth setup, QR code on the panel
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <GxEPD2_7C.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_wifi.h>
#include <qrcode.h>

#ifndef ROTATE_SECONDS
#define ROTATE_SECONDS 86400  // how often the stored photos rotate
#endif

const int W = 800, H = 480;
const size_t PHOTO_BYTES = W * H / 2;  // 4 bits per pixel
const int MAX_PHOTOS = 10;             // 10 x 192 KB fits the 2.2 MB photos partition
const gpio_num_t BUTTON = GPIO_NUM_0;  // onboard BOOT button
#ifndef SESSION_SECONDS
#define SESSION_SECONDS 180
#endif
const uint32_t SESSION_MS = SESSION_SECONDS * 1000UL;  // upload window after a button press
const uint32_t SETUP_MS = 600000;      // how long Bluetooth setup stays open
const uint32_t HOLD_RESET_MS = 5000;
const uint16_t LOW_BATTERY_MV = 3400;  // tune to the cell; below this the frame stops rotating
const uint32_t WIFI_TIMEOUT_MS = 20000;
const char *FS_ROOT = "/littlefs";

GxEPD2_7C<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT / 4>
  display(GxEPD2_730c_GDEP073E01(10, 9, 6, 5));

// Photo files hold one of these indexes per nibble; same order as PALETTE in index.html.
const uint16_t COLORS[] = {GxEPD_BLACK, GxEPD_WHITE, GxEPD_YELLOW, GxEPD_RED, GxEPD_BLUE, GxEPD_GREEN};

extern const char index_html[] asm("_binary_src_index_html_start");

Preferences prefs;
WebServer server(80);
volatile bool provStarted, gotIp, credFail;
uint32_t lastActivity, lastUpload;
bool uploaded;
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

bool showPhoto(uint32_t seq) {
  File f = LittleFS.open(photoPath(seq));
  if (!f || f.size() != PHOTO_BYTES) return false;
  Serial.printf("show %u\n", (unsigned)seq);
  displayBegin();
  static uint8_t row[W / 2];
  display.firstPage();
  do {
    f.seek(0);
    for (int y = 0; y < H; y++) {
      f.read(row, sizeof row);
      for (int x = 0; x < W; x++) {
        uint8_t c = (x & 1) ? (row[x / 2] & 0x0F) : (row[x / 2] >> 4);
        display.drawPixel(x, y, c < 6 ? COLORS[c] : GxEPD_WHITE);  // uploads are untrusted
      }
    }
  } while (display.nextPage());
  display.hibernate();
  panelOff();
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

uint16_t batteryMv() {  // MAX17048 fuel gauge, VCELL register; 0 if absent
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, HIGH);
  delay(10);
  Wire.begin();
  Wire.beginTransmission(0x36);
  Wire.write(0x02);
  if (Wire.endTransmission(false) || Wire.requestFrom(0x36, 2) != 2) return 0;
  uint16_t raw = Wire.read() << 8;
  raw |= Wire.read();
  return raw * 5UL / 64;  // 78.125 uV per bit
}

[[noreturn]] void sleepNow() {
  Serial.println("sleep");
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
  esp_sleep_enable_timer_wakeup(ROTATE_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}

// ---------- timer wake: next photo, no radio ----------

void rotate() {
  uint32_t seqs[MAX_PHOTOS + 1];
  int n = listPhotos(seqs);
  if (n < 2) return;  // nothing to rotate to
  uint16_t mv = batteryMv();
  if (mv && mv < LOW_BATTERY_MV) return;  // a refresh on a flat cell can brown out half-drawn; keep the picture
  uint32_t cur = prefs.getUInt("cur", 0), next = seqs[n - 1];
  for (int i = n - 1; i >= 0; i--)
    if (seqs[i] < cur) { next = seqs[i]; break; }  // step to the next older, wrap to newest
  showPhoto(next);
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
    for (int i = 0; i <= n - MAX_PHOTOS; i++) LittleFS.remove(photoPath(seqs[i]));  // make room: drop the oldest
    f = LittleFS.open("/up.tmp", "w");
    got = 0;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    got += up.currentSize;
    if (f && got <= PHOTO_BYTES) f.write(up.buf, up.currentSize);
  } else {  // END or ABORTED
    if (f) f.close();
    if (up.status == UPLOAD_FILE_END && got == PHOTO_BYTES) {
      uint32_t seq = prefs.getUInt("seq", 0) + 1;
      prefs.putUInt("seq", seq);
      uploaded = LittleFS.rename("/up.tmp", photoPath(seq));
      lastUpload = millis();
    } else {
      LittleFS.remove("/up.tmp");
    }
    got = SIZE_MAX;  // marks "this request is finished" for the reply below
  }
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
    WiFi.begin();  // already paired: plain connect, Bluetooth never starts
  } else {
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM, WIFI_PROV_SECURITY_1, pop, name,
                            nullptr, nullptr, resetWifi);
  }

  bool setupShown = false;
  uint32_t start = millis();
  while (!gotIp) {
    delay(50);
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
    if (millis() - start > (provStarted ? SETUP_MS : WIFI_TIMEOUT_MS)) return;  // give up, keep the current picture
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
    char json[80];
    snprintf(json, sizeof json, "{\"photos\":%d,\"max\":%d,\"battery_mv\":%u}", listPhotos(seqs), MAX_PHOTOS, batteryMv());
    server.send(200, "application/json", json);
  });
  server.on("/upload", HTTP_POST, [] {
    bool ok = uploaded && millis() - lastUpload < 1000;
    server.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "not a frame photo");
  }, handleUpload);
  server.begin();
  Serial.println("serving");

  lastActivity = millis();
  while (millis() - lastActivity < SESSION_MS) {
    server.handleClient();
    delay(2);
    if (uploaded && millis() - lastUpload > 4000) {  // quiet for 4 s: the batch is done, show the newest
      uint32_t seqs[MAX_PHOTOS + 1];
      int n = listPhotos(seqs);
      server.stop();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      radioOff = true;
      if (n) showPhoto(seqs[n - 1]);
      return;
    }
  }
}

void setup() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  setCpuFrequencyMhz(80);  // plenty for this job, and the lowest speed Wi-Fi allows
  panelOff();
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, !NEOPIXEL_POWER_ON);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block on logging when no computer is attached
  Serial.printf("wake cause=%d\n", (int)cause);
  LittleFS.begin(true, FS_ROOT, 5, "photos");
  LittleFS.remove("/up.tmp");
  prefs.begin("frame");

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    rotate();
  } else {
    uint32_t t = millis();  // button still down after 5 s means "forget Wi-Fi"
    while (cause == ESP_SLEEP_WAKEUP_EXT0 && digitalRead(BUTTON) == LOW && millis() - t < HOLD_RESET_MS) delay(20);
#ifdef TEST_FORGET_WIFI  // bench only: behave as if the button had been held
    t = millis() - HOLD_RESET_MS;
#endif
    session(millis() - t >= HOLD_RESET_MS);
  }
  sleepNow();
}

void loop() {}
