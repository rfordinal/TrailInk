# Per-device release branches

Decided 2026-08-31, when LilyGo T5 S3 Pro bring-up started (GNSS, touch, a new
panel driver, all at once). The risk: `develop` carries the code every device
builds on, and device bring-up produces questionable, half-working code for
long stretches. That code should not sit on `develop` while it is being found
out.

## The model

One branch per device, `release/<device-slug>`, forked from `develop`. The
slug matches the device's doc: `docs/devices/<slug>.md` in the parent repo, so
`release/lilygo-t5-s3-pro` for [`../../docs/devices/lilygo-t5-s3-pro.md`](../../docs/devices/lilygo-t5-s3-pro.md).

- **Bring-up and feature work for that device forks from `release/<device>`,
  not from `develop`.** Use the normal worktree recipe (`CLAUDE.md`, "Every
  change goes in a worktree"), just change the base branch:

  ```
  git -C firmware/explorink worktree add ../../.worktrees/firmware/<topic> -b <topic> release/<device>
  ```

- **Merge finished feature branches into `release/<device>`**, same testing
  bar as any other merge (`CLAUDE.md`, "Never merge into a production branch
  untested. Ask.") — `release/<device>` is not exempt just because it is not
  `develop`.
- **Promote `release/<device>` into `develop` only when it is tip-top** — the
  device's bring-up is stable, not mid-experiment. This is a second, separate
  ask: merging into `develop` still needs a hardware test and the maintainer's
  go-ahead, same as any other merge into a production branch.
- **Sync the other direction too.** `develop` keeps moving while a device's
  release branch is being worked — core fixes, shared refactors, other
  devices' contributions. Periodically merge `develop` into `release/<device>`
  so the device branch does not rot behind it. Do this whenever picking the
  device work back up after a gap, not on a fixed schedule.

## Why not just feature branches off `develop`

A single GNSS probe, touch driver and panel init are each small, but together
they are one long stretch where the board does not reliably boot. Nothing
about that should reach a session working on X4 firmware from `develop`. The
release branch is the holding area; `develop` only sees the result.

## Borrowing a release branch for verification only

A `develop`-based, device-agnostic branch sometimes needs a hardware pass
when no X4/X4 Pro is on hand, and a `release/<device>` board is the only
thing plugged in. Cherry-pick the commits onto a throwaway branch off
`release/<device>`, build, flash, confirm -- then merge the **original**
`develop`-based branch into `develop` (never the cherry-picked one anywhere).
The verification branch is discarded once its job is done; it was never
meant to merge.

Confirmed 2026-09-06/07: `pins-on-sync` (pin commands + live burst
geography on the Sync screen, `firmware/explorink`) verified this way on a
T5 S3 Pro with no X4 available, then merged straight into `develop` as
`adf6faa1` -- the T5S3Pro branch itself was never merged anywhere.

## Existing branches

- `release/lilygo-t5-s3-pro` — created 2026-08-31 from `develop`.
- `release/xteink-x4-pro` — created 2026-09-09 from `develop`, carrying
  `[env:x4pro]`. Nothing has run on the board yet;
  [`xteink-x4-pro-bringup.md`](xteink-x4-pro-bringup.md) says what the first
  session has to settle.

**The X3 needs no branch and no env.** It is an ESP32-C3 and `[env:default]`
already builds one binary for the X4 and the X3 together
(`platformio.ini`, `FREEINK_DEVICE_X4` and `FREEINK_DEVICE_X3` side by side);
the framebuffer is sized to the largest selected panel (`BoardConfig.h`,
`MAX_FRAMEBUFFER_BYTES`). A device gets its own branch when its bring-up would
leave a board unreliably booting, and a device that already has a working
binary is not that.

## Fork from `origin/release/<device>`, never from the local ref

The local `release/<device>` branch is usually checked out in some other
session's worktree, pinned wherever that session left it. On 2026-09-07 the
local `release/lilygo-t5-s3-pro` was **136 commits behind origin and 0 ahead**,
and a branch taken from it built cleanly and was minutes from being flashed
against a stale base.

What caught it was the pre-flash check (`../../CLAUDE.md`, "Only flash a
rebased branch") -- run against `origin/release/<device>` rather than against
`develop`, which is the wrong target for a device branch:

```
git -C firmware/explorink fetch origin
git -C <worktree> log --oneline origin/release/<device> ^HEAD
```

Empty means current. Anything listed means rebase, rebuild, then flash.

The same stale ref makes `git branch -d` lie. It compares against the local
branch and answers `not fully merged` for a branch that is fully merged into
origin. Check the real question, then force:

```
git -C firmware/explorink merge-base --is-ancestor <branch> origin/release/<device>
git -C firmware/explorink branch -D <branch>
```

## Carrying one change onto a device branch is a cherry-pick, not a merge

A `git merge` of a `develop`-based branch drags every `develop` commit the
device branch has not taken -- 54 of them on 2026-09-07, against a device
branch that was itself 93 commits ahead. The hardware pass that follows then
measures all of it instead of measuring the change.

Cherry-pick the commits instead, resolve the conflicts against what the device
branch actually has, and leave the `develop` sync as its own decision with its
own hardware pass. The 2026-09-07 case is a worked example: the release branch
had no `STR_TOUCH_MODE` row at the time, so the conflict resolution kept the
device branch's shape rather than importing a `develop` feature sideways.
