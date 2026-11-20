<img src="assets/icon.svg" width="64" height="64" alt="">

# inkedframe

inkedframe is an open-source battery photo frame, designed to be built and given as a gift. It uses an Adafruit Feather ESP32-S3 (4 MB flash, 2 MB PSRAM, product 5477), a Waveshare 7.3" Spectra 6 (E6) colour e-ink panel (800x480, six inks) and a 2000 mAh LiPo cell. There is no cloud, no account and no app for daily use. You press the frame's button, open `http://frame.local` on any phone or laptop on the same Wi-Fi and pick photos. The browser crops and dithers them, the frame shows the newest and goes back to sleep. It keeps the last 10 photos and steps through them once a day with the radio off. Optionally it can print today's date on the picture, show a different museum painting or print each day, or show a calendar page.

## How it is used

The person receiving the frame does a one-time setup, with no app. The frame creates its own Wi-Fi network, "Photo Frame", and draws a QR code and short instructions on its screen. They point the phone's camera at the code and tap Join. The phone joins the frame's network and opens a setup page by itself (the frame answers every web address, so phones treat it as a sign-in page). On that page they choose their home Wi-Fi from a list, type its password and tap Connect. The frame joins the home network and its screen changes to "Connected", with the address to open for adding photos. Without a camera, they can join the "Photo Frame" network by hand with the password printed under the code (it is different for each frame).

Setup stays open for 10 minutes; if nobody finishes it, the frame keeps its picture and sleeps until the button is pressed. If the home network cannot be joined within 20 seconds (usually a wrong password), the page says so and the frame forgets that network, so it can be tried again.

To add photos, they press the button on the frame, then open `http://frame.local` on a phone or laptop on the same Wi-Fi. The page is mostly one large "Choose photos" button. After choosing one or more photos, they see the first one as the frame will show it (dithered, in a drawn frame), and then press "Send to the frame". The page says "Sending 1 of 3" while it works, then "Sent. The frame is changing, about half a minute." A status strip near the top of the page (and of any open sheet) always says what the frame is doing: awake (with the photo count and battery), receiving, drawing with a countdown and a bar ("about 45 seconds", 60 after switching to Artwork, set as `DRAW_SECS` in `index.html`), then asleep. The countdown keeps going after Done, after closing a sheet and across a reload; during it the page does not contact the frame, and when it ends it checks once whether the frame is awake again. A few seconds after the last upload (or when the page is closed) the frame shows the newest photo and sleeps. After that it changes photo by itself once a day, cycling through the stored ones. When there are already 10 photos, each upload removes the oldest one that is not kept or pinned.

Between choosing and sending, the page offers:

