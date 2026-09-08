# Settings menu: what the rider sees, and what was taken away

ExplorInk inherited CrossPoint's whole e-reader Settings screen. Most of it
configures books. This file says which rows went, which stayed, why, and what a
later pass still owes.

Status: **built and seen in the simulator, not flashed.** `pio run -e default`
(X4) and `-e simulator` both link clean, and a headless simulator run captured
all four tabs at 480x800 (`qa-artifacts/settings-facade/tab0..3.png`,
gitignored). Those are host renders on the X4 profile. Nothing here has been
looked at on a device panel yet.

## The staged removal, and why nothing is deleted yet

The reader is going away entirely, but that is a delicate operation: reader
activities, the file browser, EPUB/TXT/XTC parsing and the KOReader and OPDS
stacks all hang together. This pass is the **facade** only. A row stops being
reachable; the setting behind it keeps existing.

That distinction matters because `getSettingsList()` does two jobs:

- it builds the Settings screen (`SettingsActivity.cpp:52`), and
- it drives `settings.json` serialisation, both ways
  (`CrossPointSettings.cpp:66` writes, `:130` reads).

Delete an entry and the field stops being saved, so every device carrying a
stored value silently resets it on the next boot. So a hidden row keeps its
entry and gets `withHidden()` (`SettingsActivity.h:56`, `hiddenFromMenu`), which
only `rebuildSettingsLists()` looks at. Deleting the entries is the later,
real removal.

