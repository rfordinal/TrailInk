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
  that it draws. It times the buffer fill, not the waveform. The refresh shows
  up as the ~2 s gap between the Boot and Home activity lines instead.

## What the first boot settled, and what it did not

Settled:

- **It boots and it draws.** Boot then Home, both on the glass.
- **The SDMMC card path works.** `[SD] SDMMC card mounted` on the first try.
  This was the flagged unknown -- the X4 Pro's 1-bit SDMMC is the first
  non-SPI card this firmware has driven.
- **Screenshots come back over serial.** `CMD:SCREENSHOT` at the X4's default
  geometry: 48,000 bytes = 800x480/8, no truncation, and `screenshot_gate.py`'s
  90d-CCW replay writes a 480x800 BMP that opens like any other device shot.
  Parent repo, `docs/device-shots/2026-09-09-x4pro-home-480x800.png`.
- **RTC found, IMU absent** (`[CLK] SDK RTC found`, `[GYR] SDK IMU not found`),
  matching the profile's `RtcType::Pcf8563` / `ImuType::None`.
- **Heap is a different world from the C3.** `[MEM] Free: 241,212 B, Total:
  308,188 B, MaxAlloc: 196,596 B` on the Home screen. The C3 rules were written
  against ~400 kB of SRAM total.

Not settled by this run:

- **Touch.** Nothing tapped the GT911 or the capacitive Home key.
- **Frontlight.** Never driven.
- **`[MAIN] Hardware detect: X4` is wrong and cosmetic.** `main.cpp` prints
  `_deviceType`, which the non-C3 branch hardcodes to `X4`. It selects nothing
  -- the board profile is compile-time here. Parent `docs/TODO.md` T-278.
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
