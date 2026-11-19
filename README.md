<img src="assets/icon.svg" width="64" height="64" alt="">

# inkedframe

inkedframe is an open-source battery photo frame, designed to be built and given as a gift. It uses an Adafruit Feather ESP32-S3 (4 MB flash, 2 MB PSRAM, product 5477), a Waveshare 7.3" Spectra 6 (E6) colour e-ink panel (800x480, six inks) and a 2000 mAh LiPo cell. There is no cloud, no account and no app for daily use. You press the frame's button, open `http://frame.local` on any phone or laptop on the same Wi-Fi and pick photos. The browser crops and dithers them, the frame shows the newest and goes back to sleep. It keeps the last 10 photos and steps through them once a day with the radio off. Optionally it can print today's date on the picture, or show a different random photograph from the internet each day.

## How it is used

The person receiving the frame does a one-time setup. The frame draws a QR code and instructions on its own screen. They install Espressif's free "ESP BLE Provisioning" app (iOS and Android), scan the code and pick their Wi-Fi network. Bluetooth is used only for this step.

To add photos, they press the button on the frame, then open `http://frame.local` on a phone or laptop on the same Wi-Fi, and choose one or more photos. Once uploads stop for a few seconds the frame shows the newest photo and sleeps. After that it changes photo by itself once a day, cycling through the stored ones. When there are already 10 photos, a new upload removes the oldest.

The upload page also has a small settings form (saved on the frame, kept across power loss):

