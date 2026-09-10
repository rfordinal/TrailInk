# The satellite wait: a screen in front of the map

**Status: built 2026-09-10, never run on hardware.** Nothing below is a
measurement. What a hardware pass has to check is at the bottom.

On a board with a receiver, opening the map used to mean opening the map and
finding out. This puts a screen in between: the sky as the receiver sees it,
with two ways out.

## Why it exists

The map already had a waiting state, `STR_MAP_WAITING_GNSS` over an empty panel.
It answers none of the questions a rider actually has while it is up.

The numbers are the argument. A ride on 2026-09-01 took **526 s to first fix**
(`gnss.md`, "The map reads it"). A 15-minute walk on 2026-09-04 got **no fix at
all** while the receiver tracked one satellite the whole time. A banner cannot
tell those two apart, and they call for opposite actions: wait, or go and stand
somewhere with sky.

So the screen shows what the receiver hears, and the rider decides. That is the
thesis position too -- the device answers *where am I and what is around me*, so
"why do I not know where I am yet" is a question it owes an answer to, and
"searching..." is not one.

## What is on it

Top to bottom:

- **The home screen's own header art** (`src/images/HomeHeader.h`) -- logo,
  wordmark, mountain line. Reused rather than drawn again so this reads as part
  of the device rather than as a diagnostic panel.
- **The sky**, as a panorama: azimuth left to right, elevation up from a mountain
  ridge along the bottom. South at both ends, north in the middle. One mark per
  satellite the receiver has located, filled when it is being heard and an
  outline when it is not.
- **Cardinal ticks** under the horizon -- S W N E S -- so "which way do I move"
  has an answer. Bare letters, not translated, the same choice the map's compass
  makes for its `N`.
- **The readout**: satellites in view and satellites heard, the best signal in
  dB-Hz next to **the map header's own GNSS block** at a readable size, the
  elapsed wait, and one line of advice.
- **Two action rows**, and a Back that goes home.

### Why a panorama and not a skyplot

A GNSS skyplot is conventionally a circle seen from above. That is the right
picture for checking geometry and the wrong one for a rider, because the thing
in front of them is a horizon. **North-up-on-a-circle answers "which
satellites"; a panorama answers "which way is the sky open", which is the only
action available to someone waiting for a fix.**

It is also cheaper: two divisions per satellite, no trigonometry
(`src/activities/map/GnssSkyView.h`).

### Why the ridge is not decoration

The bottom of the sky is a mountain silhouette, and it states the physical fact
behind a slow fix: a satellite low in the sky is behind terrain. A mark that
sits inside the ridge is one the rider should not expect help from. Those marks
are drawn **in white** so they read against the black silhouette instead of
disappearing into it -- they are exactly the information the screen exists to
give.

The profile itself is eleven hand-drawn numbers
(`GnssSkyView::kRidgeProfile`), interpolated per column. It is the one part of
this screen that is art rather than data.

### The signal ladder is the header's, not this screen's

Both the block next to the readout and the size of each satellite's mark read
`MapGnssBars`' calibrated rungs -- 4/8/12/16 satellites tracked for the bar
count, 26/31/36/40 dB-Hz of best C/N0 for the height (`map-header-status.md`,
the maintainer's numbers against real readings from this L76K, 2026-09-10).

**Deliberately the same instrument on both screens.** This is where a rider
first meets it, with minutes to look at it, and a wait screen that scored the
sky on its own invented ladder would teach them to read the header wrongly. The
earlier version of this screen had exactly that: thresholds of 18/24/34 picked
by eye. `test/gnss_sky_view` now asserts the two agree against the constants
rather than against copies of them.

Two differences, both stated in code:

- **No hysteresis here.** `MapGnssBars::resolve()` takes a default `State`, so
  no slack is applied. The header damps because the map repaints per fix; this
  screen redraws at most once every five seconds and has nothing to damp.
- **Empty slots stay as outlines.** The header draws nothing below the first
  rung. Here the screen is up for minutes with nothing to show, and an
  instrument that disappears reads as a broken one.

The per-satellite mark folds the top rung into the one below it: the radius
ladder is 3/4/5/6 px and a fifth step would need a 14 px wide mark, which is too
big for a plot holding sixteen of them.

## The two ways out, and what they really choose

One position source per map session (`MapActivity`'s `bleInUse_`, maintainer's
call 2026-09-03), so the rows are not "wait" versus "do not wait". They pick a
source:

| row | receiver | map session runs | what it costs |
|---|---|---|---|
| **Open the map now** | keeps searching, handed to the map | GNSS, no BLE | nothing; the header glyph goes Seeking to Fixed when the fix lands |
| **Take position from the phone** | powered down here | BLE | the sky for this session; buys tile sync, the command channel and the phone's stabilised heading |

And the screen leaves on its own the moment a usable fix arrives -- same
acceptance test as the map's (`valid` latches on the first solution, `quality`
is what says the receiver still has satellites; 0 is no fix and 6 is dead
reckoning with nothing behind it).

**Back goes home, not forwards.** Same as the trip picker: a Back that
continued into the map would mean something different here than everywhere else.

## Ownership of the rail: the one trap

Whoever starts the receiver has to be the one to stop it, and this screen starts
it (`gnssStart()`, which also seeds it with the persisted last fix -- see
`gnss.md`). The map's own rule is that a receiver it finds already running
belongs to somebody else, so it declines to own it and leaves the rail up on
exit. That rule is right for a host `CMD:GNSS ON` session and wrong for a
handover.

