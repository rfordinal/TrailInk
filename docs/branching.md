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

- **Hardware-conditional work for that device forks from `release/<device>`.**
  Bring-up, a panel driver, a touch stack, a GNSS rail. **Everything else forks
  from `develop`**, which is most work — see "Where a branch forks from:
  `develop` by default" below. Use the normal worktree recipe (`CLAUDE.md`,
  "Every change goes in a worktree"), just change the base branch:

  ```
  git -C firmware/explorink worktree add ../../.worktrees/firmware/<topic> -b <topic> release/<device>
  ```

- **Merge finished feature branches into `release/<device>`**, same testing
  bar as any other merge (`CLAUDE.md`, "Never merge into a production branch
  untested. Ask.") — `release/<device>` is not exempt just because it is not
  `develop`.
- **When the branch is stable, all of it goes into `develop`** — that is what
  hands the work to every other device, and it is how the branch ends rather
  than becoming a parallel line. See "A stable device branch goes back into
  `develop`, whole" below. Still a separate ask with its own hardware test.
- **Sync the other direction too.** `develop` keeps moving while a device's
  release branch is being worked — core fixes, shared refactors, other
  devices' contributions. Merge `develop` into `release/<device>` **before
  forking any feature branch off it**, so the work starts on the current base.
  See "Sync the device branch before forking a feature off it" below, including
  the submodule pointer that merge will otherwise carry quietly.

## Why hardware-conditional work does not sit on `develop`

A single GNSS probe, touch driver and panel init are each small, but together
they are one long stretch where the board does not reliably boot. Nothing
about that should reach a session working on X4 firmware from `develop`. The
release branch is the holding area; `develop` sees the result.

That argument covers bring-up and nothing else. It is **not** a reason to put
ordinary features there — see "Where a branch forks from" below for the split,
and for what happens when the holding area quietly becomes the trunk.

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

## Where a branch forks from: `develop` by default

**Maintainer's decision, 2026-09-09.** Two kinds of work, two bases:

- **New functionality forks from `develop`.** This is the default and it is most
  work. A map feature, a settings row, a BLE command, a refactor: none of it is
  about one board, so none of it belongs on a board's branch.
- **Hardware-conditional work forks from `release/<device>`.** Bring-up, a panel
  driver, a touch stack, a GNSS rail — anything that needs *that* board to run
  at all, or that leaves it unreliably booting for a while.

The test is not "which board is on the desk", it is **"would this work exist if
that board did not"**. A feature that merely has to be *verified* on a
particular board is still `develop` work.

## A stable device branch goes back into `develop`, whole

**Same decision.** When a device branch is stable — everything on it works —
**all of it merges into `develop`**. That is the step that hands the work to
every other device: the shared half of a bring-up (a driver seam, a capability
query, a settings row) is useful to boards nobody was holding at the time, and
it reaches them only through `develop`.

So the device branch is a **holding area with an exit**, not a parallel line. It
exists for the stretch where the board is unreliable, and it ends when the board
is not.

This is a merge into a production branch: it needs a hardware pass and the
maintainer's go-ahead, same as any other
([`../../../CLAUDE.md`](../../../CLAUDE.md), "Never merge into a production
branch untested"). "Stable" is the maintainer's call, not a branch statistic.

**The failure mode this rules out** is what `release/lilygo-t5-s3-pro` had
become by 2026-09-08: created 2026-08-31, never promoted, 212 commits ahead of
`develop` and carrying 124 docs-only commits plus 23 code commits that no other
board could see. It had stopped being a holding area and become the de-facto
trunk, with `develop` as the branch nobody built on.

### The consequence for build environments

If new functionality forks from `develop`, then a board's env has to be **on**
`develop`, or the default base cannot be flashed to that board and the rule
collapses back into the thing that produced the cherry-picks. Moving
`[env:t5s3pro]` and `[env:x4pro]` down is therefore part of this model, not a
separate cleanup. T-289 in the parent repo.

## Sync the device branch before forking a feature off it

**Maintainer's decision, 2026-09-09.** A feature branch forked from
`release/<device>` must be forked from a base that already carries what
`develop` has. So the order is: merge `develop` into `release/<device>`, then
fork. Development happens on the current base, not on whatever the branch
happened to hold.

This replaces "periodically, whenever picking the device work back up after a
gap". A gap is not the trigger; forking is.

```
git -C firmware/explorink fetch origin
git -C <device worktree> log --oneline origin/develop ^HEAD    # what the sync brings
git -C <device worktree> merge origin/develop
```

**The sync does not need its own hardware pass.** The device branch is the
holding area for work that is allowed to be half-finished, and the feature's own
hardware pass then measures the feature against the synced base, which is the
combination that matters. Requiring a pass per sync is what makes people skip
syncing, and skipping syncing is what produced the cherry-picks.

**The sync will also move the `freeink-sdk` pin, and that is fine — but it
happens quietly.** Once `develop` and the device branch pin different SDK
commits and one contains the other, git decides it knows the answer: it stages
the newer one and prints a single `Note: Fast-forwarding submodule freeink-sdk`.
No conflict, nothing to confirm. Measured 2026-09-09: a sync of
`release/lilygo-t5-s3-pro` carries the pin from `55a49587` to `955b2530`,
**209 commits**, inside a routine merge whose only reported conflicts are in
documentation.

**That propagation is wanted.** A pin was moved deliberately somewhere, and a
sync is how the other branches get it. So this is not a thing to undo. What it
costs is a heavier pass, and the point is to notice you now owe it:

- **A row in [`freeink-sdk-pins.md`](freeink-sdk-pins.md)**, with what moved and
  why.
- **A full hardware pass on that branch, not a spot check.** The SDK is the
  panel driver, the SD card and the input layer, so: boot, a map frame (SD
  read), `MKCOL` + `PUT` (SD write), and a large WebDAV GET
  (`readFileToStream`). That set is what caught real defects in this subsystem
  twice.
- **The commit body says it**, with both SHAs.

`scripts/sdk_pin_check.py` answers it and the hooks call it for you:

```
python3 scripts/sdk_pin_check.py --from ORIG_HEAD    # after a merge
python3 scripts/sdk_pin_check.py                     # HEAD against its parent
```

It reports the direction, the span, and whether the patches on
`origin/explorink` are in the new pin; exit 1 means it moved, 2 means a patch of
ours is not in the new pin.

**The hooks are off on purpose, and turning them on is not yet safe.**
`.githooks/post-merge` and `post-commit` exist, and git ignores them until it is
told where they are:

```
git -C firmware/explorink config core.hooksPath .githooks     # NOT YET, see below
```

Nobody had that set as of 2026-09-09, which is why the `pre-commit`
clang-format hook had been running for no one and why 35 files had drifted out
of format unnoticed.

**Why the wait.** `core.hooksPath` is one setting for the whole repo, but a
relative path resolves **per working tree** — so every worktree runs the hook
from *its own branch*, not from `develop`. When this was switched on for a few
minutes on 2026-09-09, 21 of 23 worktrees still carried the pre-fix
`pre-commit`, the one that reformats all 544 tracked C/C++ files instead of the
modified ones. Several of those are branches with work in progress. Switching it
on would have handed each of those sessions 35 rewritten files, in their own
working tree, on a commit that touched none of them.

**The gate**: turn it on once the branches people are working on carry
`./bin/clang-format-fix -g` (fixed on `develop` in `c7dcf135`). They pick it up
on their next sync from `develop`, which the rule at the top of this section
requires anyway. Check before flipping it:

```
for d in $(git -C firmware/explorink worktree list --porcelain \
            | sed -n 's/^worktree //p'); do
  grep -q 'clang-format-fix -g' "$d/.githooks/pre-commit" 2>/dev/null \
    || echo "old hook: $d"
done
```

Until then, run the pin check by hand — it is the same script the hook calls.

**CI cannot cover this, and it is worth knowing why.** `.github/workflows/ci.yml`
triggers on `push: branches: [master]` and on pull requests. This repo's trunk is
`develop` and merges happen locally without PRs, so **CI never runs on our
pushes at all**. The hook plus the log is the mechanism; there is no server-side
net behind it.

Cost of the two syncs pending as this was written: `release/lilygo-t5-s3-pro` is
53 commits behind `develop` and 212 ahead, and its pin differs;
`release/xteink-x4-pro` is 6 behind, 1 ahead, same pin.

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

