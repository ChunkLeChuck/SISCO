# SISCO

**Streaming Issue Solver: Crash Override.** An .asi plugin for Grand Theft Auto IV.

Download it from [Nexus Mods](https://www.nexusmods.com/gta4/mods/1453). This repository is its source.

GTA IV is notorious for its texture streaming problems. Those of us who have been modding the game for years have all seen what happens as soon as we want to install just a few bigger mods.

SISCO raises the limits to better accommodate bigger mod installs, and fixes the things that break when you do.

It supports **1.0.8.0** and the **Complete Edition (1.2.0.59)**. 

## Installing

Put `SISCO.asi` in your `plugins` folder. If you run FusionFix you already have an ASI loader.

To uninstall, delete it. It never writes to a game file, and every change it makes lives in memory for that one run.

It writes `plugins\SISCO.log`: a short, plain record of what it changed and what it decided. That file is the first
thing to look at if something seems wrong, and the thing to attach to a bug report.

## What it changes

Every one of these is a number the game already has. 

| | the game | SISCO | why |
|---|---|---|---|
| streaming budget | 300 to 800 MB | up to 4000 MB, sized to your card | the reason the rest of this exists |
| car models | 40 MB | 200 MB | measured: 7 different cars in traffic becomes 22 |
| ped models | 50 MB | 200 MB | measured: 4 different people on the street becomes 7 |
| streaming arena | 160 MiB | 400 MiB | the workspace the streamer loads into |
| drawable slots | 10,000 | 32,768 | when this pool fills, the game deadlocks |
| list entries | 13,000 | 65,536 | when this pool runs dry, the game crashes |
| VehicleStruct slots | 50 | 100 | more distinct cars need more of them |

**Without DXVK**, the last four still apply and so do the three fixes, but the budget, the arena and the traffic
budgets are left exactly as the game set them. GTA IV's own renderer cannot reach most of a modern card's memory, so
there is nothing to raise the budget into. That path has not been measured: everything here was worked out and run
under DXVK.

## How it was tested

I tested it by making a texture pack out of the game's own textures, every one of them doubled in size, so the game was
under the same load a big HD pack puts it under. Then drove the same route from the same save, over and over. The drives
stayed within a few metres of each other, so every run could be fairly compared to the last.

The full pack was 3,989 texture files holding 54,363 textures, about 7 GB, and I could run it at 12.5% (1 GB), 25%
(2.1 GB), 50% (3.7 GB) or 100% (7.2 GB) to turn the pressure up step by step. Each smaller size is part of the bigger
one, so the steps are comparable.

While driving I counted how often the game failed to show something, per kilometre, and how many different cars and
people were on the street.

79 runs in total. Most of them 6 to 15 minutes, and the longest 25 minutes to see if the fixes hold up.

## The three fixes

These are bugs in the game, not in the mod. They are out of reach at the stock limits and easy to hit above them.

- **A release queue with no bounds check.** The game keeps up to 65,536 pending releases and never tests whether that
  list is full before adding to it. Quitting a well-filled game was measured at 64,673 and 66,133 entries. SISCO holds
  the overflow in a list of its own and feeds it back on the next drain, so nothing is released early and nothing is
  written past the array.
- **The freeze.** When memory runs short the game evicts something to make room, and the eviction takes a spinlock its
  own caller already holds, so it waits for itself forever. SISCO makes that one lock recursive for the thread that
  owns it. Measured: 2.2 million nested takes with no freeze, where the same test without it froze on the first one.
- **A startup crash.** A table's size is published before the table is allocated, so anything that reads it in that
  instant gets a valid size and a null pointer. SISCO replaces the initialiser with one that publishes them in the
  other order.

## While you play

GTA IV is 32-bit, so there is a hard ceiling on address space that no graphics card can raise. Once a second SISCO
looks at how much room is left, and if it is running out it eases the budget back down a step at a time, taking it
from the world and never from the traffic. Measured: the city thins slightly instead of the game falling over.

## Alongside other mods

- **FusionFix**: works with it. SISCO notices when its ExtraStreamingMemory option is on and sizes accordingly.
- **Anything that sets the same values**: a larger value another mod already set always stands. SISCO only raises.
- **Every site is verified before it is written.** SISCO compares the exact bytes it expects at each of the twenty
  places it patches, and if one does not match, that feature stays off and the log says which and why. It never
  half-installs: the queue fix in particular goes in completely or not at all, and rolls back if a write fails.

## Building

Needs Visual Studio's C++ desktop workload (any edition; the compiler is found with `vswhere`, not assumed) and
Python 3 for the checks.

```
.\build.ps1
```

That builds the .asi, builds and runs its tests, loads the real .asi into a process that is not the game to prove it
stays passive, checks every address and every expected byte against a copy of the game, and refuses to deploy if any
of it fails.

The site check needs a copy of the game to check against. Give it one with `-Game1080` / `-GameCE`, or put the paths in
a `games.txt` beside the script (`name = path`, one per line). If it has none it says so in yellow rather than passing
quietly. The Complete Edition ships its first megabyte of code encrypted, so its check reads a copy recovered from a
runtime dump rather than the executable as installed; `docs/CE-SITES.md` explains that.

## Layout

```
core/sis_core.h        everything the mod does: the addresses, the patches, the sizing, the brake
core/sis_core_tests.h  its tests, compiled into the release's own build
SIS/SIS.cpp            DllMain, the log, the once-a-second worker
SIS/sis_test.cpp       the test runner
SIS/sis_load.cpp       loads the built .asi outside the game and checks it changes nothing
tools/                 the site checker, the instrument check, and a small PE reader they share
docs/                  how each address was found, and what was measured
```

## Licence

MIT. See [LICENSE](LICENSE).
