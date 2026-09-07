#pragma once

// One CSV row per boot, written in the boot path itself.
//
// Why not a column in PowerLog: `PowerLog::tick()` runs from the main loop, and
// the boot path can decide to go straight back to deep sleep before the loop
// ever starts (`main.cpp`, the `getWakeupReason()` switch). That park is exactly
// the event worth recording, so a row written from the loop would be missing in
// the one case it is needed. This writes before the decision is acted on.
//
// Why a file and not just a log line: the device is carried away from the
// laptop. A freeze noticed on a walk has no serial capture attached to it, and
// by the time the device is back on USB the only thing left is what it wrote
// down. Three such events happened before this file existed (2026-09-07) and
// none of them could be told apart afterwards -- deep sleep, a silent park and
// a power loss all leave the same stale frame on an e-ink panel.
//
// Cost: one open/append/close of about 120 bytes per boot. Nothing rotates it;
// at a handful of boots a day that is a few kB a year.
class BootLog {
 public:
  // Records how this boot started and whether the boot path is about to park
  // the device again. Call `parked = true` immediately before a
  // `startDeepSleep()` that happens during boot, and `parked = false` once the
  // boot is known to continue into the UI.
  //
  // `resolved` is what the firmware decided the raw pair means -- pass
  // `HalGPIO::name(wakeupReason)`. It is a separate column because the mapping
  // is where a boot goes wrong: on a board whose profile leaves `usbDetect`
  // unassigned, `isUsbConnected()` is hardwired false, so a plain POWERON is
  // resolved as PowerButton and a released button then parks the device. The
  // raw pair alone cannot show that; the pair next to the verdict can.
  //
  // Reads `esp_reset_reason()` and `esp_sleep_get_wakeup_cause()` itself rather
  // than taking them as arguments: both are latched at startup and stay
  // readable for the life of the boot, so a caller cannot pass a stale pair.
  static void record(const char* resolved, bool parked);

  // Next to power.csv, under the same on-device root as the tiles. The path
  // still says trailink because the card's directory has not been renamed
  // (parent CLAUDE.md, "Naming").
  static constexpr const char* kPath = "/trailink/boot.csv";
  static constexpr const char* kDir = "/trailink";
};
