# Windowed / partial refresh on the T5 S3 Pro

How to make `displayWindow()` actually cost less than a whole-panel frame on
the LilyGo T5 S3 4.7" Pro. Written 2026-09-07 from a read of our own driver,
of LovyanGFX's `Panel_EPD` (the engine we already run on this board), and of
the two projects that solved a version of this on the same panel: **FastEPD**
(bitbank2, <https://github.com/bitbank2/FastEPD>; notes in
[`fastepd.md`](fastepd.md)) and **OpenTrailPaper**
(<https://github.com/RaemondBW/OpenTrailPaper>; notes in
`docs/prior-art-opentrailpaper.md` in the parent repo).

Nothing here is built. Nothing here is measured on hardware except where the
line says so.

## 1. The window request dies in the driver base class

The whole chain from the map down exists and works:

```
MapActivity -> GfxRenderer::displayBufferWindow()   lib/GfxRenderer/GfxRenderer.cpp:1584
            -> HalDisplay::displayWindow()          lib/hal/HalDisplay.cpp:118
            -> FreeInkDisplay::displayWindow()      freeink-sdk/.../FreeInkDisplay.cpp:685
            -> PanelDriver::displayWindow()         freeink-sdk/.../driver/PanelDriver.h:53
```

`Ssd1677Driver` overrides that last call and drives only the requested
rectangle (`Ssd1677Driver.cpp:424`). **`LgfxEpdDriver` does not override it at
all.** So the base implementation runs:

```cpp
virtual void displayWindow(EpdBus& bus, const uint8_t* fb, const uint8_t* prev,
                           uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOff) {
  display(bus, fb, prev, RefreshMode::Fast, turnOff);   // PanelDriver.h:53-56
}
```

The rectangle is dropped on the floor and a whole-panel frame is pushed.

That is the **1,081 ms** `[measured on the T5 S3 Pro, 2026-09-05]`. Instrument:
the device's own `PowerTelemetry` counters over one 4 h 36 min walk, read out of
`power.csv` afterwards -- `panel_busy_ms` 2,944,634 over `ref_window` 2,608
window refreshes plus 29 whole-panel ones, holding between 1,049 and 1,101 ms
across seven segments. That figure is recorded on `develop` in
[`refresh-modes.md`](refresh-modes.md) under "The T5 S3 Pro is not the X4"; this
branch carries only the short pointer. **All 2,608 of those were full-screen
refreshes wearing a window's name.**

One doc in the parent repo still needs fixing: `docs/prior-art-opentrailpaper.md`
says "we already have ... an explicit `displayWindow(x, y, w, h)`". True of the
X4, false of this board.

## 2. What `Panel_EPD` already gives us

The board runs LovyanGFX's `Panel_EPD`, bundled inside M5GFX
(`.pio/libdeps/t5s3pro/M5GFX/src/lgfx/v1/platforms/esp32/Panel_EPD.cpp`, read
2026-09-07). It is not a dumb blitter. It is a per-pixel state machine, and it
already does most of what we would otherwise write ourselves.

**Vocabulary, used below.** A *pass* is one whole-panel scan: every row clocked
out once. A *LUT* here is a table of passes -- one row of the table per pass, one
column per grey level, each cell saying `1` drive to black, `2` drive to white,
`3` do nothing, `0` end. *Arming* a pixel means recording a new target for it in
the step framebuffer, so the next passes drive it. FastEPD's wiki makes the
naming point: this is not a "waveform", it is a fixed list of digital steps
([`fastepd.md`](fastepd.md), 2a).

### 2a. Three buffers, all in PSRAM

| buffer | what | size at 960x540 |
|---|---|---|
| `_buf` | the panel's own 4bpp image | 259.2 kB |
| `_step_framebuf` | 2x `uint16_t` per pixel: current step + reserved next | 1,036.8 kB |
| our `g_canvas` `LGFX_Sprite` | 8bpp copy we fill and push | 518.4 kB |
| our `g_lsb` + `g_msb` | 1bpp grey overlay planes | 129.6 kB |

`Panel_EPD::init_intenal()` allocates the first two (`Panel_EPD.cpp:232`, `:235`);
`LgfxEpdDriver::allocCanvas()` allocates ours
(`freeink-sdk/.../driver/LgfxEpdDriver.cpp:133-147`). **About 1.94 MB of PSRAM
for one panel**, and the sprite is a pure duplicate of `_buf`.

### 2b. The diff is per pixel and it is the driver's own model

`task_update()` (`Panel_EPD.cpp:895`) walks the requested rectangle and arms a
pixel only when its new target differs from what is already recorded:

```cpp
if (d1 != s0) { d[1] = s0; d[0] = s0 - 0x8000; }   // Panel_EPD.cpp:948-951
```

