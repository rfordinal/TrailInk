# Touch modes

What a touch panel is allowed to do, and when the on-screen button boxes are
drawn. Added 2026-09-05. **Not yet run on hardware** — see "What a hardware pass
has to check" at the bottom.

## The problem it fixes

`gpio.hasTouch()` answered two different questions at once. It says whether the
board has a digitizer (`BoardConfig.h`, `hasTouch()` reads
`ACTIVE.touch.controller`), and every theme also used it as a *policy*: a board
with a panel drew no button hints at all and every pixel of the screen was live.

So on a T5 S3 Pro the four bottom boxes and the two side boxes vanished, and the
only input left was touch anywhere plus whatever hardware keys the board has —
which on that board is almost none (BOOT on GPIO0, plus a user button behind the
PCA9535 expander). There was no way to ask for the boxes back.

## The three modes

One Controls setting, `touchMode` (`src/CrossPointSettings.h`, enum `TOUCH_MODE`).
It is only offered on a board that has a digitizer; `SettingsList.h` drops the
row otherwise.

| Mode | Boxes drawn | What touch does |
|---|---|---|
| `TOUCH_ANYWHERE` (0, default) | no | everything: list rows, swipes, edge gestures, map drag |
| `TOUCH_BUTTONS_ONLY` (1) | yes | only the six boxes, each acting as its hardware button |
| `TOUCH_DISABLED` (2) | yes | nothing, and touch stops counting as user activity |

Default is `TOUCH_ANYWHERE`, so a device that was already in use behaves exactly
as it did before the setting existed.

Both non-default modes draw the boxes. On a board with no keys under the glass
the box *is* the button, so hiding it would leave the mode unusable; on an X4 the
box labels the key beneath it, which is what it always did.

## Where the decision lives

`src/TouchPolicy.h` — four questions, one place:

- `touchAnywhere()` — the whole screen is live.
- `touchHintBoxes()` — only the boxes are.
- `touchActive()` — any touch reaches the UI at all (false in OFF).
- `hintsVisible()` — the boxes are drawn and the layout reserves room for them.

Nothing else reads `gpio.hasTouch()` for policy any more. The themes and
`UITheme::getMetrics()` ask `hintsVisible()`; `MappedInputManager` asks the other
three.

## How a tap becomes a button press

Four HAL touch primitives are the only way touch enters the firmware
(`src/MappedInputManager.cpp`: `wasScreenTapped`, `wasScreenTouchDown`,
`isScreenTouchHeld`, `decodeSwipe`). Each returns false unless
`TouchPolicy::touchAnywhere()`. Every other touch helper — list hit tests,
`rowTouch`, `colTouch`, the home/menu/back gestures — is built on those four, so
one gate switches all 96 call sites off.

In BUTTONS mode, `MappedInputManager::pumpHintTouch()` runs once per
`update()` and turns contact into button edges:

- touch down inside a box: that box's hardware button reports `wasPressed` for
  one frame, and `isPressed` until the finger leaves.
- tap released on the **same** box: `wasReleased` for one frame, the way a
  physical key does not fire when the finger slides off it. It also calls
  `rememberTouchHeldTime()`, so `getHeldTime()` returns the contact duration and
  long-press behaviours work off a box.
- lifted without producing a tap: the held state is cleared with no release
  event. Without this branch the button would stay held for good.

Every hardware read in `mapButton()` goes through `rawButton()`, which ORs the
synthetic state in before asking `HalGPIO`. That is why nothing downstream
changed: the front-button remap (`frontButtonBack` and friends), the
orientation-following axis swap, the reader's side-button layout and every
activity's `wasPressed(Button::Confirm)` all work on a box tap unmodified.

**Box index is the hardware button index.** Front box 0..3 are
`BTN_BACK`, `BTN_CONFIRM`, `BTN_LEFT`, `BTN_RIGHT` — the same order
`mapFrontLabels()` hands the labels to `drawButtonHints()`, so the box under a
label really is the key that label names. A `static_assert` in `hintBoxAt()`
holds that. Side boxes are `BTN_UP` (top) and `BTN_DOWN` (bottom), which the
reader's page pair maps onto.

