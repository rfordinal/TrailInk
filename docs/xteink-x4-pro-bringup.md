# Xteink X4 Pro bring-up

**Nothing here has run on the board.** The env exists and it compiles -- RAM
21.2 % (69,324 B), flash 58.6 % (3,839,003 B), no warning in `src/` or `lib/` --
and that is the whole claim as of 2026-09-09. The device is expected on the desk the same
day. This file says what the build assumes and what the first hour with the
hardware has to settle, so the assumptions are written down before they get
confirmed by accident.

The board itself, its parts and the review material are in the parent repo:
[`../../../docs/devices/xteink-x4-pro.md`](../../../docs/devices/xteink-x4-pro.md).
Branching for this device: [`branching.md`](branching.md).

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

## What the first session has to settle, in order

1. **Which port it is.** The X4 Pro carries USB data on **pogo pins** and ships
   a USB-C-to-pogo dongle; that is a different cable from every other device
   here. `303a` names the vendor and not the board -- the C3 X4 and every S3
   enumerate the same -- so ask the chip before picking an env
   (`../../../CLAUDE.md`, "Identify the port before every flash").
3. **That it boots at all**, before anything is judged: panel init, then a map
   frame, then the SD card.
4. **Touch.** GT911 plus a capacitive Home key is a different input story from
   the T5 S3 Pro's buttons. `TouchPolicy.h` and the touch lock were written
   against that board.

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