Pushing an unchanged frame arms nothing. This is exactly epdiy's property that
OpenTrailPaper's `investigations/display-ghosting.md` opens with, and it has the
same consequence: **a re-push cleans nothing**. Only a mode that inserts the
eraser LUT resyncs the glass.

`_step_framebuf` is the driver's model of what is on the glass. Anything we keep
beside it is a private shadow, and OpenTrailPaper's third trap says a private
shadow drifts until a pixel is invisible to both records forever. **Our
`g_canvas` sprite is that shadow today.** It is harmless only because we refill
all of it from the 1bpp frame on every frame. The moment we scope the fill, it
becomes stateful and the trap is live.

### 2c. The rectangle scopes the CPU work, never the scan

`display(x, y, w, h)` (`Panel_EPD.cpp:553`) accumulates the rectangle into
`_range_mod` and queues it. `task_update` arms only rows `y .. y+h` and only
columns `x .. x+w`. Then it scans:

```cpp
for (uint_fast16_t y = 0; y < mh; y++) { ... bus->writeScanLine(...); }   // :1048-1060
```

Unconditional, every row, every pass. Same finding as FastEPD's
`bbepPartialUpdate` ([`fastepd.md`](fastepd.md), 4a): the panel's gate scan
cannot be shortened, only the data prep can. Two independent libraries on this
panel class agree, so treat it as settled.

### 2d. Pass count comes from the LUT, and ours are not what you would guess

Counted off the arrays (`Panel_EPD.cpp:82-156`, and
`freeink-sdk/libs/hardware/BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp:26-38`).
A `~0u` row is all-`3`, an idle settle pass; `0u` ends the table.

| mode | LUT in our build | drive passes | idle passes | total |
|---|---|---|---|---|
| `epd_fast` | our `kFastLut` | 8 | 1 | **9** |
| `epd_fastest` | our `kFastLut` (aliased) | 8 | 1 | **9** |
| `epd_text` | LovyanGFX default | 12 | 19 | **31** |
| `epd_quality` | LovyanGFX default | 15 | 16 | **31** |
| eraser (prepended by non-fast modes) | LovyanGFX default | 2 | 1 | 3 |

`LgfxEpdDriver::epdModeFor()` maps `Full` and `Half` to `epd_text` and
everything else to `epd_fast` (`LgfxEpdDriver.cpp:105-111`). So a clean frame on
this board is **34 passes against a fast frame's 9**, and it runs a LUT the
board never tuned.

Two knobs nobody has turned: `epd_fastest` is aliased to `kFastLut`, so asking
for it buys nothing today -- a real 5-drive-pass table would cut a marker move to
6 passes. And `lutQuality` / `lutText` are `nullptr` in our config
(`LilyGoT5S3LgfxConfig.cpp:157-181`), so the clean path runs an untuned default.

### 2e. The scan already runs in its own task, and we throw that away

`init_intenal()` starts `xTaskCreatePinnedToCore(task_update, "epd", ...)` on the
other core (`Panel_EPD.cpp:293`). The panel scan is already asynchronous.
`LgfxEpdDriver::pushCanvas()` then does:

```cpp
g_dev.waitDisplay();          // LgfxEpdDriver.cpp:182-188
g_dev.setEpdMode(epdMode);
g_canvas->pushSprite(0, 0);
g_dev.waitDisplay();          // <- blocks the caller for the whole scan
```

and `LgfxEpdDriver` never overrides `supportsAsyncDisplay()`, so the facade's
async path is off on this board. `refresh-modes.md` records the consequence:
the map loop spends up to a quarter of its wall clock inside a blocking panel
call. **The asynchrony is already paid for and we do not collect it.**

## 3. Cost model: what scoping actually buys

Per whole-panel frame today, in order:

1. `fillCanvasBW()` -- 518,400 PSRAM byte writes, bit-extracted one at a time
   (`LgfxEpdDriver.cpp:149-160`). Scales with area.
2. `pushSprite(0,0)` -- 518,400 pixels through `Panel_EPD::writeImage` and
   `_draw_pixels`, Bayer-dithered into `_buf` (`Panel_EPD.cpp:375`, `:467`).
   Scales with area.
3. arming loop in `task_update` -- reads 259.2 kB of `_buf`, reads and writes
   1,036.8 kB of `_step_framebuf`. Scales with area.
4. 9 passes of `blit_dmabuf` + `writeScanLine`. Each pass reads the **whole**
   1,036.8 kB step framebuffer and clocks 540 x 248 = 133,920 bytes.
   **Does not scale with area.**

Bus time per pass, `[derived]`: 133,920 bytes on an 8-bit bus at
`busHz` 16,000,000 (`LilyGoT5S3LgfxConfig.cpp:167`) is **8.4 ms**, so nine
passes is about 75 ms of bus. The measured frame is 1,081 ms. **Roughly 93 % of
a T5 S3 Pro refresh is CPU and PSRAM traffic, not panel time.**