### A box with no label is not a button

Screens pass an empty label for a key they do not use, and the themes draw
nothing (or, in Lyra, a stub) there. The input layer never sees labels, so each
theme records the last painted set (`BaseTheme::rememberFrontLabels()` /
`rememberSideLabels()`) and `frontHintBox()` / `sideHintBox()` return false for
an empty one. Without it a tap on blank glass where a box used to be would still
fire its button.

### The hit test runs in portrait

The boxes are always painted in portrait — every `drawButtonHints()` forces
`Orientation::Portrait` and restores the caller's afterwards. `tapToLogical()`
maps a touch to whatever orientation is *currently* being drawn, which the reader
rotates. So `MappedInputManager::tapToPortrait()` applies the portrait transform
directly (the same arithmetic as `GfxRenderer`'s `Portrait` branch) and the hit
test compares against portrait rects.

## Geometry on a panel that is not an X4

The position arrays in each theme are X4 and X3 numbers, hand-tuned so a box sits
above the key it names. Those are never derived and are unchanged.

Any other panel has no keys under the boxes, so there is nothing to line up with
and the only requirement is that the six boxes land on screen in the same
arrangement. `src/components/themes/HintGeometry.h` scales the X4's 480x800
portrait layout: X by `screenWidth / 480`, Y by `screenHeight / 800`.

`UITheme::getMetrics()` scales `buttonHintsHeight` (by the Y factor) and
`sideButtonHintsWidth` (by the X factor) the same way when the boxes are visible.
This matters: the T5 S3 Pro is 540x960 in portrait at about 234 PPI, where an
unscaled 40 px band is a 4.3 mm tap target. Scaled it is 48 px, about 5.2 mm —
still small, and a candidate for a deliberate touch-sized band later rather than
a scaled button-era one.

Boards and what they get:

| Board | Portrait screen | Layout |
|---|---|---|
| X4, X4 Pro | 480x800 | the X4 arrays, unscaled |
| X3 | 528x792 | the X3 arrays, unscaled |
| T5 S3 Pro | 540x960 | X4 arrays scaled x1.125 / x1.2 |

`BoardConfig::ACTIVE.displayHeight` is the portrait width (the profile stores the
panel's native landscape size), which is how `HintGeometry` answers without a
renderer — `ThemeMetrics` is a singleton with nothing to ask.

## The X4 Pro trap

The X4 Pro has a digitizer plus **two** hardware keys (`Left` on GPIO0, `Right`
on GPIO7) and the capacitive Home key. Back and Confirm come from touch. So
`TOUCH_DISABLED` on an X4 Pro leaves no way to confirm or go back. The setting
does not currently block that — see the parent repo's `docs/TODO.md`.

## What a hardware pass has to check

Built and host-tested only: `default` (esp32c3) and `t5s3pro` (esp32s3) both
compile clean with no new warnings, 424/424 host tests pass. None of that says
anything about a finger on glass.

1. **T5 S3 Pro, BUTTONS mode.** Are the six boxes on screen, in the X4
   arrangement, fully inside 540x960? Does a tap on each fire the right action?
2. **Do the boxes match the keys they name** as screens change (home, settings,
   reader, map) — and does a tap on a box with no label do nothing?
3. **Slide off.** Press a box, drag off it, lift. Nothing should fire, and the
   next press must still work (the stuck-held branch).
4. **Long press on a box** — chapter skip / the long-press menu, whichever the
   settings select. `lastTouchHeldMs()` is written on release
   (`InputManager.cpp`), so a long press should report its real duration; this is
   read off the code, not measured.
5. **OFF mode.** Nothing on the panel reacts, and the device still sleeps on
   time with a palm resting on the glass.
6. **X4 regression.** The bottom band and the side boxes must be pixel-identical
   to before — the X4 arrays and metrics are untouched, so any difference is a
   bug in this change.
