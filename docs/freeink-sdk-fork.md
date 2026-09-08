# The freeink-sdk submodule points at our fork

**Since 2026-09-03.** `freeink-sdk` is fetched from
**`git@github.com:rfordinal/freeink-sdk.git`**, our fork of
`Free-Ink/freeink-sdk`. The pinned commit did not change when the URL did, and
does not have to: a fork carries upstream's whole history, so every commit any
branch here pins is reachable in it.

## The trap: an existing checkout keeps the old URL

`git submodule update --init` reads the URL from `.git/config`, not from
`.gitmodules`, and `git submodule init` does **not** overwrite an entry that is
already there. So a checkout that has ever initialised this submodule keeps
fetching from `Free-Ink` and nothing says so. Run:

```
git submodule sync freeink-sdk
git -C freeink-sdk remote get-url origin      # must be rfordinal/freeink-sdk
```

Measured 2026-09-03: after the `.gitmodules` change alone, a fresh worktree
still cloned from `Free-Ink` and reported the old remote. Only `sync` moved it.

## Remotes inside the submodule

| remote | where |
|---|---|
| `origin` | `git@github.com:rfordinal/freeink-sdk.git` (our fork) |
| `upstream` | `https://github.com/Free-Ink/freeink-sdk.git` |

Same convention as this repo against CrossPoint: `origin` is ours, `upstream` is
theirs.

## Branches in the fork

- **`main`** mirrors upstream. Do not commit to it. A PR to upstream is branched
  from it, so it has to stay clean.
- **`explorink`** is upstream plus our patches. This is what the firmware pins.
  Today it is two commits on top of `e514a868` (upstream, 2026-07-28):

  - `94e19f73` -- the T5 S3 Pro EPD config no longer asserting the LoRa radio's
    chip select (`Free-Ink/freeink-sdk#73`, parent `docs/BUGS.md` BUG-037)
  - `55a49587` -- `readFileToStream` feeds the task watchdog

  The branch has never been rebased, so its base is still 2026-07-28 while
  upstream `main` has moved 219 commits past it.

`.gitmodules` says `branch = main` on every branch of this repo, deliberately.
That field only steers `git submodule update --remote`, which is not part of the
normal flow, and keeping it identical everywhere means `.gitmodules` never
conflicts when `develop` and a `release/*` branch merge. **What is used is the
pinned commit**, and that is allowed to differ per branch.

## Why a fork at all

`CLAUDE.md` used to say "freeink-sdk is upstream, so correct it here rather than
forking the SDK". That cost a workaround in our own tree twice:

- the frontlight PWM ceiling -- the vendor caps the PT4103B23F at about 1 kHz and
  the SDK board profile asks for 5 kHz, corrected in `src/main.cpp` instead
- the T5 S3 Pro EPD config handing the SX1262's chip select to LovyanGFX as a
  dummy pin, which killed the SD card (parent `docs/BUGS.md`, BUG-037)

Neither is a firmware bug and both were patched in the firmware because an SDK
fix had nowhere to be pushed: we have read access only, and our pinned base sat
199 commits behind upstream `main`. This repo already forks for that exact
reason, and so does the simulator.

**So an SDK-level defect now gets a branch in the fork and a PR upstream**, not
a workaround here. Keep the fork's `main` tracking upstream so the PR branch has
a clean base.

## Moving the pinned commit is its own decision

Upstream `main` is far ahead of what this repo builds against, and the gap
carries panel-driver work for the shipping devices -- a new X4 Pro display
driver, e-ink init and ghosting changes, UC8279C grayscale, a new X4C board,
deep-sleep panel parking. Bumping the pin is a separate task with its own
hardware pass on an X4 or X4 Pro. It does not ride along with a board bring-up
fix. See [`branching.md`](branching.md).

## Two ways the pin loses our patches, both seen on 2026-09-08

Our patches live only on `explorink`. Nothing checks that the commit a firmware
branch pins is on that branch, so the pin can walk off it silently. Both ways
happened, and on 2026-09-08 the result was that **no firmware branch carried
either of our two SDK fixes**.

**A commit that moves the gitlink without saying so.** `44fe2972` on
`release/lilygo-t5-s3-pro` (2026-09-07, *"feat(t5s3): the frontlight level is a
Settings row"*) moved the pin backwards from `55a49587` to `e514a868`. Its
17-line body does not mention the submodule. `e514a868` is the base `explorink`
forks from, so the move dropped exactly our two commits. Restored by
`c5ba73c3`.

**A bump pass that lands on an upstream commit `explorink` does not contain.**
The 2026-09-08 SDK pass moved `develop` from `e514a868` to `cb9167d5` -- a
measured, deliberate pass for the X4 Classic board profile (parent
`docs/PROGRESS.md`, "The SDK pointer is the separable part"). But `cb9167d5` is
on the mirror `main`, not on `explorink`, and `explorink` was never rebased onto
it. Verified, both answering NO:

```
git -C freeink-sdk merge-base --is-ancestor 94e19f73 cb9167d5
git -C freeink-sdk merge-base --is-ancestor 55a49587 cb9167d5
```

So `develop` took 217 upstream commits and gave back the LoRa chip-select fix
that BUG-037 confirmed on hardware 2026-09-03.

**Two rules follow.**

- **A bump pass rebases `explorink` onto the new base and pins the rebased
  tip**, never the mirror commit. The pass is not done while our patches are
  only on the old base.
- **A commit that moves the gitlink says so in its body**, with the old and new
  SHA and why. A gitlink is one line in a diff and `--stat` shows it as one
  changed file, so nothing else makes it visible in review.

**Verified on hardware, 2026-09-08.** LilyGo T5 S3 Pro (MAC
`7c:2c:67:8a:4c:b4`), env `t5s3pro`, pin back at `55a49587`, flashed over
`/dev/ttyACM0`. The device booted to Home and the map screen drew real tile
linework, which is an SD read -- the thing BUG-037's dummy chip select killed.
Free heap 185708 bytes of 305340 at idle. Archived as
`docs/firmware-builds/2026-09-08-t5s3pro-sdk-pin-089d1d42.bin` in the parent
repo.

Screen any pin against the fork before trusting it:

```
git -C freeink-sdk branch -r --contains $(git rev-parse HEAD:freeink-sdk)
```

`origin/explorink` in that output means the pin carries our patches. Only
`origin/main` means it does not.