Hence `MapActivity`'s `adoptRunningGnss` constructor flag: set only on the
handover from this screen, and it makes the map's `onExit()` drop the rail.
Without it the wait screen would leak a powered rail -- which also feeds the
LoRa radio -- every time a rider went to the map and then home.

The phone row drops the rail here instead, before the map opens.

## Where it is in the flow, and where it deliberately is not

`ActivityManager::goToGnssAcquire()` is the front door, and it falls straight
through to `goToMap()` unless there is something to wait for:

- the build has a receiver (`ENABLE_GNSS_CMD`), and
- `SETTINGS.mapGnssPosition` is on, and
- the receiver does not already have a usable fix.

So a second entry into the map inside one session shows nothing, and a device
with the setting off behaves exactly as before.

Two callers route through it: the Home screen's Explore row and the trip
picker's rows (which carry the chosen route through the wait untouched).

Three paths deliberately do not:

- **`CMD:GOTO_MAP`** -- host tooling that must land on the map itself. Every
  screenshot recipe in the parent repo depends on it.
- **The wake-into-map path** -- a resume, not a departure.
- **The trip picker's OOM fallback** -- an allocation has already failed there,
  and the wait screen is another one.

## The refresh budget, which is the reason the clock is coarse

A windowed refresh on the T5 S3 Pro costs **1,081 ms measured**
(`t5s3-partial-refresh.md`), the same as a whole panel today. A screen that
redrew per fix would hold the panel busy for a third of every second of a
ten-minute wait, for a picture that changed by one satellite.

So: **never more than one redraw per 5 s**, and only when the picture would
actually differ (satellite count, satellites heard, best signal, or the clock's
own 5-second step). The clock is deliberately coarse for exactly that reason --
a per-second timer would force a redraw with nothing new in it and read as a
device that is busy rather than one that is waiting.

A selection move refreshes only the two action rows, which is a much smaller
rectangle than the sky.

## Input, and why the rows are drawn as buttons

`MapActivity`-style logical buttons do not exist on every board. On the T5 S3
Pro the physical inputs are a side switch (tap = Confirm) and the capacitive
home key (tap = Confirm); Back is a touch gesture, and there is no Up or Down at
all (`lilygo-t5s3-bringup.md`). In the default touch mode no hint boxes are
drawn either (`touch-modes.md`).

So the screen accepts three things at once, and any one of them is enough:

- **Up / Down / Left / Right** move the highlight (X4, X4 Pro).
- **Confirm** activates the highlighted row (every board).
- **A tap on a row itself** activates it directly (any board with a digitizer,
  in any touch mode).

## What was added

- `lib/Gnss` -- a per-satellite snapshot: `GnssSatellite` (talker, PRN,
  elevation, azimuth, C/N0, and whether the position fields were present),
  `satelliteCount()` and `satellite(i)`. GSV already parsed those four fields
  per satellite and threw three of them away.

  It updates **in place per talker** and sweeps at the end of each constellation's
  GSV cycle: an entry disappears only when a *complete* cycle stopped listing it.
  A lost GSV sentence is ordinary on a 9600 baud line, and dropping four
  satellites out of the plot every time one goes missing would read as the sky
  emptying. Costs 32 entries of internal RAM, fixed, never grown.
- `src/activities/map/GnssSkyView.h` -- the projection, the signal buckets, the
  ridge profile. Pure arithmetic, no renderer, host-tested in
  `test/gnss_sky_view` (13 tests: north centring, azimuth wrap, out-of-range
  clamping, ridge continuity, a degenerate box).
- `src/activities/map/GnssAcquireActivity.{h,cpp}` -- the screen.
- `ActivityManager::goToGnssAcquire()`, plus two new `goToMap()` arguments
  (`adoptRunningGnss`, `forcePhonePosition`).
- `MapActivity`: `bleInUse_` now also honours `forcePhonePosition_`, and
  `gnssHeaderState()` / `pollGnssFix()` read `bleInUse_` instead of the setting
  -- so a session the rider sent to the phone neither draws a receiver glyph nor
  accepts a fix from a receiver something else is running.
- Ten `STR_GNSS_ACQ_*` strings in `lib/I18n/translations/english.yaml`. The
  build strips unused keys under SCons (`scripts/gen_i18n.py`), so a string added
  to the yaml and not yet drawn does not compile.

## What a hardware pass has to check

Nothing here has been on a panel. In rough order of what would embarrass us:

1. **Does the layout fit** on the T5 S3 Pro's 540x960 portrait screen -- the art
   is 480 px wide and centred, and the sky takes whatever the bottom-anchored
   rows leave. Check the ridge is not squeezed to nothing.
2. **Do the white marks read** against the black ridge, and do the black ones
   read against white sky at 3-6 px.
3. **Is the 5 s cadence right**, or does a wait screen that redraws twelve times
   a minute feel worse than one that redraws four times?
4. **Does the handover actually keep the fix?** `Gnss::begin()` treats a second
   call as a no-op by design, so the map must not restart the receiver -- and the
   rail must be **down** after the map exits, which is the `adoptRunningGnss`
   flag's whole job. Read it back with `CMD:GNSS STATUS` after leaving the map.
5. **Does the phone row leave BLE working**, with the setting still on.
6. **Does the screen leave by itself** when the fix lands, and how long after.
7. **Elevation and azimuth from this receiver**: the snapshot is read off GSV
   fields nothing here has ever used. Confirm with `CMD:GNSS RAW ON` that the
   marks land where the sentences say.