- **Look**: Natural (default), Poster (flatter areas of colour) or Duotone (black, white and one ink: the one closest to each photo's main colour, or red, blue, yellow or green). Each choice shows the chosen photo drawn that way. The phone remembers the choice.
- **Show on a date every year** (collapsed): a month and day. The photo is then shown on that day every year, instead of the usual picture, and is not part of the daily rotation. An optional greeting is printed in red in a white band across the top. The frame keeps its current picture until that day.
- **Add a caption** (collapsed): a message and a "from" line, printed in black in the same band.

The look, date and caption apply to all the photos chosen together. The browser draws the band and its text after dithering, so the letters stay sharp. If the frame has gone to sleep, the page says "Press the button on the frame" with a Try again button; the chosen photos stay on the page, and Try again carries on from the photo that was not sent yet.

The gear button (or "Frame settings") opens the settings (saved on the frame, kept across power loss):

- **Show**: "My photos" (default), "Artwork" (a new picture every day instead of the stored photos) or "Today" (a calendar page: a large day number, the weekday, month and year, and the moon's phase, all in black).
- **Artwork**: "Museum art" (default: one of 223 public-domain paintings and prints from the Metropolitan Museum of Art, with its title and artist in a strip along the bottom) or "Random photos" (a random landscape-format photograph from Lorem Picsum).
- **Date**: Off (default), Corner (a short date such as "Thu 19 Nov" in the bottom right corner), Banner (a white strip along the bottom, the weekday on the left and the full date on the right), Big (a large day number with the month and weekday, bottom left) or Split (the middle 480x480 of the picture on the left, and a white column on the right with the weekday, a very large day number, the month and the year; museum art has its title and artist at the bottom of that column instead of in a strip). The date is black on white so it can be read over any picture. The Today page ignores this setting.
- **Time zone**: Automatic (default: looked up from the frame's internet address each day) or a fixed UTC offset.
- **Unsplash access key** (optional, shown when Artwork is set to Random photos): with a free key from unsplash.com/developers, Random photos asks Unsplash for a landscape photo instead of using Lorem Picsum. The key is hidden as dots; the eye button next to it shows it for checking or editing.

The date and time are always set automatically over the internet; there is nothing to enter. After Save, the frame redraws about 5 seconds later with the new settings and sleeps, and the page says so: "The frame is updating now, about half a minute. Press its button again to change more." At the bottom of the settings, "Advanced" shows the firmware version and has the "Update firmware" button (see [Firmware updates](#firmware-updates)).

The page also shows how many photos are stored and the battery charge in percent. "Photos on the frame" shows the stored photos as small pictures, loaded one at a time. Tapping one offers "Show this now" (shown when the page is closed), "Keep on the frame" (a kept photo is never removed to make room) and "Delete from the frame". "Select" lets several be ticked and removed together with "Delete (n)". Photos pinned to a date show that date instead of Keep.

Closing the page (or leaving it) tells the frame to finish at once, unless photos are still being sent or the photo picker is open. Reloading the page within 2 seconds cancels that. If the frame has already gone to sleep, the page says to press the button.

The button does two things:

- Press it: wake the frame and open the upload page for a few minutes.
- Hold it for about 5 seconds, either to wake the frame or while the upload page is open: forget the stored Wi-Fi and start the Wi-Fi setup again, with the "Photo Frame" network and the QR code (for a new router or a new home). It triggers after 3 seconds, because people told to hold for 5 let go at about 4; the light turns blue the moment the hold has registered.

The light on the board: green while the page can be opened, amber breathing slowly while the frame redraws, blue breathing slowly during Wi-Fi setup. It stays off for the nightly change, when nobody is watching.

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

- Flashing replaces the Feather's partition table with `firmware/partitions.csv`: a 1.69 MB slot for the frame app (the current build is 1,213,536 bytes, 69% of it, with the upload page about 59 KB and the date fonts about 85 KB of that), a 768 KB slot for the updater and a 1.5 MB `photos` partition. Coming from the earlier one-slot table, the first flash erases the stored photos, and the Wi-Fi network and settings may be lost too (on the bench they were): the frame then shows its Wi-Fi setup screen again.
- Every USB upload resets `otadata` to start the frame app. If you flash the updater while the frame app slot holds a broken image, flash the frame app afterwards.
- On this board, two resets in quick succession enter the UF2 bootloader. If the board stops running your firmware after a double tap of reset, that is why.

## How it works

Everything happens in `setup()`, then the chip goes back to deep sleep. What happens depends on why it woke:

| Wake cause | What the frame does |
| --- | --- |
| Timer (every 24 hours), Date off, My photos, nothing pinned | Shows the next older stored photo that is not pinned, wrapping to the newest. Radio stays off. Does nothing with fewer than 2 photos. |
| Timer (00:30 local time), Date on, Artwork, Today or a pinned photo | Joins Wi-Fi, sets the clock by NTP (`pool.ntp.org`) and, for Automatic time zone, looks up the UTC offset at `http://ip-api.com`. Then shows the photo pinned to today if there is one, otherwise fetches the day's artwork, draws the Today page or steps to the next photo, with the date. |
| Timer, battery low | See [Low battery](#low-battery). |
| Button or power-on | Joins the stored Wi-Fi, serves `http://frame.local` until the session has been idle for 3 minutes (5 seconds after a settings change), stores uploads and settings, and shows the newest photo (or the one picked with "Show now") 4 seconds after the last upload or 2 seconds after the page was closed. |
| Button held (3 seconds or more) | Forgets the stored Wi-Fi and runs Wi-Fi setup again. |
| No Wi-Fi stored | Runs Wi-Fi setup: starts its own network "Photo Frame" (WPA2, with a password worked out from the chip's MAC address), answers every DNS name with its own address so the phone opens the setup form, and draws the QR code (network name and password) on the panel. Setup stays open for 10 minutes. |

If Wi-Fi does not connect within 20 seconds the frame gives up and keeps the current picture. During setup, a network that cannot be joined within 20 seconds (a wrong password) is forgotten at once and the form says so, so it can be tried again while setup is open. Once the home network is joined, the form shows "Connected" for a few seconds before the frame's own network closes.

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

### Low battery

The fuel gauge's cell voltage is read at each timer wake. The thresholds are `WARN_BATTERY_MV`, `LOW_BATTERY_MV` and `CHARGED_MV` at the top of `main.cpp`.

- Below 3.55 V, each daily picture gets a small battery sign in the top right corner.
- Below 3.4 V, the frame draws one screen, "Please plug me in with the USB-C cable. I'll be back once I'm charged.", and remembers that it did. A refresh on a flat cell can brown out half-drawn, so after that it neither refreshes nor uses Wi-Fi: it wakes for about half a second, reads the voltage and sleeps again.
- Above 3.9 V (on charge), it goes back to normal at the next wake. A photo shown from the upload page also replaces the screen.

The button and power-on always open the upload page, whatever the voltage.

### Date and artwork

The chip's deep-sleep timer runs from an internal RC oscillator that can drift by a percent or two, tens of minutes a day. So when the date or artwork is on, each wake re-syncs the clock and sleeps until 00:30 local time. If it wakes and the synced clock says it is still the evening before, it sleeps the remainder instead of drawing yesterday's date. If Wi-Fi or NTP fails, it uses its own clock (which keeps running through deep sleep), draws anyway and tries again the next day. Every network step has a timeout, so a dead router costs about 10 seconds awake and a dead internet connection about 15 (Wi-Fi, then up to 10 seconds waiting for NTP). The time zone lookup is cached, so a failed lookup uses the last known offset.

Artwork is fetched as one JPEG a day over HTTPS, decoded with the JPEG decoder in the ESP32-S3's ROM, scaled and cropped to fill the frame, dithered on the frame with the same palette, saturation, contrast and Floyd-Steinberg steps as the upload page, and saved as `/art.bin` (not one of the 10 photos). If anything fails (no network, a bad key, a JPEG it cannot decode) the frame keeps the picture it has. Decoding and dithering take about 7 seconds at 80 MHz and use the PSRAM for the full frame.

Museum art comes straight from `images.metmuseum.org`. The list of 223 works (from `tools/met.json`, with title, artist and the image's path) is built into the firmware as `firmware/src/met.h`, about 16 KB; `node tools/met.mjs` regenerates it. Each day the frame picks one at random, never the same as the day before. The museum's "web-large" images are about 600 pixels wide (598 to 600, 275 to 499 high, all baseline JPEG), so they are enlarged by up to about 1.6 times (nearest pixel) and cropped to fill 800x430. The title and artist are drawn in the 50-pixel strip below when the picture is shown, so the text is not dithered. A long title is shortened with "...". Some scans include the museum's grey calibration strip along an edge.

Lorem Picsum and Unsplash only serve progressive JPEGs, which the ROM decoder cannot read, so Random photos are fetched through the free image proxy [wsrv.nl](https://wsrv.nl), which re-encodes them as baseline JPEGs.

The date styles and the Today page use Inter, rendered by `tools/fonts.py` at the pixel sizes the panel shows (from 22 px text to 210 px tall digits) into Adafruit GFX fonts in `firmware/src/fonts.h`, about 85 KB of flash. Edges are 1-bit (the panel has no greys), but nothing is enlarged. To change a size or weight, edit `FONTS` in that script and run `uv run --with pillow python3 tools/fonts.py`; it downloads the font once into `tools/.cache/`. The digit fonts are anchored at the top of the digits, not the baseline, because GFX glyph offsets are 8-bit. With Split, the frame fetches artwork the full height of the panel, since the label goes beside it. The moon's phase is worked out from the date as days since the new moon of 6 January 2000 in mean synodic months (29.53 days). The real moon runs up to about 14 hours ahead of or behind that mean, so the phase shown is right to within about a day.

### Photo format

Each photo is a raw file of 128,000 bytes (800 x 480 pixels, three to a byte). Each pixel is a palette index in this order: black, white, yellow, red, blue, green. Pixels run left to right, top to bottom, and three consecutive pixels a, b, c make the byte a * 36 + b * 6 + c (6^3 = 216 values). Rows do not start on a byte boundary (800 is not a multiple of 3), but every third row does. The firmware ignores any file that is not exactly 128,000 bytes and draws any byte above 215 as three white pixels.

The photos partition (1,572,864 bytes, 384 blocks of 4 KB) holds 10 photos and the artwork at 32 blocks each, plus the file system's own blocks: 1,449,984 bytes used, measured. An upload is written to a temporary file before it replaces the oldest photo, so the oldest is removed when an upload starts to make room. A rejected upload therefore still removes the oldest photo when 10 are stored.

"Keep" and a pinned date are stored per photo in the settings (key `m<sequence>`: the date as month * 100 + day, plus 0x8000 for Keep) and removed with the photo. The oldest photo that is neither kept nor pinned makes room for an upload. If all 10 are kept or pinned, uploads are refused and the page asks to delete one first. A photo pinned to 29 February shows only in leap years.

### Tuning colours

The browser does the image work. In `firmware/src/index.html`, `PALETTE` holds the RGB values used to match pixels to the six inks, and `SATURATION` (1.4) and `CONTRAST` (1.15) adjust the photo before Floyd-Steinberg dithering. Poster carries 40% of the error to the neighbouring pixels instead of all of it. Change them to suit your panel. The palette order must stay the same as `COLORS` in `main.cpp`.

To check the dither and packing code without a browser or a board:

```sh
node tools/test_dither.mjs
```

It checks that a solid field of each ink dithers to that ink, and that a gradient packs to exactly 128,000 bytes with every byte at most 215. It also checks the looks (Duotone uses only black, white and its ink; Poster changes ink less often than Natural), the choice of Duotone ink, and the text stamping. It prints `dither ok` on success.

## Power

These figures were measured on the bench with a Nordic PPK2 at 3.8 V on the battery pin.

| Measured | Value |
| --- | --- |
| Deep sleep | 22 uA |
| One photo rotation (a ~30 s colour refresh) | about 0.5 mAh |
| Upload session, first 30 s after power-on, page idle | about 115 mA (200 mA for the first 5 s after joining Wi-Fi, then about 90 mA) |
| Daily wake, Date on, My photos (Wi-Fi, NTP, time zone lookup, refresh) | 0.71 to 0.80 mAh, 42 to 52 s awake |
| Daily wake, Museum art (also download, decode, dither) | 1.05 to 1.72 mAh, 46 to 64 s awake |
| Daily wake, Today | 0.53 to 0.61 mAh, 43 to 51 s awake |
| Daily wake with a flat battery (no refresh, no Wi-Fi) | about 0.5 s awake |
| Daily wake, Random artwork (earlier firmware) | 1.26 mAh, 51 s awake |

The daily-wake rows were measured with the bench build (the same `daily()` code, waking every minute); each range is three or more wakes. Most of the spread is the NTP request, which lwIP sends after a random 0 to 5 seconds, and for museum art the size of the image (50 to 450 KB). For comparison, a radio-off rotation measured 0.42 mAh. So the date costs about 0.3 mAh a day (about 120 mAh a year) and Museum art about 0.9 mAh a day (about 340 mAh a year) on top of the plain rotation. Both add one Wi-Fi connection a day, and the frame wakes at the same time each night instead of every 24 hours from the last wake.

The light costs little either way. Measured on the radio-off rotation of two photos (the charge depends on the picture: 0.42 and 0.62 mAh with the old steady amber light, three wakes each), the breathing light, which wakes from light sleep every 250 ms instead of every second to step its brightness, measured 0.43 and 0.63 mAh (+1 to 2%), and with the light off, as on the nightly change, 0.41 and 0.61 mAh.

At about 95 mA, the full 3-minute upload window costs about 5 mAh, and a typical quick upload about 2 mAh. Closing the page ends the session at once instead of after 3 idle minutes.

The session draws 200 mA for its first 5 seconds or so after joining Wi-Fi because the radio does not enter modem sleep yet (the trace has no beacon gaps). Without mDNS it measured the same, so mDNS is not the cause. After that, the floor is about 84 mA with a short burst at every beacon (about every 100 ms): the radio sleeps between beacons, but the CPU never does.

Joining the access point of the last wake directly (cached channel and BSSID) was measured and not kept: joining took 2.3 s either way (the stored network already carries its channel), and the charge per wake was the same. A visit to the updater that times out after 5 minutes measured 9.7 mAh (about 98 mA while serving).

Estimate for one year, using the figures above:

| Use | Charge per year |
| --- | --- |
| Deep sleep | ~190 mAh |
| One rotation per day | ~180 mAh |
| One upload session per week | ~260 mAh |
| Total | ~630 mAh |
| Total with Date on | ~730 mAh |
| Total with Museum art | ~940 mAh |

LiPo self-discharge (not measured, typically a few percent a month) comes on top. A 2000 mAh cell should comfortably pass a year. This is an estimate from bench measurements, not a year-long test.

One non-obvious finding: the panel's control lines must be held low whenever the panel is not being redrawn. Left floating or high, the display board drew 0.85 mA asleep instead of 22 uA. `panelOff()` in `main.cpp` ends SPI, drives CS, DC, RST, clock and data low and latches them through deep sleep. If you use a different panel board, check this first if your sleep current is far above expectations.

## Limits

- Photos can only be added from the same Wi-Fi network as the frame, and only after pressing its button.
- 2.4 GHz Wi-Fi only.
- Firmware updates over Wi-Fi replace only the frame app. The first install, the updater, the boot loader and the partition table need USB. So does recovery if the updater itself is broken.
- `frame.local` needs mDNS: iPhone, Mac, Windows 10 and later, Android 12 and later. The "Connected" screen also shows the frame's IP address as a fallback.
- The upload page needs a browser from 2022 or later (Safari 15.4 on iPhone): it uses the HTML dialog element for its sheets.
- Landscape only. Photos are centre-cropped to 800x480.
- Below 3.4 V the frame shows "Please plug me in" and stops changing the picture until it has been charged above 3.9 V.
- Museum art depends on `images.metmuseum.org` serving the same image paths. Random photos depend on three free outside services: Lorem Picsum (or Unsplash with a key), and wsrv.nl to convert the JPEG. Lorem Picsum photos are random Unsplash photos of any subject, not only landscapes. If a service changes or goes away, the frame keeps showing the last picture.
- Museum images are about 600 pixels wide, so museum art is enlarged and a little soft, and cropped to the frame's shape: up to about a third of a tall picture is cut off.
- A pinned photo needs the clock, so pinning one makes the frame join Wi-Fi once a day.
- Photos are uploaded as finished frame images, so a look or caption cannot be changed afterwards: upload the photo again.
- With the Split date style the left side shows only the middle 480 columns of each photo, so the sides of a greeting or caption band are cut off.
- The HTTPS connections for artwork do not check certificates (a CA bundle would not fit comfortably in flash). The pictures are public, but the optional Unsplash key is sent over such a connection, so someone able to intercept the frame's traffic could read it. Use a free key you do not mind losing. The key is also visible to anyone who opens the upload page.
- The date and time zone lookups need the internet; with no internet the frame uses its own clock, which loses the time after a power loss (no date is drawn until it has synced again).

## Credits

- [GxEPD2](https://github.com/ZinggJM/GxEPD2) by Jean-Marc Zingg, the panel driver.
- [Inter](https://github.com/rsms/inter) by The Inter Project Authors, the typeface for the date and the Today page, under the SIL Open Font License 1.1 (`firmware/src/fonts-OFL.txt`).
- Museum art: public-domain images from [The Metropolitan Museum of Art](https://www.metmuseum.org/about-the-met/policies-and-documents/open-access) (Open Access, CC0).

## License

MIT. See [LICENSE](LICENSE).
