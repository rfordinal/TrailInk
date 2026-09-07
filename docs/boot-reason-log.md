# The boot reason log: why a device that looks frozen may only be parked

**What this file is.** The device can end a boot by going straight back to deep
sleep, before it ever writes the panel. On e-ink that looks exactly like a
freeze. This file documents the record that tells the two apart, why the record
had to live in the boot path, and the board-profile trap that makes the
misreading likely.

**Status, 2026-09-07: written, not verified on hardware.** The mechanism below
is read off the code and cited. Nothing here has been observed on a device yet,
and the event that motivated it is still unexplained -- see "What is still
open".

Confidence is marked per claim: **[repo]** read off this code, **[measured]** on
hardware, **[open]** nobody knows.

## The symptom

A rider finds the device unresponsive: the map still on the glass, no touch, no
USB port on the host, no BLE, and no crash report on the card. Pressing the
power button boots it, and it lands on Home rather than back in the map.

Reported three times up to 2026-09-07, twice on a walk. None of the three could
be explained afterwards, because **nothing the device kept said how the previous
boot ended**.

Every one of those symptoms is also what a healthy deep sleep looks like:

- e-ink holds its last frame with no power at all, so a stale map says nothing
  about whether the CPU is alive
- `startDeepSleep()` tears down the USB CDC (`logSerial.end()`,
  `lib/hal/HalPowerManager.cpp`) so the host's port disappears **[repo]**
- it then calls `powerDownRailsForSleep()`, which cuts the touch rail -- the
  comment there says it outright: *"Trade-off: no touch-to-wake"* **[repo]**

## Why the boot path parks the device

`main.cpp` resolves how the boot started and can decide to sleep again
immediately:

```
const auto wakeupReason = gpio.getWakeupReason();
switch (wakeupReason) {
  case HalGPIO::WakeupReason::PowerButton:
    if (!gpio.verifyPowerButtonWakeup(...)) {
      powerManager.startDeepSleep(gpio);   // nobody was holding the button
    }
  case HalGPIO::WakeupReason::AfterUSBPower:
    powerManager.startDeepSleep(gpio);     // "USB power caused a cold boot"
```

Both are deliberate for a reader: the device should come up when a person holds
the power button, not whenever a charger is plugged in.

**The trap is that the panel is never written on that path.** `setupDisplayAndFonts()`
runs further down in `setup()`, well after this switch, so a boot that parks here
leaves the previous frame on the glass and no sleep screen is ever drawn -- the
`SleepActivity` that would draw one is only reached through `enterDeepSleep()`,
which is a different path entirely. **[repo]**

## The board-profile trap that makes it worse

`isUsbConnected()` (`lib/hal/HalGPIO.cpp`) ends with:

```
if (BoardConfig::ACTIVE.usbDetect < 0) {
  return false;
}
```

On the LilyGo T5 S3 Pro the profile's `usbDetect` is `PIN_UNASSIGNED`
(`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`, the
`LILYGO_T5S3` profile -- the field order is `batteryAdc`,
`batteryChargeStatus`, `batteryDividerMultiplier`, `usbDetect`). So on that
board **`isUsbConnected()` returns false even with a cable in it**. **[repo]**

Two consequences:

- `WakeupReason::AfterUSBPower` is **unreachable** on this board -- its
  condition requires `usbConnected`.
- A plain `ESP_RST_POWERON` is instead resolved as `PowerButton`, and since
  nobody is holding a button, `verifyPowerButtonWakeup()` fails and the device
  parks.

So on this board *any* momentary loss of power reads as a cold power-on and ends
in a silent park with a stale frame. Whether that is what actually happened on
the three reported events is **[open]**.

## What the record holds

One CSV row per boot, `/trailink/boot.csv`, next to `power.csv` so the same
hotel-evening download picks it up. `src/BootLog.h` carries the contract.

| column | what it is |
| --- | --- |
| `reset_reason` | `esp_reset_reason()` as text -- `POWERON`, `DEEPSLEEP`, `PANIC`, `TASK_WDT`, `INT_WDT`, `BROWNOUT`, ... |
| `wake_cause` | `esp_sleep_get_wakeup_cause()` as text -- `UNDEFINED`, `EXT1`, `GPIO`, `TIMER`, ... |
| `resolved` | what the firmware decided the pair means: `PowerButton`, `AfterUSBPower`, `AfterFlash`, `Other` |
| `parked` | 1 when this boot went straight back to deep sleep, 0 when it continued |
| `batt_mv` | battery millivolts, to rule a flat cell in or out |
| `build` | `TRAILINK_VERSION` |

The raw pair and the verdict are separate columns on purpose. The mapping is
where a boot goes wrong, and the pair alone cannot show a misreading -- the pair
next to the verdict can.

### Why not a column in power.csv

`PowerLog::tick()` runs from the main loop. A boot that parks never reaches the
loop, so a row written there would be missing in exactly the case worth
recording. **[repo]**

### What it cannot do

- **No timestamp.** The T5 S3 Pro carries no RTC (its profile is `NO_SENSORS`)
  and nothing has a wall clock this early in boot. Rows are ordered, not dated.
- **It cannot be joined to a `power.csv` run automatically.** A parked boot
  writes a row here and no block there, so the two files drift by a row. Read
  them by eye.
- **No card, no record.** `BootLog::record()` says so in the log when
  `Storage.ready()` is false.

## How this reads against a watchdog reset

A watchdog reset writes **no** `crash_report.txt` and does **not** trip the
crash screen, because `HalSystem::isRebootFromPanic()` tests only `ESP_RST_PANIC`
and `ESP_RST_CPU_LOCKUP` ([`crash-reporting.md`](crash-reporting.md), "Gap 3";
tracked as T-234). It does write a coredump.

So before this file, a watchdog reset was invisible to anyone without a coredump
reader. `reset_reason` now names it directly. That gap is not hypothetical: a
coredump pulled from a T5 S3 Pro on 2026-09-07 held

```
Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
 - loopTask (CPU 1)
```

and had left no trace on the SD card at all. **[measured]** Its
`app_elf_sha256` prefix (`277bbd45d`) matched no image on the laptop, so it came
from a build whose ELF is gone and it could never be symbolised. The raw notes
are archived under the parent repo's `docs/crashes/undated-t5s3-loop-task-wdt/`.

## What is still open

- **The three reported events are unexplained.** Ruled out so far: a panic or
  watchdog reset for the 2026-09-07 event (the coredump in flash predates the
  running build, and overwrite is enabled, so no panic has happened since), and
  plugging USB as the trigger (**[measured]** 2026-09-07: uptime ran unbroken
  across an unplug and replug, so a cable does not power-cycle this board).
  Still possible: an ordinary auto-sleep timeout, or a momentary power loss
  resolved into a silent park.
- **Whether a T5 S3 Pro ever actually reports `ESP_RST_POWERON` in the field.**
  That is the claim the park mechanism rests on and it has never been observed.
  The next occurrence answers it from `boot.csv`.
- **Whether `usbDetect` can be wired up on this board.** The X4 Pro profile
  notes GPIO10 as an unconfirmed candidate for its own USB detect; nothing
  equivalent has been looked for here. Giving the board a real detect would make
  `AfterUSBPower` reachable and stop a power-on being read as a button.