The old web settings page would have been the other consumer, but it was
removed before this (`webserver-endpoints.md`, "Device settings belong in the
device menus").

## Four tabs, not five

`categoryNames[]` (`SettingsActivity.cpp:30`) is now Display, Map, Controls,
System. The Reader tab is gone, and with it three sub-screens that were only
reachable from it:

| Went | What it was |
|---|---|
| Text Settings | 10 rows: font family, size, line spacing, margin, alignment, embedded style, focus reading, hyphenation, paragraph spacing, anti-aliasing |
| Manage Fonts | SD card font install/delete |
| Customise Status Bar | 11 rows, including the clock |

The status bar those 11 rows configure is the **reader's**. The map screen
draws its own header row and reads none of them — it takes the time from the
phone's BLE packet and ignores `SETTINGS.clockFormat` outright
(`MapActivity.cpp:1930-1932`, and `map-header-status.md`).

`TextSettingsActivity`, `StatusBarSettingsActivity`, `KOReaderSettingsActivity`
and `OpdsServerListActivity` are all still compiled and still constructible.
Only the rows that started them are gone.

## Rows hidden, and the consumer that proves them reader-only

Each of these was hidden because its only consumer is a reader activity. The
citation is the consumer, not the definition.

| Row | Tab | Only consumer |
|---|---|---|
| Refresh Frequency | Display | `ReaderActivity.cpp:34` — it counts pages of a book |
| Touch Reader Controls | Controls | `ReaderUtils.h` |
| Orient front buttons | Controls | `MappedInputManager.cpp:48`, keyed on the reader's orientation |
| Long-press button behavior | Controls | `EpubReaderActivity`, `XtcReaderActivity` |
| Long-press Menu | Controls | `EpubReaderActivity.cpp` — options are KOReader sync, bookmark, dictionary |
| Quick-return from footnotes | Controls | `ReaderUtils.h` — footnotes are an EPUB concept |
| Short Back to File Browser | Controls | `ReaderUtils.h` |
| Tilt Page Turn (X3 only) | Controls | the IMU, turning book pages |
| Show Hidden Files | System | `FileBrowserActivity.cpp` |
| Clear Read Books from Recent List | System | `EpubReaderActivity.cpp` |
| Move Finished Books to Read Folder | System | `EpubReaderActivity.cpp` |

Three System actions went the same way: **KOReader Sync** (reading-position
sync), **OPDS Servers** (an e-book catalogue) and **Clear Reading Cache** (the
reader's pagination cache).

## Screen Orientation: the one dimmed row

`CrossPointSettings::orientation` moved from the dead Reader tab into Display
under a new label, `STR_SCREEN_ORIENTATION` ("Screen Orientation"), and is
drawn **disabled**.

It is not hidden because a device carried in landscape — on a mount or in a
hand — will want a real screen orientation, and this is the field that will
carry it. It is not active
because today the field rotates the reader only — `EpubReaderActivity`,
`TxtReaderActivity` and `SleepActivity` read it, the map and the rest of the UI
do not. Offering it would rotate nothing the rider is looking at.

Three mechanics behind `disabled` (`SettingsActivity.h:59`):

- The row draws dimmed through `drawList()`'s `rowDimmed` callback
  (`BaseTheme.h:284`), a checkerboard dither on the text
  (`BaseTheme.cpp:499`).
- **The dither is skipped on the selected row** (`BaseTheme.cpp:499`, `i !=
  selectedIndex`), so dimming alone tells a rider standing on the row nothing.
  The Confirm hint is therefore blanked for a disabled row.
- **What an empty hint label draws is per theme, and the device does not run
  the plain one.** `BaseTheme::drawButtonHints()` skips an empty label and
  draws nothing (`BaseTheme.cpp:259`). `LyraTheme` — the default — draws a
  **short stub box** with no text instead (`LyraTheme.cpp:398-403`), so the
  slot is not empty, it is visibly shorter than its neighbours. Seen on a
  T5 S3 Pro panel 2026-09-07, on that board's release branch.
- **The stub is not tappable.** `rememberFrontLabels()` records only non-empty
  labels (`BaseTheme.cpp:380-383`), `frontBoxActive()` gates the hit test on
  that flag (`BaseTheme.cpp:390`), and `frontHintBox()` refuses a rect for an
  inactive slot. So the stub is drawn and dead — the behaviour wanted, arrived
  at by accident rather than designed. **Read, not measured.** The hardware
  pass ran with Touch Screen on Buttons only, where no list row is tappable at
  all, so the tap path was never exercised — only the Confirm button was.
  Setting touch to Anywhere and tapping the row would settle it.
- `toggleCurrentSetting()` returns early, so both the button path and the
  touch-tap path are inert.

**Read on hardware, on the T5 S3 Pro release branch, 2026-09-07.** The same
change cherry-picked onto `release/lilygo-t5-s3-pro` was flashed and grabbed at
540x960: four tabs cycling Display -> Map -> Controls -> System -> Display, the
hidden rows gone, and Confirm pressed three times on the disabled row changing
nothing. The maintainer's reading of the panel was "riadok je mrtvy". That
branch's copy of this file carries the detail.

**Still open: `settings.json` was not read back.** `CMD:SETTING` is a four-key
allow-list (`main.cpp:771-780`) and none of the hidden keys is in it, so the
claim that hidden rows keep being serialised is still read-off-the-code only.

**Only the label dims, not the value.** The simulator capture shows "Screen
Orientation" in dither grey with "Portrait" beside it in solid black:
`drawList()` applies the dither to the row title and draws the value at full
weight. It reads acceptably — the label is the part that says whether the row
is live — but it is not deliberate, and a hardware pass should say whether the
mixed weight is confusing on the panel.

**`RoundedRaffTheme` ignores `rowDimmed` entirely** (`RoundedRaffTheme.cpp:284`,
`(void)rowDimmed;`). Under that theme the row looks ordinary and only the
missing Confirm hint gives it away. Open — either that theme learns to dim, or
the row needs a second cue.

## What this pass did not do

- **Enum option lists were left alone.** Two rows offer book-only choices among
  useful ones: Hide Battery % has `In Reader`, and Short Power Button Click has
  `Page turn` and `Footnotes`. They cannot simply be dropped: enum settings are
  **persisted by index** (`SettingsList.h:194`, the comment at the top of
  `getSettingsList()`), so removing a middle option silently reassigns the
  values after it. That needs a migration, not an edit.
- **`STR_SIDE_BTN_LAYOUT` still reads "Side Button Layout (reader)".** The
  label is wrong: the field is a global remap in `MappedInputManager.cpp` and
  applies to the map's zoom ladder too. Renaming it in `english.yaml` alone
  leaves 30-odd translations saying "(reader)", so it wants its own pass.
- **No reordering.** The rows inside each tab are still in the order the
  reader-era list put them in.
