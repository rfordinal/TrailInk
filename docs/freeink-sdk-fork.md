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
  Today it is one commit: the T5 S3 Pro EPD config no longer asserting the LoRa
  radio's chip select (`Free-Ink/freeink-sdk#73`).

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

## Rule, 2026-09-08: the fork stays a mirror and the bump is its own pass

**Maintainer's words:** track upstream closely and carry only small changes of our
own. Two consequences, and the second one is new:

- **Moving the SDK pointer is its own pass**, never folded into a CrossPoint sync.
  The other track has its own file and its own rules
  ([`upstream-crosspoint.md`](upstream-crosspoint.md)). The reason is diagnostic:
  the SDK is a library whose API only grows, so a bump either builds or does not,
  while a CrossPoint sync is a judgement call per commit. Bundled, a red build says
  nothing about which half broke.
- **A local SDK commit is a cost, not a feature.** The one patch this fork carried
  on `explorink` (the T5 S3 Pro EPD config asserting the LoRa chip select) went
  upstream as PR #73 and was **merged 2026-09-03**. So the right end state is what
  we have: nothing of ours in the SDK line we pin.

Measured 2026-09-08 through the GitHub compare API:

| | Value |
|---|---|
| Our `main` | `24003795`, 2026-09-02, **zero commits of its own** -- a plain ancestor of upstream `main` |
| Upstream `main` ahead of our `main` | 18 commits |
| `explorink` branch | one patch, now redundant: PR #73 is upstream |

### The pin has to sit on a branch of our own remote

**Trap found 2026-09-08.** The pin `develop` now carries, `cb9167d5`, is **9 commits
ahead of our fork's `main`**, so it is on no branch of `rfordinal/freeink-sdk`. It
resolves only because GitHub keeps a fork network in one object store -- a property
of the host, not of our repository. If that ever stops holding, a fresh clone fails
`git submodule update` with a commit-not-found error that names nothing useful.

The fix is one push, and it is the rule anyway: fast-forward our `main` to upstream
`main`, then pin a commit that sits on it. Nothing can be lost -- our `main` has no
commits of its own.

```
git -C freeink-sdk fetch upstream
git -C freeink-sdk push origin upstream/main:main     # fast-forward, ask first
```

**Not done: that push needs the maintainer's word.** Until then the pin works and
this paragraph is the record of why it is not clean.

### The 2026-09-08 bump, and what it cost

`e514a868` (2026-07-28) to `cb9167d5` (2026-09-04), 217 commits. What it is *for*:
the **X4 Classic board profile** (`Board::XteinkX4Classic`, SDK
`docs/xteink-x4c-support.md`, full pinout), which is what an S3 Xteink env needs.
It also brings our own PR #73 in from upstream, so the `explorink` patch stops being
load-bearing.

Evidence, all from a laptop, none from a device:

- **No header deleted, none renamed** (`git diff --name-status e514a868 cb9167d5 -- '*.h'`):
  68 headers change, additive, +8548 lines, mostly FreeInkUI list, tile-grid and
  sheet components we do not link. `BoardConfig.h` gains 780 lines.
- **All six envs build**: `default`, `gh_release`, `gh_release_rc`, `slim` (C3),
  `sticky` (S3), `simulator` (host).
- **Host tests 437 of 437**, 2.73 s.
- **Cost, measured by building the S3 env against each SDK commit in turn:**
  RAM +296 B, flash +21,920 B (+21 kB) for 217 commits.

| Env | Chip | RAM | Flash |
|---|---|---|---|
| `default` | ESP32-C3 | 18.0 %, 58,924 B | 61.5 %, 4,033,587 B |
| `gh_release` | ESP32-C3 | 16.1 %, 52,812 B | 57.5 %, 3,768,871 B |
| `gh_release_rc` | ESP32-C3 | 16.1 %, 52,812 B | 57.5 %, 3,768,867 B |
| `slim` | ESP32-C3 | 16.1 %, 52,788 B | 56.8 %, 3,720,817 B |
| `sticky` | ESP32-S3 | 19.2 %, 62,844 B | 55.0 %, 3,606,343 B |

**None of that says the firmware runs.** A bump is trusted once a C3 device boots,
draws a map and holds a BLE link on it.

### Checklist for the next bump

1. `git -C freeink-sdk fetch upstream`, then read what changed under
   `libs/hardware/BoardConfig`, `libs/display` and `docs/`.
2. Confirm no header was deleted or renamed.
3. Pin a commit that is on our fork's `main`; fast-forward `main` first if it is not.
4. Build every env, run the host tests, record RAM and flash for one C3 and one S3
   env here.
5. Say in the commit message what the bump is **for**. A pointer move with no reason
   cannot be reverted with confidence.
