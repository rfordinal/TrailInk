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

## Verifying a `develop`-based branch on a device branch's board

**The old answer was to cherry-pick onto a throwaway branch. That is now
forbidden** -- see "No cherry-pick between our own branches" below.

The reason a cherry-pick was needed at all is one line of `platformio.ini`: the
device's env exists only on `release/<device>`, so a `develop`-based branch
cannot be flashed to that board. **With the env on `develop`, there is nothing
to carry**: build the branch itself, flash it, confirm, merge into `develop`.

Moving the envs down is therefore a prerequisite for this rule rather than an
option, and it is the open half of T-289 in the parent repo. Until that is done
there is no supported way to verify a `develop`-based branch on a board whose
env lives elsewhere -- say so and stop, rather than reaching for the banned tool.

History: `pins-on-sync` was verified the old way on 2026-09-06/07 on a T5 S3 Pro
with no X4 available, then merged into `develop` as `adf6faa1`. That is how it
was done, not how it is done.

## Existing branches

- `release/lilygo-t5-s3-pro` — created 2026-08-31 from `develop`.

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

## No cherry-pick between our own branches

**Maintainer's decision, 2026-09-09.** A change reaches another branch of ours
by **merge**. `git cherry-pick` is not used to move our own work, and this
section used to say the opposite.

This does not cover taking commits out of a foreign upstream. A CrossPoint
review take is selecting from someone else's history, not moving our own work,
and it keeps its own procedure
([`upstream-crosspoint.md`](upstream-crosspoint.md)).

### What the old rule said, and why it was replaced

It said a `git merge` of a `develop`-based branch drags every `develop` commit
the device branch has not taken -- 54 of them on 2026-09-07 -- so the hardware
pass that follows measures all of it instead of measuring the change. That
observation is true. The conclusion was wrong, because the drag is a symptom of
the device branch being allowed to fall behind, and cherry-picking makes the
drift permanent instead of fixing it.

Measured on 2026-09-09, on this repo:

- **Twin branches.** Four pairs of them: `cmd-buttons` / `cmd-buttons-t5s3`,
  `diag/boot-reason` / `-t5s3`, `feat/map-popup-size-classes` / `-t5s3`,
  `touch-lock-flag` / `-t5s3`. Three of the four twins were merged into the
  release branch despite this doc calling them throwaway.
- **The same file written twice.** `docs/settings-menu.md` existed on both
  branches with an **identical commit subject**, a different SHA and 134 lines
  of divergence. Git cannot see those as the same patch, so the next sync hits
  it as an add/add conflict.
- **Seven conflicts** in the pending `develop` -> release sync, five of them
  produced by the cherry-picking itself, and every one of them resolved twice --
  once on each branch.

So: carry a change by merging, which means **the target branch has to be kept
current** rather than left to drift. A sync that drags 54 commits is a sync that
was overdue, not a reason to avoid syncing.

