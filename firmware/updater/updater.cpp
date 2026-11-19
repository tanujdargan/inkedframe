// inkedframe updater: the small app in the second slot (app1). The frame app switches here when
// "Update firmware" is pressed. It joins the stored Wi-Fi, serves an upload form at http://frame.local,
// writes the uploaded frame app into app0 and restarts into it. Cancel, 5 minutes idle or no Wi-Fi go
// back to the frame app. A new frame app that fails to prove itself rolls back to here (see main.cpp).
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_ota_ops.h>

const uint32_t IDLE_MS = 300000, WIFI_TIMEOUT_MS = 30000;

WebServer server(80);
uint32_t lastActivity;
bool updated, cancelled;

const char page[] = R"(<!doctype html><meta name=viewport content="width=device-width,initial-scale=1">
<title>Frame update</title><body style="font:18px system-ui,sans-serif;max-width:520px;margin:auto;padding:24px 16px">
<h1>Frame firmware update</h1><p>Choose the firmware file (firmware.bin) and press Upload. The frame restarts with it.</p>
<form method=post action=/update enctype=multipart/form-data><input type=file name=firmware accept=.bin required>
<p><button>Upload</button></form><form method=post action=/cancel><button>Cancel</button></form>
<p>Leaves by itself after 5 minutes without use.)";

// Back to the frame app, unless it is missing, broken or was rolled back: then going there would only
// bring us back here, so sleep until the button asks for another try.
[[noreturn]] void backToFrame() {
  const esp_partition_t *app = esp_ota_get_next_update_partition(nullptr);
  esp_ota_img_states_t state;
  bool bad = !esp_ota_get_state_partition(app, &state) && (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED);
  if (!bad && !esp_ota_set_boot_partition(app)) {
    Serial.println("updater: back to the frame app");
    Serial.flush();
    esp_restart();
  }
  Serial.println("updater: no working frame app, sleeping until the button");
  Serial.flush();
  WiFi.mode(WIFI_OFF);
  while (digitalRead(GPIO_NUM_0) == LOW) delay(20);
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_deep_sleep_start();
}

void setup() {
  setCpuFrequencyMhz(80);
  for (uint8_t pin : {10, 9, 6, 36, 35}) {  // panel CS, DC, RST, clock, data low, as panelOff() in main.cpp
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    gpio_hold_en((gpio_num_t)pin);
  }
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, !NEOPIXEL_POWER_ON);
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, LOW);
  pinMode(GPIO_NUM_0, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(esp_ota_get_next_update_partition(nullptr), &state);
  Serial.printf("updater: running %s, frame app state %d\n", esp_ota_get_running_partition()->label, (int)state);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("frame");
  WiFi.begin();  // the frame app's stored network
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() > WIFI_TIMEOUT_MS) backToFrame();
    delay(50);
  }
  Serial.printf("updater: ip %s\n", WiFi.localIP().toString().c_str());
  MDNS.begin("frame");
  MDNS.addService("http", "tcp", 80);
  server.on("/", HTTP_GET, [] {
    lastActivity = millis();
    server.send(200, "text/html", page);
  });
  server.on("/cancel", HTTP_POST, [] {
    server.send(200, "text/plain", "Going back to the frame.");
    cancelled = true;
  });
  server.on("/update", HTTP_POST, [] {
    server.send(updated ? 200 : 400, "text/plain",
                updated ? "Updated. The frame restarts with the new firmware." : String("Update failed: ") + Update.errorString());
  }, [] {
    HTTPUpload &up = server.upload();
    lastActivity = millis();
    if (up.status == UPLOAD_FILE_START) {
      updated = false;
      Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);  // into app0
    } else if (up.status == UPLOAD_FILE_WRITE) {
      Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      updated = Update.end(true);  // checks the image, then makes it the boot app
      Serial.printf("updater: %u bytes, %s\n", (unsigned)up.totalSize, updated ? "ok" : Update.errorString());
    } else {
      Update.abort();
    }
  });
  server.begin();
  lastActivity = millis();
}

void loop() {
  server.handleClient();
  delay(2);
  if (updated) {
    delay(500);  // let the reply go out
    esp_restart();
  }
  if (cancelled || millis() - lastActivity > IDLE_MS) {
    delay(500);
    backToFrame();
  }
}
