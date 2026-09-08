# Pressing the buttons from the host: `CMD:BUTTON`

Devel builds accept a serial command that presses a hardware button.

```
CMD:BUTTON down          ->  BUTTON_OK:down:0
CMD:BUTTON back 1500     ->  BUTTON_OK:back:1500
CMD:BUTTON middle        ->  BUTTON_ERR:unknown:back,confirm,left,right,up,down,power
```

Names: `back` `confirm` `left` `right` `up` `down` `power`. Case insensitive.
Second argument is the hold in milliseconds, 0 (a tap) to 10000.

Host side: `tools/press.py` in the parent repo.

## Why

Before this, a laptop could reach two screens and no more. `CMD:GOTO_MAP` and
`CMD:GOTO_TILESYNC` put the map and the sync screen up; `CMD:SCREENSHOT` reads
the panel back. Home, the file browser, the reader, settings, the map menu and
every confirm prompt in between need a thumb on the device.

Two costs came out of that. A screen nobody can walk to gets reviewed when
somebody is standing at the device and not otherwise -- the same gap
`CMD:GOTO_TILESYNC` was added to close (`docs/tile-freshness.md`, "The check
queue is dots"). And a bug report that starts "press back twice in the reader"
cannot be reproduced without the hardware in hand. The simulator has no thumb
either, and still does not -- see "The simulator compiles it but cannot reach
it" below.

## Where it is injected

`MappedInputManager::rawButton()` (`src/MappedInputManager.cpp`), in front of
the real read:

```
hintButton(index, fn) || injectedButton(index, fn) || (gpio.*fn)(index)
```

The touch hint boxes already sit there, so an injected press is the second
synthetic source on a path activities cannot tell from a thumb.

Not in `HalGPIO`, deliberately. `lib/hal/` is replaced wholesale by the
simulator's own `HalGPIO` (`docs/simulator.md`), so a HAL-level injector would
have to be written twice and kept in sync. `src/` is compiled by both.

The price of that choice: **three places read `HalGPIO` directly and never see
an injected press.**

| what | where | effect |
|---|---|---|
| long-press-to-sleep | `src/main.cpp`, `getPowerButtonHeldTime()` | `CMD:BUTTON power 5000` cannot sleep the device -- **read off the code, not measured** |
| POWER+DOWN screenshot combo | `src/main.cpp` | not reachable; `CMD:SCREENSHOT` already is |
| the reader's own POWER+DOWN check | `src/activities/reader/EpubReaderActivity.cpp` | not reachable |

The first one is the good half. A host script cannot drop the port it is
talking through.

The short power press *is* covered: `main.cpp`'s force-refresh path reads
`mappedInputManager.wasReleased(Power)`.

## The timing model

`src/DebugInput.cpp`. One press at a time, the rest queued (8 deep), each press
advanced one step per input frame -- a frame being one `gpio.update()` in
`loop()`, about 10 ms.

```
frame N     wasPressed + isPressed, held time 0
frame N+k   isPressed, held time = now - down
frame N+m   wasReleased once, held time = the total (>= the requested hold)
frame N+m+1 idle; the next queued press may start
```

Three things in that shape are load-bearing:

- **The release frame carries the total held time.** Half the UI reads
  `wasReleased(X) && getHeldTime() < N` -- the reader's back button is one
  (`src/activities/reader/ReaderUtils.h`). A release frame reporting zero would
  make every long press look short.
- **`getHeldTime()` answers the injected time while a press is down.**
  `MappedInputManager::getHeldTime()` returns `DebugInput::heldMs()` whenever an
  injected press owns the frame. Otherwise the real hardware time under it is a
  stale zero and no `isPressed(X) && getHeldTime() >= N` path can ever fire.
- **The idle frame after a release.** Without it two queued taps run into each
  other as one long press, and `back back` would long-press to Home instead of
  stepping up two levels.

An injected press also counts as user input in `loop()`, for both the
auto-sleep deadline and the CPU throttle. A host walking the UI otherwise
watches the device throttle and then sleep under it.

## Security

Devel builds only: `ENABLE_BUTTON_CMD`, set in `default`, `sticky` and
`simulator`, absent from `gh_release`, `gh_release_rc` and `slim`. The release
build has no injector compiled in at all -- `DebugInput.cpp` is empty there and
the call sites inline to `false`.

**Measured 2026-09-08**, not only reasoned from the `#ifdef`: a `gh_release`
build carries no `BUTTON_OK` or `BUTTON_ERR` string and no `DebugInput` symbol,
while the archived devel build carries one and three. So the check could have
failed.

The reason is the standing one: the device gets lost or stolen, and the person
holding it can plug in USB. A press injector is a thumb for that person -- walk
the menus, open the rider's books, read their pins, all without touching the
device's buttons.

Serial only. It is **not** in the `MapCommandParser` grammar, which BLE shares
(`src/activities/map/MapCommandParser.h`). BLE advertises with no pairing and
no bonding (`docs/ble-advertising.md`), so a BLE version would hand that thumb
to anyone in radio range instead of to someone holding a cable. Both channels
are unauthenticated today (T-222 in the parent repo's `docs/TODO.md`), which is
why the cable is the smaller surface rather than a safe one.

The command reveals nothing by itself: the reply is the button name back.

## The simulator compiles it but cannot reach it

`src/` is shared, so the simulator build carries `DebugInput` and the injection
point. It has no way to send a command: its `HardwareSerial::available()`
returns 0 (`src/HardwareSerial.h` in the simulator fork) -- read off that
header, never tried -- so nothing ever reaches `main.cpp`'s `CMD:` branch
there. Driving the simulator's UI from a
script needs a serial-input path in the fork first, or a different door
altogether -- the fork's JSON socket (`docs/simulator.md`).

## Tested

- `test/debug_input` -- nine host tests over the frame shape: press and release
  edges, hold, two taps not merging, queue order, a full queue refused, a
  double pump in one frame, name parsing. `ctest` runs them with the rest.
- **Verified on the LilyGo T5 S3 Pro, 2026-09-08**, but from the
  `release/lilygo-t5-s3-pro` line, not from this one: the board is 200 commits
  ahead of `develop` and a develop build would have dropped its bring-up. The
  commit was cherry-picked there (`cmd-buttons-t5s3`, env `t5s3pro`) and the
  whole run is written up in that branch's copy of this file. In short: a walk
  from Home to the map and back, driven from the laptop with no thumb on the
  board, all seven button names pressed, `--hold 1500 up` zooming in Look
  around where a plain `up` pans, and the two logged runs showing the CPU coming
  out of power saving on the press. The `power` press was a 0 ms tap, so
  nothing about the long hold that the table above rules out has been measured
  (T-286 in the parent repo).
- **Not run on a C3** (X4, X4 Pro). The injection point is board-agnostic
  `src/` code and the `default` build is clean, but no C3 has been flashed with
  it.