That is the opposite of the X4, where the 500 ms `FAST` is waveform-fixed and
"a windowed `FAST` costs the same as a whole-panel one"
([`refresh-modes.md`](refresh-modes.md)). **Do not carry the X4's intuition
here.** On this board area is the thing to minimise.

Estimated split, `[derived, needs measurement]`: steps 1-3 scale with area and
should be most of the ~1,000 ms; step 4 is a floor of maybe 300-400 ms
(nine passes, each reading 1 MB of PSRAM plus 8.4 ms of bus). So a small window
plausibly lands at **300-400 ms instead of 1,081 ms** -- a 3x win, not a 20x one.
Anyone who quotes a bigger number has not counted step 4.

**The instrument that would settle it**: `micros()` brackets around
`fillCanvasBW`, `pushSprite` and `waitDisplay` inside `pushCanvas`, one build,
one map session, read off the serial log. That measurement should come before
the work, not after -- it says whether step 3 or step 4 is the wall.

## 4. What the two reference projects say

### FastEPD (drives this exact panel, `BB_PANEL_LILYGO_T5PRO`)

- **4a, and it matches `Panel_EPD` exactly**: a row-ranged partial update still
  clocks every row. Budget a window as a full-duration scan.
- **4b, a real bug in its ping-pong buffer** when the row range actually
  narrows: rows outside the band are zeroed only on the first skipped row, so
  later skipped rows re-clock a stale pattern. If we ever write our own scan
  loop, do not copy that one.
- Its passes are ~32 ms each against our ~8.4 ms of bus, and it defaults to
  4 partial / 5 full passes -- so its whole partial update is ~130 ms. Our nine
  passes are not the expensive part; our PSRAM traffic is.
- `bKeepOn = false` by default and `// This clear to neutral step is necessary;
  do not remove` on every path. Both are arguments for dropping the rails after
  a scan, which `task_update` already does when `remain == false`
  (`Panel_EPD.cpp:1068`).
- A documented ESP32-S3 LCD-peripheral erratum corrupts the start of each line;
  FastEPD works around it with an opt-in bit-banged bus. If scoped updates ever
  show a corrupted left edge, that is the first suspect, not our code.

### OpenTrailPaper (same board, bike head unit)

- **The reason to do this at all**: they measured that pushing a full-screen
  differential update for one moving GPS dot "was electrically disturbing all
  518,400 pixels". Their fix was to scope each update to the rows that changed.
  We are one step behind that -- we do not even scope the CPU work.
- **Diff against the driver's model, never a private shadow.** See 2b. Our
  sprite is the shadow.
- **Two-phase flash through white** is the answer to residue inside a dithered
  dark area: force the region white and clean, then restore content and clean.
  A single inverting pass fails because adjacent dither pixels drive in opposite
  directions. Relevant the moment map hatching meets a scoped clean.
- **Snap anti-aliased text to 1 bit before refresh.** They measured ~20 % of one
  bundled font's glyph pixels sitting at intermediate levels, and a mid grey
  inside a frequently-redrawn region ghosts. Our board config already carries AA
  grey columns in `kFastLut`, which is the other way to handle it -- worth
  knowing both answers exist.
- **Scrub on transitions, not on a counter.** Their `FULL_REFRESH_EVERY 60` is
  defined and used nowhere; a count-based periodic clean was their first theory
  and it lost. Read that before anyone builds a `ghostClearInterval` here.
- **Defer the expensive clean** until ~900 ms of no input, each new tap pushing
  it out. Fast dirty frames during a zoom burst, one clean at the end.
- **Async trap**: their `paint()` returned while rows were still clocking, and a
  rail cut mid-drive "is how ghosts get baked in". Ours is the same shape once
  step 6 below lands.

## 5. The plan, cheapest first

**T-269. Instrument the frame.** `micros()` brackets in `pushCanvas` around the
three phases. One build, one session. No behaviour change. This decides whether
the rest is worth it and gives every later claim a baseline.

