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

The person carrying it finds the device unresponsive: the map still on the glass, no touch, no
USB port on the host, no BLE, and no crash report on the card. Pressing the
power button boots it, and it lands on Home rather than back in the map.

Reported four times up to 2026-09-08 -- twice on a walk, once on a flight. The
first three could not be explained afterwards, because **nothing the device kept
said how the previous boot ended**. **The fourth was explained**, from
`power.csv` plus the settings file rather than from anything this record wrote:
it was an ordinary auto-sleep, and the section below has it.

**The first of them is written up: `docs/PROGRESS.md`, 2026-09-05, "A device
dead after a walk"** (parent repo). A T5 S3 Pro came back from a 4 h 36 min walk
with no USB enumeration at all and the panel still holding a header. Battery was
75 % / 3,922 mV and the heap was flat, so neither a flat cell nor a leak explains
it. **That pass got further than this one on the cause and stopped one step short
of the mechanism:** with both watchdogs set to panic it ruled out a hang and left
brownout or the rail going away. What it did not explain is why the device then
stays dead rather than rebooting -- which is what the park below does, and why
`resolved` is a column.

Every one of those symptoms is also what a healthy deep sleep looks like:

- e-ink holds its last frame with no power at all, so a stale map says nothing
  about whether the CPU is alive
- `startDeepSleep()` tears down the USB CDC (`logSerial.end()`,
  `lib/hal/HalPowerManager.cpp`) so the host's port disappears **[repo]**
- it then calls `powerDownRailsForSleep()`, which cuts the touch rail -- the
  comment there says it outright: *"Trade-off: no touch-to-wake"* **[repo]**

## A GNSS session has nothing holding it awake

**This is what the fourth occurrence turned out to be, measured 2026-09-08, and
it is a defect rather than a mystery.** The device was asleep, not dead.

`MapActivity::preventAutoSleep()` returns
`freeink::BlePositionServer::getInstance().isRunning()` and nothing else. So the
only thing that keeps the map screen awake is **a phone**. On the device's own
GNSS nothing does, because `lastActivityTime` is reset only by a button, by
touch while touch is enabled, and by the tilt sensor (`src/main.cpp`, the
`userInput` line) -- **an arriving GNSS fix is not input**. After
`sleepTimeoutMinutes` with no hands on it, a device that is tracking perfectly
deep-sleeps and cuts its own track. **[repo]**, and **[measured]** below.

### The evidence

`power.csv` off the device's own web server ([`../../docs/device-log-forensics.md`](../../docs/device-log-forensics.md)).
The run ended at uptime **10,233 s** (2 h 50 min):

| at the last row | |
| --- | --- |
| `gnss_run` / `gnss_tracked` | 1 / 8 -- receiver running, eight satellites |
| `ble` | 0 -- BLE stack down, so `preventAutoSleep()` was false |
| `batt_mv` / `batt_pct` | 3,813 / 58 %, slope flat (3,816 to 3,813 over six minutes) |
| `heap` / `min_heap` | 174,076 B, unmoved through the whole tail |
| `ref_window` | 828 and climbing -- the map was still redrawing |
| rows | 171, continuous, no gap over 75 s |

Nothing crashed: the coredump partition read afterwards was **byte-identical**
to a read four days earlier, so no panic and no watchdog fired. And the next run
began on battery with no charge in between, so a button press woke it.

Settings that complete the picture: `sleepTimeoutMinutes=10`, `sleepScreen=6`
(`QUICK_RESUME`), `lastSleepActivity=1` (`SLEEP_ACTIVITY_MAP`).

### Why it reads as a dead device

Two things hide the sleep, and both are working as designed:

- **`QUICK_RESUME` keeps the frame.** `SleepActivity::renderLastScreenSleepScreen()`
  leaves the map on the glass and adds only a moon icon bottom-left. Nothing
  announces the sleep.
- **The frontlight cannot wake it.** The light is a hold on the user button or on
  the GT911 home key, and **neither is a deep-sleep wake source** -- only
  `BOOT`/ext1 is armed (`freeink::PowerManager::deepSleepUntilPowerButton()`).
  So reaching for the light is guaranteed to fail on a sleeping device. That is
  how this occurrence was found, and it is why "no button responded" was
  reported.

So the honest description is not "the device froze". It is: **it slept, said
nothing, and the button a person reaches for first is the one that cannot wake
it.**

Tracked as **T-283**, which also carries the warning against the obvious fix:
simply OR-ing `Gnss::isRunning()` in makes a receiver left switched on hold the
device awake until the cell is flat.

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
- **No previous-boot uptime.** T-262 asks for it and this does not record it, so
  a row cannot say how long the device had been up before it died. That needs a
  value persisted across the reset (RTC memory survives deep sleep and a
  watchdog reset, not a power loss) and is still open.

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

- **The fourth event is explained** (auto-sleep, above). **The first three are
  not, and are not proven to be the same thing.** Auto-sleep is consistent with
  all of them -- it explains the stale frame, the dead touch and the absent USB
  port equally well -- but one detail does not fit yet: the 2026-09-07 wake
  booted `Boot` then `Home`, while a `QUICK_RESUME` sleep clears
  `showBootScreen` and should have resumed into the map. Either an extra boot
  intervened, or that one had a different cause. **What would settle it:** a
  `boot.csv` row per occurrence, which is the whole point of this file.
  Ruled out for 2026-09-07: a panic or
  watchdog reset (the coredump in flash predates the
  running build, and overwrite is enabled, so no panic has happened since), and
  plugging USB as the trigger (**[measured]** 2026-09-07: uptime ran unbroken
  across an unplug and replug, so a cable does not power-cycle this board).
  Conditions on that measurement: the board was **awake with the map open** and
  ran on battery across the unplug; uptime was continuous through both events
  (674 s and climbing). **It says nothing about plugging into a board that is
  already parked or unpowered**, which is the case the 2026-09-05 "rail going
  away" hypothesis actually needs. One run, one board.
  Still possible: an ordinary auto-sleep timeout, or a momentary power loss
  resolved into a silent park.
- **Whether a T5 S3 Pro ever actually reports `ESP_RST_POWERON` in the field.**
  That is the claim the park mechanism rests on and it has never been observed.
  The next occurrence answers it from `boot.csv`.
- **Whether `usbDetect` can be wired up on this board.** The X4 Pro profile
  notes GPIO10 as an unconfirmed candidate for its own USB detect; nothing
  equivalent has been looked for here. Giving the board a real detect would make
  `AfterUSBPower` reachable and stop a power-on being read as a button.
