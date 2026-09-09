# Xteink X4 Pro bring-up

**It runs, and it draws.** First flash and first boot on the board on
2026-09-09: RAM 21.2 % (69,380 B), flash 58.6 % (3,841,619 B), no warning in
`src/` or `lib/`. Home screen on the glass, SD card mounted, screenshot pulled
back over serial. One code change was needed to get there and it is the section
below. The rest of this file is still the assumption list, now with the settled
items marked.

The board itself, its parts and the review material are in the parent repo:
[`../../../docs/devices/xteink-x4-pro.md`](../../../docs/devices/xteink-x4-pro.md).
Branching for this device: [`branching.md`](branching.md).

## First boot: our unit is a UC8279, and the probe never ran on the S3

**Measured on hardware 2026-09-09**, `release/xteink-x4-pro`, one board, USB
serial `b8:1f:3f:d4:89:bc`.

The first flash booted, mounted the SD card and painted nothing. The glass kept
the stock firmware's last frame -- an open Bookshelf -- and the log looked
healthy:

```
[MAIN] Starting TrailInk version 0.2.0-x4pro
[MAIN] Hardware detect: X4
[SD] SDMMC card mounted
[MAIN] Display initialized
[ACT] Entering activity: Boot
[GFX] Time = 7 ms from clearScreen to displayBuffer
```

**Cause: `applyXteinkDisplayController()` was gated behind `FREEINK_MCU_C3`.**
`HalGPIO::begin()` ran the whole Xteink detect block only on the C3, so on this
S3 the profile's default controller stood unchallenged. That default is
SSD1677, and all three drivers are compiled in for this device
(`FREEINK_DRIVER_SSD1677`, `_UC8179`, `_UC8279_X4` -- `BoardConfig.h`, the
driver-enable block), precisely because the batch varies. We wrote SSD1677
commands at a controller that is not one, and it ignored them without
complaint.

Fixed by calling the probe on the X4 Pro path too (`lib/hal/HalGPIO.cpp`, the
`#else` branch of `HalGPIO::begin`). The probe is board-agnostic on purpose --
it bit-bangs whatever pins `ACTIVE` carries, and `XteinkDetect.cpp` says so at
`FREEINK_XTEINK_DISPLAY_PROBE`. It has to run after `holdPowerRails()` has
raised the GPIO1 peripheral rail and before `EpdBus::begin()` gives the pins to
the SPI peripheral; `gpio.begin()` sits between those two in `setup()`.

With the call in, the same build draws, and it names the silicon:

```
[XTDET] NVS hw_calib/screenType=2 (UltraChip)
[XTDET] bus probe VER=00 0F 68 00 00 FLG=13 -> UltraChip
[XTDET] MTP[0x000..0x02F]: A5 A5 1F 20 25 25 3C 00 97 02 02 03 20 02 58 00 ...
[XTDET] promoted SSD1677 -> UC8279 800x480 (LUT_VER=68)
```

Three things worth keeping from that:

- **This unit is a UC8279 800x480, `LUT_VER=0x68`.** Not the SSD1677 the
  profile defaults to and not the UC8179 that "every unit benched so far" had
  carried per the SDK's own comment. So a second X4 Pro may well be a third
  answer -- nothing here may assume a controller, ever.
- **The OEM's own NVS agrees with the probe** (`screenType=2`, UltraChip). It is
  readable because `nvs` lives at 0x9000 and our upload erases only
  0x0-0x4fff, 0x8000-0x8fff, 0xe000-0xffff and the app slot. It is still
  diagnostics and not the decision -- `applyXteinkDisplayController()` says why
  (a full-flash from another unit overwrites it).
- **`[GFX] Time = N ms from clearScreen to displayBuffer` is not refresh time.**
  It read 7 ms and 41 ms while the panel was dead and reads 6 ms and 40 ms now
  that it draws. It stops the clock **before** the panel push:
  `GfxRenderer::displayBuffer()` computes `elapsed` and logs it, then calls
  `display.displayBuffer()` (`lib/GfxRenderer/GfxRenderer.cpp:1567-1570`). So
  the number is the drawing work and the waveform is not in it. **What the
  waveform costs on this panel is open.** This section first pointed at the
  ~2 s gap between the Boot and Home activity lines as "the refresh", which is
  unfounded: those timestamps are second-resolution, and the gap also holds
  Boot's own dwell, the activity exit and Home's enter. A `micros()` bracket
  around `display.displayBuffer()` would settle it.

