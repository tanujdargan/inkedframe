<img src="assets/icon.svg" width="64" height="64" alt="">

# inkedframe

inkedframe is an open-source battery photo frame, designed to be built and given as a gift. It uses an Adafruit Feather ESP32-S3 (4 MB flash, 2 MB PSRAM, product 5477), a Waveshare 7.3" Spectra 6 (E6) colour e-ink panel (800x480, six inks) and a 2000 mAh LiPo cell. There is no cloud, no account and no app for daily use. You press the frame's button, open `http://frame.local` on any phone or laptop on the same Wi-Fi and pick photos. The browser crops and dithers them, the frame shows the newest and goes back to sleep. It keeps the last 10 photos and steps through them once a day with the radio off.

## How it is used

The person receiving the frame does a one-time setup. The frame draws a QR code and instructions on its own screen. They install Espressif's free "ESP BLE Provisioning" app (iOS and Android), scan the code and pick their Wi-Fi network. Bluetooth is used only for this step.

To add photos, they press the button on the frame, then open `http://frame.local` on a phone or laptop on the same Wi-Fi, and choose one or more photos. Once uploads stop for a few seconds the frame shows the newest photo and sleeps. After that it changes photo by itself once a day, cycling through the stored ones. When there are already 10 photos, a new upload removes the oldest.

The button does two things:

- Press it: wake the frame and open the upload page for a few minutes.
- Hold it for 5 seconds after waking: forget the stored Wi-Fi and restart Bluetooth setup (for a new router or a new home).

## Parts and wiring

| Part | Notes |
| --- | --- |
| Adafruit Feather ESP32-S3, 4 MB flash / 2 MB PSRAM (product 5477) | Controller. Its onboard BOOT button (GPIO0) is the frame's button. |
| Waveshare 7.3" Spectra 6 (E6) e-paper, 800x480 | GxEPD2 driver class `GxEPD2_730c_GDEP073E01`. |
| 2000 mAh LiPo | Plugs into the Feather's battery connector. |

Wiring, as given at the top of `firmware/src/main.cpp`:

| Panel pin | Feather pin |
| --- | --- |
| CS | 10 |
| DC | 9 |
| RST | 6 |
| BUSY | 5 |
| DIN | MO (35) |
| SCLK | SCK (36) |
| VCC | 3V |
| GND | GND |

## Build and flash

Install [PlatformIO](https://platformio.org/), connect the Feather over USB, then:

```sh
cd firmware
pio run -e feather -t upload
```

There is also a `dev` environment for bench testing. It rotates photos every 60 seconds instead of once a day and shortens the upload window to 25 seconds instead of 3 minutes:

```sh
pio run -e dev -t upload
```

Two warnings:

- Flashing replaces the Feather's partition table with `firmware/partitions.csv`: one 1.75 MB app slot, no OTA, and a 2.2 MB `photos` partition for the photo files.
- On this board, two resets in quick succession enter the UF2 bootloader. If the board stops running your firmware after a double tap of reset, that is why.

## How it works

Everything happens in `setup()`, then the chip goes back to deep sleep. What happens depends on why it woke:

| Wake cause | What the frame does |
| --- | --- |
| Timer (every 24 hours) | Shows the next older stored photo, wrapping to the newest. Radio stays off. Does nothing with fewer than 2 photos or when the battery is below 3.4 V. |
| Button or power-on | Joins the stored Wi-Fi, serves `http://frame.local` until the session has been idle for 3 minutes, stores uploads, and shows the newest photo 4 seconds after the last upload. |
| Button held 5 seconds | Forgets the stored Wi-Fi and runs Bluetooth setup again. |
| No Wi-Fi stored | Runs Bluetooth setup and draws the QR code on the panel. Setup stays open for 10 minutes. |

If Wi-Fi does not connect within 20 seconds the frame gives up and keeps the current picture. A wrong password makes it forget the network and offer setup again.

### Photo format

Each photo is a raw file of 192,000 bytes (800 x 480 pixels at 4 bits per pixel). Each nibble is a palette index in this order: black, white, yellow, red, blue, green. The first pixel of a row is in the high nibble. The firmware ignores any file that is not exactly 192,000 bytes and draws any nibble above 5 as white.

### Tuning colours

The browser does the image work. In `firmware/src/index.html`, `PALETTE` holds the RGB values used to match pixels to the six inks, and `SATURATION` (1.4) and `CONTRAST` (1.15) adjust the photo before Floyd-Steinberg dithering. Change them to suit your panel. The palette order must stay the same as `COLORS` in `main.cpp`.

To check the dither and packing code without a browser or a board:

```sh
node tools/test_dither.mjs
```

It checks that a solid field of each ink dithers to that ink, and that a gradient packs to exactly 192,000 bytes with valid indexes. It prints `dither ok` on success.

## Power

These figures were measured on the bench with a Nordic PPK2 at 3.8 V on the battery pin.

| Measured | Value |
| --- | --- |
| Deep sleep | 22 uA |
| One photo rotation (a ~30 s colour refresh) | about 0.5 mAh |
| Upload session, while the page is being served | about 95 mA |

At 95 mA, the full 3-minute upload window costs about 5 mAh, and a typical quick upload about 2 mAh.

Estimate for one year, using the figures above:

| Use | Charge per year |
| --- | --- |
| Deep sleep | ~190 mAh |
| One rotation per day | ~180 mAh |
| One upload session per week | ~260 mAh |
| Total | ~630 mAh |

LiPo self-discharge (not measured, typically a few percent a month) comes on top. A 2000 mAh cell should comfortably pass a year. This is an estimate from bench measurements, not a year-long test.

One non-obvious finding: the panel's control lines must be held low whenever the panel is not being redrawn. Left floating or high, the display board drew 0.85 mA asleep instead of 22 uA. `panelOff()` in `main.cpp` ends SPI, drives CS, DC, RST, clock and data low and latches them through deep sleep. If you use a different panel board, check this first if your sleep current is far above expectations.

## Limits

- Photos can only be added from the same Wi-Fi network as the frame, and only after pressing its button.
- 2.4 GHz Wi-Fi only.
- No over-the-air updates. Firmware updates need USB.
- `frame.local` needs mDNS: iPhone, Mac, Windows 10 and later, Android 12 and later. The setup-complete screen also shows the frame's IP address as a fallback.
- Landscape only. Photos are centre-cropped to 800x480.
- The frame stops rotating when the battery is below 3.4 V, to avoid a half-drawn refresh on a flat cell.

## Credits

- [GxEPD2](https://github.com/ZinggJM/GxEPD2) by Jean-Marc Zingg, the panel driver.
- Espressif's Wi-Fi provisioning (the ESP-IDF provisioning library and the "ESP BLE Provisioning" app).

## License

MIT. See [LICENSE](LICENSE).
