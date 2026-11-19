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

After Save, the frame redraws about a minute later with the new settings. The page also shows the battery charge in percent. A collapsed "Advanced" section at the bottom shows the firmware version and has the "Update firmware" button (see [Firmware updates](#firmware-updates)).

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

Install [PlatformIO](https://platformio.org/) and connect the Feather over USB. The first time, flash two things: the frame app, then the small updater app into its own slot:

```sh
cd firmware
pio run -e feather -t upload
pio run -e updater -t upload
```

Either order works: each upload also writes the boot loader, the partition table and an `otadata` that starts the frame app, so flashing the updater never starts it by mistake. After the first time, `pio run -e feather -t upload` alone updates the frame app over USB, or see [Firmware updates](#firmware-updates) to do it over Wi-Fi.

There is also a `dev` environment for bench testing. It rotates photos every 60 seconds instead of once a day and shortens the upload window to 25 seconds instead of 3 minutes:

```sh
pio run -e dev -t upload
```

Two warnings:

- Flashing replaces the Feather's partition table with `firmware/partitions.csv`: a 1.69 MB slot for the frame app, a 768 KB slot for the updater and a 1.5 MB `photos` partition. Coming from the earlier one-slot table, the first flash erases the stored photos, and the Wi-Fi network and settings may be lost too (on the bench they were): the frame then shows its Bluetooth setup screen again.
- Every USB upload resets `otadata` to start the frame app. If you flash the updater while the frame app slot holds a broken image, flash the frame app afterwards.
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

### Firmware updates

The flash holds two apps: the frame app (`app0`) and a small updater (`app1`, `firmware/updater/updater.cpp`). The updater has only Wi-Fi, mDNS, a one-page form and Arduino's `Update` library.

To update:

1. Build the new frame app with `pio run -e feather`. The file to upload is `firmware/.pio/build/feather/firmware.bin`.
2. Press the frame's button, open `http://frame.local`, open "Advanced" and press "Update firmware". The frame restarts into the updater.
3. About half a minute later, reload `http://frame.local`. It now shows "Frame firmware update". Choose `firmware.bin` and press Upload. The updater writes it into `app0`, checks it and restarts into it. Uploading takes about 20 seconds.
4. Check the version under "Advanced". It is the git commit and build time, for example `40aa33b-261119.0435`.

Or with curl: `curl -X POST http://frame.local/updater`, wait 30 seconds, then `curl -F firmware=@.pio/build/feather/firmware.bin http://frame.local/update`.

The updater's Cancel button goes back to the frame app without changing it. So do 5 minutes without use and a failed Wi-Fi connection, so a frame never sits awake in the updater.

A new frame app is on probation until it has joined Wi-Fi and started its web server. Then it confirms itself. If it cannot join Wi-Fi within 20 seconds, hangs (a 60 second watchdog), crashes or restarts first, the boot loader goes back to the updater, which is still on the same Wi-Fi at `http://frame.local` for another try. A deep-sleep wake skips the boot loader's check, so an unconfirmed app never goes to deep sleep: it rolls back first. A build that has already confirmed itself on this frame is trusted at once when it comes back from the updater without an upload, so a Wi-Fi outage at that moment does not roll back a good app.

What it does not protect against:

- The updater overwrites the frame app as the file arrives. A file that is not firmware at all is refused before anything is written (its first byte is wrong), and Cancel returns to the old app. A firmware file cut short is refused at the end, but by then the old app is gone. The updater then has no app to return to, so on Cancel or timeout it sleeps until the button is pressed (or power is cycled) and offers the upload form again. A rolled-back app is treated the same way, instead of starting it again and again.
- A new app that joins Wi-Fi and starts its web server but is broken in some other way (the panel, the photos, sleep) is kept. Rollback only proves that the next update will be possible.
- The updater itself, the boot loader and the partition table can only be changed over USB.

### Date and artwork

The chip's deep-sleep timer runs from an internal RC oscillator that can drift by a percent or two, tens of minutes a day. So when the date or artwork is on, each wake re-syncs the clock and sleeps until 00:30 local time. If it wakes and the synced clock says it is still the evening before, it sleeps the remainder instead of drawing yesterday's date. If Wi-Fi or NTP fails, it uses its own clock (which keeps running through deep sleep), draws anyway and tries again the next day. Every network step has a timeout, so a dead router costs about 10 seconds awake and a dead internet connection about 15 (Wi-Fi, then up to 10 seconds waiting for NTP). The time zone lookup is cached, so a failed lookup uses the last known offset.

Random artwork is fetched as one JPEG a day over HTTPS, decoded with the JPEG decoder in the ESP32-S3's ROM, cropped to 800x480, dithered on the frame with the same palette, saturation, contrast and Floyd-Steinberg steps as the upload page, and saved as `/art.bin` (not one of the 10 photos). Lorem Picsum and Unsplash only serve progressive JPEGs, which the ROM decoder cannot read, so the image is fetched through the free image proxy [wsrv.nl](https://wsrv.nl), which re-encodes it as a baseline JPEG. If anything fails (no network, a bad key, a JPEG it cannot decode) the frame keeps the picture it has. Decoding and dithering take about 7 seconds at 80 MHz and use the PSRAM for the full frame.

### Photo format

Each photo is a raw file of 128,000 bytes (800 x 480 pixels, three to a byte). Each pixel is a palette index in this order: black, white, yellow, red, blue, green. Pixels run left to right, top to bottom, and three consecutive pixels a, b, c make the byte a * 36 + b * 6 + c (6^3 = 216 values). Rows do not start on a byte boundary (800 is not a multiple of 3), but every third row does. The firmware ignores any file that is not exactly 128,000 bytes and draws any byte above 215 as three white pixels.

The photos partition (1,572,864 bytes, 384 blocks of 4 KB) holds 10 photos and the artwork at 32 blocks each, plus the file system's own blocks: 1,449,984 bytes used, measured. An upload is written to a temporary file before it replaces the oldest photo, so the oldest is removed when an upload starts to make room. A rejected upload therefore still removes the oldest photo when 10 are stored.

### Tuning colours

The browser does the image work. In `firmware/src/index.html`, `PALETTE` holds the RGB values used to match pixels to the six inks, and `SATURATION` (1.4) and `CONTRAST` (1.15) adjust the photo before Floyd-Steinberg dithering. Change them to suit your panel. The palette order must stay the same as `COLORS` in `main.cpp`.

To check the dither and packing code without a browser or a board:

```sh
node tools/test_dither.mjs
```

It checks that a solid field of each ink dithers to that ink, and that a gradient packs to exactly 128,000 bytes with every byte at most 215. It prints `dither ok` on success.

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

At 95 mA, the full 3-minute upload window costs about 5 mAh, and a typical quick upload about 2 mAh. A visit to the updater that times out after 5 minutes measured 9.7 mAh (about 98 mA while serving).

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
- Firmware updates over Wi-Fi replace only the frame app. The first install, the updater, the boot loader and the partition table need USB. So does recovery if the updater itself is broken.
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