- **Show**: "My photos" (default) or "Random artwork", a new random landscape-format photograph every day instead of the stored photos.
- **Date**: Off (default), Corner (a short date such as "Thu 19 Nov" in the bottom right corner), Banner (a white strip along the bottom with the weekday and full date) or Big (a large day number with the month and weekday, bottom left). The date is black on a white box so it can be read over any picture.
- **Time zone**: Automatic (default: looked up from the frame's internet address each day) or a fixed UTC offset.
- **Unsplash access key** (optional): with a free key from unsplash.com/developers, Random artwork asks Unsplash for a landscape photo instead of using Lorem Picsum.

After Save, the frame redraws about a minute later with the new settings. The page also shows the battery charge in percent.

The button does two things:

- Press it: wake the frame and open the upload page for a few minutes.
- Hold it for 5 seconds, either to wake the frame or while the upload page is open: forget the stored Wi-Fi and restart Bluetooth setup (for a new router or a new home). When held at wake, the 5 seconds count from the moment the frame wakes.

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
| Timer (every 24 hours), Date off and My photos | Shows the next older stored photo, wrapping to the newest. Radio stays off. Does nothing with fewer than 2 photos or when the battery is below 3.4 V. |
| Timer (00:30 local time), Date on or Random artwork | Joins Wi-Fi, sets the clock by NTP (`pool.ntp.org`) and, for Automatic time zone, looks up the UTC offset at `http://ip-api.com`. Then fetches the day's artwork or steps to the next photo, and draws it with the date. Does nothing when the battery is below 3.4 V. |
| Button or power-on | Joins the stored Wi-Fi, serves `http://frame.local` until the session has been idle for 3 minutes (30 seconds after a settings change), stores uploads and settings, and shows the newest photo 4 seconds after the last upload. |
| Button held 5 seconds | Forgets the stored Wi-Fi and runs Bluetooth setup again. |
| No Wi-Fi stored | Runs Bluetooth setup and draws the QR code on the panel. Setup stays open for 10 minutes. |

If Wi-Fi does not connect within 20 seconds the frame gives up and keeps the current picture. A wrong password makes it forget the network and offer setup again.

### Date and artwork

The chip's deep-sleep timer runs from an internal RC oscillator that can drift by a percent or two, tens of minutes a day. So when the date or artwork is on, each wake re-syncs the clock and sleeps until 00:30 local time. If it wakes and the synced clock says it is still the evening before, it sleeps the remainder instead of drawing yesterday's date. If Wi-Fi or NTP fails, it uses its own clock (which keeps running through deep sleep), draws anyway and tries again the next day. Every network step has a timeout, so a dead router costs about 10 seconds awake and a dead internet connection about 15 (Wi-Fi, then up to 10 seconds waiting for NTP). The time zone lookup is cached, so a failed lookup uses the last known offset.

Random artwork is fetched as one JPEG a day over HTTPS, decoded with the JPEG decoder in the ESP32-S3's ROM, cropped to 800x480, dithered on the frame with the same palette, saturation, contrast and Floyd-Steinberg steps as the upload page, and saved as `/art.bin` (not one of the 10 photos). Lorem Picsum and Unsplash only serve progressive JPEGs, which the ROM decoder cannot read, so the image is fetched through the free image proxy [wsrv.nl](https://wsrv.nl), which re-encodes it as a baseline JPEG. If anything fails (no network, a bad key, a JPEG it cannot decode) the frame keeps the picture it has. Decoding and dithering take about 7 seconds at 80 MHz and use the PSRAM for the full frame.

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
| Daily wake, Date on, My photos (Wi-Fi, NTP, time zone lookup, refresh) | 0.66 mAh, 38 s awake |
| Daily wake, Random artwork (also download, decode, dither) | 1.26 mAh, 51 s awake |

For comparison, a radio-off rotation measured 0.42 mAh in the same session as the two rows above. So the date costs about 0.24 mAh a day (about 90 mAh a year) and Random artwork about 0.8 mAh a day (about 300 mAh a year) on top of the plain rotation. Both add one Wi-Fi connection a day, and the frame wakes at the same time each night instead of every 24 hours from the last wake.

At 95 mA, the full 3-minute upload window costs about 5 mAh, and a typical quick upload about 2 mAh.

Estimate for one year, using the figures above:

| Use | Charge per year |
| --- | --- |
| Deep sleep | ~190 mAh |
| One rotation per day | ~180 mAh |
| One upload session per week | ~260 mAh |
| Total | ~630 mAh |
| Total with Date on | ~720 mAh |
| Total with Random artwork | ~930 mAh |

LiPo self-discharge (not measured, typically a few percent a month) comes on top. A 2000 mAh cell should comfortably pass a year. This is an estimate from bench measurements, not a year-long test.

One non-obvious finding: the panel's control lines must be held low whenever the panel is not being redrawn. Left floating or high, the display board drew 0.85 mA asleep instead of 22 uA. `panelOff()` in `main.cpp` ends SPI, drives CS, DC, RST, clock and data low and latches them through deep sleep. If you use a different panel board, check this first if your sleep current is far above expectations.

## Limits

- Photos can only be added from the same Wi-Fi network as the frame, and only after pressing its button.
- 2.4 GHz Wi-Fi only.
- No over-the-air updates. Firmware updates need USB.
- `frame.local` needs mDNS: iPhone, Mac, Windows 10 and later, Android 12 and later. The setup-complete screen also shows the frame's IP address as a fallback.
- Landscape only. Photos are centre-cropped to 800x480.
- The frame stops rotating when the battery is below 3.4 V, to avoid a half-drawn refresh on a flat cell.
- Random artwork depends on three free outside services: Lorem Picsum (or Unsplash with a key), and wsrv.nl to convert the JPEG. Lorem Picsum photos are random Unsplash photos of any subject, not only landscapes. If a service changes or goes away, the frame keeps showing the last picture.
- The HTTPS connections for artwork do not check certificates (a CA bundle would not fit comfortably in flash). The pictures are public, but the optional Unsplash key is sent over such a connection, so someone able to intercept the frame's traffic could read it. Use a free key you do not mind losing. The key is also visible to anyone who opens the upload page.
- The date and time zone lookups need the internet; with no internet the frame uses its own clock, which loses the time after a power loss (no date is drawn until it has synced again).

## Credits

- [GxEPD2](https://github.com/ZinggJM/GxEPD2) by Jean-Marc Zingg, the panel driver.
- Espressif's Wi-Fi provisioning (the ESP-IDF provisioning library and the "ESP BLE Provisioning" app).

## License

MIT. See [LICENSE](LICENSE).