**And the screenshot channel could not have found this.** `CMD:SCREENSHOT`
dumps the framebuffer, which the renderer had filled correctly the whole time;
only the push to the glass was going to a controller that ignored it. A grab
taken during the dead-panel boot would almost certainly have decoded into a
correct Home screen and reported `ok (48000 bytes)`. **The instrument that saw
the defect was a person looking at the panel.** Keep the two claims apart: a
clean grab says the renderer produced the right frame, and says nothing about
whether the panel developed it.

## What the first boot settled, and what it did not

Settled:

- **It boots and it draws.** Boot then Home, both on the glass.
- **The SDMMC card mounts.** `[SD] SDMMC card mounted` on the first try, and
  `SdmmcBlockDevice` validates a real sector-0 read per attempt, so a block
  read did happen. **No file has been read off it yet** -- the same boot logged
  `missing tile list not read` and two `Fonts directory not found`, which are
  absent paths and not successful reads. The X4 Pro's 1-bit SDMMC is the first
  non-SPI card this firmware has driven, so "mounts" and "serves a map tile"
  are two different claims here.
- **Screenshots come back over serial.** `CMD:SCREENSHOT` at the X4's default
  geometry: 48,000 bytes = 800x480/8, no truncation, and `screenshot_gate.py`'s
  90d-CCW replay writes a 480x800 BMP that opens like any other device shot.
  Parent repo, `docs/device-shots/2026-09-09-x4pro-home-480x800.png`.
- **RTC found, IMU absent** (`[CLK] SDK RTC found`, `[GYR] SDK IMU not found`),
  matching the profile's `RtcType::Pcf8563` / `ImuType::None`.
- **Heap is a different world from the C3.** `[MEM] Free: 241,212 B, Total:
  308,188 B, MaxAlloc: 196,596 B` on the Home screen, one sample, with no BLE
  link and no map open. The C3 rules were written against ~400 kB of SRAM
  total.
- **The C3 build is unaffected, verified rather than argued.** The probe call
  sits in the `#else` of `#if FREEINK_MCU_C3` **and** behind
  `#if FREEINK_DEVICE_X4PRO`, so a C3 build never preprocesses it.
  `pio run -e default` at `develop` f2ddbaca, full core rebuild: RAM 18.0 %
  (59,012 B), flash 61.6 % (4,033,773 B), zero warnings in `src/` or `lib/`.
  Not checked, and not claimed: whether the C3 binary is byte-identical to the
  pre-merge one.

Not settled by this run:

- **Touch.** Nothing tapped the GT911 or the capacitive Home key.
- **Frontlight.** Never driven.
- **`[MAIN] Hardware detect: X4` is wrong, and it is not only a log label.**
  `HalGPIO::begin()`'s non-C3 branch hardcodes `_deviceType = DeviceType::X4`
  (`lib/hal/HalGPIO.cpp:136`), and `deviceIsX3()` is read in ten places outside
  that log line: the differential-refresh choice (`src/main.cpp:485`), theme
  geometry (`src/components/themes/HintGeometry.h:25,30`,
  `BaseTheme.cpp:284,305,412`, `lyra/LyraTheme.cpp:425,448`) and the interval
  stepper's direction (`src/activities/util/IntervalSelectionActivity.cpp:123,124`).
  So every S3 board silently takes the X4 branch in all of them. On the X4 Pro
  that is **plausibly right and unchecked**: the panel is 800x480, the same
  geometry the X4 branch was written for. On an S3 board that is not 800x480 it
  is a real defect. This section first said "it selects nothing", which was
  read off `main.cpp:366` alone without grepping the accessor. Parent
  `docs/TODO.md` T-278.
- **Buttons.** No press was tried.
- Everything on the SDK's own Pending list below.

## Why it is its own branch and its own binary