**T-270. Collect the asynchrony that already exists.** Implement
`supportsAsyncDisplay()`, `displayStart()` and `displayFinish()` on
`LgfxEpdDriver` over `Panel_EPD`'s existing task: `displayStart` pushes and
returns without the trailing `waitDisplay()`, `displayFinish` waits. No panel
behaviour changes at all -- the same bytes reach the glass in the same order.
Frees up to ~1 s of CPU per map frame for tile work. Two rules come with it:
never sleep or cut rails between start and finish (FastEPD's neutral-pass
comment, OpenTrailPaper's rail-cut bug), and keep `fadingFix` users on the
blocking path exactly as `GfxRenderer::displayBufferAsync` already does.

**T-271. Override `displayWindow` on `LgfxEpdDriver`.** The library needs no
patch -- `LGFXBase::display(x, y, w, h)` is public (`LGFXBase.cpp:95`) and
`Panel_EPD::writeImage` already narrows `_range_mod` to whatever rectangle was
written. The shape:

```cpp
g_dev.setAutoDisplay(false);                    // once, in begin()
// ... convert only rows y..y+h of the 1bpp frame into an 8bpp scratch row band
g_dev.setEpdMode(lgfx::epd_mode::epd_fast);     // same mode as the whole-panel path
g_dev.pushImage(x, y, w, h, band, lgfx::grayscale_8bit, nullptr);
g_dev.display(x, y, w, h);                      // explicit, see the cache trap below
```

Alignment is compatible: `Panel_EPD::display` floors `x` to even and rounds `w`
up by two for 4bpp packing (`Panel_EPD.cpp:573-580`), and
`GfxRenderer::displayBufferWindow` already hands us an 8-pixel-aligned rectangle
in panel memory coordinates via `screenRectToAlignedMemRect`
(`GfxRenderer.cpp:1584-1590`). Panel geometry is landscape 960x540
(`BoardConfig.h:878`) and `rotation` is 0, so no coordinate translation is
needed in the driver.

**T-272. Delete the sprite.** Once T-271 pushes bands directly, `g_canvas`
(518.4 kB PSRAM) has no reader for the B/W path and the private-shadow trap goes
with it. The grey path (`fillCanvasGray`) needs the same treatment or a scratch
band of its own. Feeds the planned firmware memory audit.

**T-273. Tune the pass count.** Give `epd_fastest` a real 5-drive-pass table
instead of aliasing `kFastLut`, and use it for marker-only moves; give
`lutText` a tuned table so a clean frame is not 34 default passes. Both are data,
not code. FastEPD's `gray_matrix_editor` is the prior art for the iteration loop
— live edit, live redraw, dump source -- and it is the same shape as
`tools/style_watch.py`.

**T-274. A scrub policy, from OpenTrailPaper's, not from a counter.** Scoped
clean on named transitions: entering the map, a popup closing over it, a page
flip. Deferred until input goes quiet. Two-phase through white where the region
is dithered. Explicitly not a `ghostClearInterval`.

Order matters: T-269 before anything, T-270 is free and independent, T-271 is the
actual feature, T-272 follows from it, T-273 and T-274 are policy on top.

## 6. Traps, written down before anyone hits them

- **Mixing epd modes defeats the diff.** Already learned on this board and
  recorded in the board config: `Panel_EPD` embeds the mode's LUT offset in the
  stored per-pixel value, so alternating modes between two pushes of one page
  re-drives every pixel and flashes the whole screen black
  (`LilyGoT5S3LgfxConfig.cpp:18-25`). A windowed path must use the same mode as
  the whole-panel path it interleaves with.
- **Auto-display writes back zero bytes of cache.** `Panel::endWrite()` calls
  `display(0, 0, 0, 0)` (`Panel.hpp:88`), and `Panel_EPD::display` computes its
  cache writeback from the **arguments**, not from `_range_mod`:
  `cacheWriteBack(&_buf[y * panel_width >> 1], h * panel_width >> 1)` with
  `y = h = 0` flushes nothing (`Panel_EPD.cpp:586`). The update task runs on the
  other core (`:293`, and the file's own comment says the cache must be
  synced across cores). It works today by luck, not by contract. Turn auto-display
  off and call `display(x, y, w, h)` explicitly.
- **The private shadow.** See 2b. Either keep filling the sprite whole or delete
  it; never scope the fill and keep the sprite.
- **`_range_mod` accumulates.** Two writes before one display give the bounding
  box of both, not two updates. Fine, but it means an unrelated stray draw
  silently widens a window.
- **A window cannot clear the glass.** `FreeInkDisplay::displayWindow` already
  refuses to spend a pending `requestCleanNextFrame()`
  (`refresh-modes.md`), and the same logic holds here: only an eraser-prefixed
  mode resyncs the model to the panel.

## 7. Status

Everything in sections 1, 2 and 6 is **read off the source**, cited above, at
`release/lilygo-t5-s3-pro` `aa831fcb` and M5GFX as vendored in
`.pio/libdeps/t5s3pro` on 2026-09-07. The 1,081 ms is **measured on the T5 S3
Pro** (one 4 h 36 min walk, 2,608 refreshes, instrument named in section 1). The 8.4 ms bus
figure per pass is **derived** from `busHz`, row count and `write_len`. The
93 % CPU share and the 300-400 ms projected window cost are **derived and
unverified** -- T-269 exists to replace them with numbers. Nothing in section 5
has been built.