The X4 Pro is an **ESP32-S3**, the plain X4 and the X3 are ESP32-C3. A build
selects one MCU family (`BoardConfig.h`: *"all selected devices must share one
MCU family"*), so this cannot be a flag on `[env:default]` the way `X3` sits
next to `X4` there. Second binary, second flash, second hardware pass, for
every release.

It is the reference device because the frontlight is mandatory for the product
and the plain X4 has none.

## The build environment

`[env:x4pro]` in `platformio.ini`, on `release/xteink-x4-pro`. Modelled on
`[env:sticky]`, the other S3 env, plus `FREEINK_CAP_BLE_PERIPHERAL` and the two
bench commands that `[env:default]` carries.

`-DFREEINK_DEVICE_X4PRO=1` selects `BoardConfig::XTEINK_X4_PRO`
(`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1495`), which
brings the SSD1677 panel at 800x480, GT911 capacitive touch plus a capacitive
Home key, the warm/cold frontlight and digital active-low buttons -- the profile
notes those are *"confirmed on hardware: plain active-low GPIO buttons, not the
OEM ADC ladder"*, meaning confirmed by whoever wrote the SDK profile, not by us.

## The SD card is the part that is different, and it did not compile

The X4 Pro's SD is **1-bit SDMMC**, not SPI. `BoardConfig.h` auto-enables
`FREEINK_SD_SDMMC` for this device, and `SDCardManager` then mounts SdFat's
`FsVolume` on an esp-idf block device instead of driving the card itself. That
needs SdFat's generic block-device interface in the *consumer's* build, which
`BoardConfig.h` states at the `FREEINK_SD_SDMMC` definition. Without it:

```
freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:55:19: error:
cannot convert 'freeink::SdmmcBlockDevice*' to 'FsBlockDevice*' {aka 'SdSpiCard*'}
```

Fixed by `-DUSE_BLOCK_DEVICE_INTERFACE=1` in the env. **The SDK's own sample env
for this board omits that flag** (`freeink-sdk/platformio.sample.ini`,
`[env:x4pro]`) while the X4 Classic env two blocks down has it, so the sample
would hit the same error. Worth a PR to the fork
([`freeink-sdk-fork.md`](freeink-sdk-fork.md)).

This is also the first sign that **the X4 Pro's SD path is not the one the rest
of the firmware has been exercised against.** Everything measured so far --
BUG-037, the `readFileToStream` watchdog fix -- was on SPI cards.

## What the first session had to settle, in order

The first two are done -- see "What the first boot settled" above. Kept here
because the port note is the one a later session still needs.

1. ~~**Which port it is.**~~ Done: `/dev/ttyACM2`, USB serial
   `b8:1f:3f:d4:89:bc`, ESP32-S3 (QFN56) rev v0.2, embedded PSRAM 8 MB
   (AP_3v3). The X4 Pro carries USB data on **pogo pins** and ships a
   USB-C-to-pogo dongle; that is a different cable from every other device
   here. `303a` names the vendor and not the board -- the C3 X4 and every S3
   enumerate the same -- so ask the chip before picking an env
   (`../../../CLAUDE.md`, "Identify the port before every flash").
2. ~~**That it boots at all.**~~ Done, once the controller probe ran. A map
   frame is still untried; Boot and Home are what has been on the glass.
3. **Touch.** Still open. GT911 plus a capacitive Home key is a different input
   story from the T5 S3 Pro's buttons. `TouchPolicy.h` and the touch lock were
   written against that board.

## The T5 S3 Pro tree, merged in to see what survives

Branch `t5s3-to-x4pro`, forked from `release/xteink-x4-pro` on 2026-09-09 and
merged with `release/lilygo-t5-s3-pro` whole. 93 commits, 25 non-docs files.
Parent `docs/TODO.md` T-297 is the plan; its counts (212 commits, 53 files) are
from before the T5 branch synced `develop`, which moved the merge base and
shrank the incoming set.

Why here and not `develop`: the X4 Pro is the **other** S3 board. The T5 S3 Pro
has physical keys and one warm frontlight channel; this board has a GT911
digitizer, a capacitive Home key and warm plus cool. So this is the board that
answers whether that work is board-agnostic or T5-shaped, and it answers it
before any of it reaches a production branch.

One conflict, `src/main.cpp`, both hunks additive, resolved as a union. The SDK
pointer stayed at `955b2530`; the T5 branch pinned `e514a868`, which is an
ancestor of it, so ours is the newer of the two and there is no downgrade to
audit.

### Three things switch themselves on for this board

Read off the code, not run on hardware:

- `HalGPIO::wasHomeKeyTapped()` and `wasHomeKeyLongPressed()`
  (`lib/hal/HalGPIO.cpp:248`) forward straight to the SDK's `InputManager`, so
  they answer on any profile with a capacitive home key. This board's profile
  has one.
- `MappedInputManager::wasHomeKeyConfirm()` reports a home-key tap as
  **Confirm**, with no board condition
  (`src/MappedInputManager.cpp`, `wasPressed`/`wasReleased`).
- `loop()` toggles the frontlight on a home-key **hold**, gated only by
  `FrontlightManager::present()` (`src/main.cpp:899`).

**Confirm is a T5 decision inherited by accident.** There, the user button was
the only readable button on the board and had to carry two jobs. Here there are
two physical keys (`Left` on GPIO0, `Right` on GPIO7) plus the Home key, so what
Home should mean is an open question and not something to take from the other
board. It is not the same question as whether the hold should light the panel --
that one is the whole reason this is the reference device.

What did **not** come across: the T5's `userButtonHook()` is behind `#if
FREEINK_DEVICE_LILYGO` and does not compile here, and the LEDC frequency
override is guarded at runtime on `BoardConfig::Board::LilyGoT5S3`
(`src/main.cpp:632`). This board therefore runs the SDK's default frontlight PWM
frequency, which is on the SDK's own Pending list -- see "What is open" below.

GNSS is not in this binary at all. `lib/Gnss`, `GnssLog` and the map's GNSS
reader are all behind `ENABLE_GNSS_CMD`, which `platformio.ini` sets in
`[env:t5s3pro]` and in no other env (`src/GnssAccess.h` explains why that flag's
name is narrower than its meaning). `MapGnssHeading` is host-tested either way.

### What the hardware pass has to check

Built and host-tested only: `pio run -e x4pro` and `-e t5s3pro` both clean, no
warnings from `src/` or `lib/`, 457/457 host tests. That says nothing about a
finger on glass.

1. **Does it still boot.** The merge touched `main.cpp`'s `setup()` ordering.
2. **Home key tap.** Does it select? Should it, given `Left` and `Right` exist?
3. **Home key hold.** Does the frontlight come on under the thumb, and does the
   level survive a reboot (`SETTINGS.frontlightOn` / `frontlightBrightness`)?
4. **The boot flash.** `main.cpp:687-689` calls `setBrightness()` and only then
   `off()`, so a board whose light was left off may still light up for one
   frame at boot. **That order is deliberate** -- the comment above it says
   `setBrightness()` seeds the manager's "last brightness", and `off()` alone
   would make a later toggle restore the SDK's 50 % default instead of the
   saved level. So if the flash is visible on this board, the fix is a
   set-without-actuating path in `FrontlightManager`, not swapping these two
   lines. Whether it is visible at all is a two-channel question and unmeasured.
5. **Two channels, one `on()`.** `toggleFrontlight()` calls
   `FrontlightManager::on()` and reads `brightness()`. What that does to warm
   plus cool here is unknown -- the T5 has one channel.
6. **Touch, first time on this board.** `TouchPolicy.h` and the touch lock were
   written against the T5's buttons ([`touch-modes.md`](touch-modes.md), "The X4
   Pro trap"): `TOUCH_DISABLED` here leaves no Back and no Confirm, and the
   setting does not block that.
7. **A map frame.** Still untried on this board, merge or no merge.
8. **Heap.** RAM 21.2 % at link for `env:x4pro`; the runtime free-heap number
   after a map frame has no baseline on this board yet.

## What is open

- The SDK's own doc keeps a Pending list for this board -- frontlight GPIO and
  frequency, SD CS, battery and VBUS pins, panel and touch orientation, the
  ADC-ladder pins. Read `freeink-sdk/docs/xteink-x4pro-support.md` before
  treating any of those as known.
- Whether the OEM bootloader locks anything the way the AliExpress units did
  (parent `docs/devices/xteink-lineup.md`).
- Frontlight control: the SDK has it, nothing here has driven it. The T5 S3 Pro
  has `CMD:LIGHT` behind `ENABLE_FRONTLIGHT_CMD` on its own branch; whether that
  command should exist here is a decision, not a copy.
- Battery drain with the frontlight on. Early units had it badly enough to be
  reported in reviews.
