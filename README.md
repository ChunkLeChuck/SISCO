# SISCO

**Streaming Issue Solver: Crash Override.** An .asi plugin for Grand Theft Auto IV.

Download it from [Nexus Mods](https://www.nexusmods.com/gta4/mods/1453). This repository is its source.

GTA IV is notorious for its texture streaming problems. Those of us who have been modding the game for years have all seen what happens as soon as we want to install just a few bigger mods.

SISCO raises the limits to better accommodate bigger mod installs, and fixes the things that break when you do.

It supports **1.0.8.0** and the **Complete Edition (1.2.0.59)**, and it requires **DXVK 3.x**, which is in the
download. The game's own Direct3D 9 renderer is not supported. 

## Installing

Put `SISCO.asi` and `SISCO.ini` in your `plugins` folder, and `vulkan.dll` in the game folder next to `GTAIV.exe`,
replacing the one FusionFix put there. If you run FusionFix you already have an ASI loader.

`vulkan.dll` is DXVK 3.1.1, unmodified, under the file name FusionFix's D3D9 proxy loads DXVK by. It is the
version SISCO is tested against; an older DXVK is not refused, and the log records the version and file it found.
FusionFix's `d3d9.cfg` in the game folder must say `API = 1` under `[MAIN]`, which it already does if the game was
running DXVK before. If you run DXVK as your own `d3d9.dll` instead of through FusionFix, rename the `vulkan.dll`
from the download to `d3d9.dll` and use it in place of yours; it is the same file.

To uninstall, delete `SISCO.asi` and `SISCO.ini`; `vulkan.dll` is only a newer DXVK and can stay. SISCO never writes
to a game file, and every change it makes lives in memory for that one run.

It writes `plugins\SISCO.log`: a short, plain record of what it changed and what it decided. That file is the first
thing to look at if something seems wrong, and the thing to attach to a bug report.

## If something goes wrong

`SISCO.ini` has four switches and nothing else. They are all on, which is how the mod is meant to run, and you
should only turn one off to find out which part of the mod is causing a problem.

| | what it turns off |
|---|---|
| `Enabled=0` | everything. If the problem is still there, it is not SISCO (the `vulkan.dll` from the download stays in place; put your old one back to rule that out too) |
| `Fixes=0` | the three bug fixes |
| `Limits=0` | the bigger pools and the streaming arena |
| `Budget=0` | the streaming budget, the traffic budgets and the brake |

Only the number `0` turns something off. A typo, or a word like `off`, leaves that part on, so you cannot break it
by mistyping. The log records which switches were on every run, so send it with any report.

Turning `Limits` or `Fixes` off also stops the budget being raised, because a raised budget is only safe behind
them. Every switch moves the game back towards how it runs without the mod.

**VSync and frame-rate caps, and slow loading.** A raised budget needs the `-managed` launch option, and with it
the world build after a load waits on every present: measured on a clean install, VSync alone (120 Hz) turned a
2-second world build into 27 seconds, a 30 fps cap into 61, a 60 fps cap into 30, while the same build without
`-managed` took 2 seconds under the 30 fps cap. So from the moment the player appears after a load SISCO presents
without waiting for VSync until the world has built (and for at least 15 seconds), then hands VSync back; the log's
`present` lines say so and count it. Frame-rate limiters are not covered by that, and neither is a VSync forced in
DXVK's own config: SISCO reads `dxvk.conf` (and `DXVK_CONFIG_FILE`, `DXVK_CONFIG`) the way DXVK does, and a
`maxFrameRate` cap or a `d3d9.presentInterval` there makes it leave `-managed` unset and the budget at the game's
own, saying so. It cannot see RTSS's figure or the driver's cap, so if you use one of those, lift it while loading
or set `Budget=0`. The log's `load` line times every build (on 1.0.8.0 with its frame rate), and when a build took
over 20 seconds with the option in effect, SISCO writes `plugins\SISCO-SLOW-LOAD.txt`, a plain note of what it
could see and what to do, and says the same on the log's `slowload` line.

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

SISCO tells DXVK from the game's own renderer by asking the Direct3D object itself, and reads DXVK's version from
its module; the log's `direct3d` line says what it found. At sizing it also reads every raised value back from the
game, so a value another mod set back after SISCO loaded is seen and counted as a refused raise.

**DXVK is required.** SISCO is built and tested for DXVK 3.x, the `vulkan.dll` in the download. GTA IV's own
renderer holds a copy of everything in system memory, so a raised budget eats the 32-bit address space instead of
helping, and nothing about SISCO on Direct3D 9 is tested or supported: on it, SISCO leaves the budgets alone and says
so in the log.

## How it was tested

I tested it by making a texture pack out of the game's own textures, every one of them doubled in size, so the game was
under the same load a big HD pack puts it under. Then drove the same route from the same save, over and over. The drives
stayed within a few metres of each other, so every run could be fairly compared to the last.

The full pack was 3,989 texture files holding 54,363 textures, about 7 GB, and I could run it at 12.5% (1 GB), 25%
(2.1 GB), 50% (3.7 GB) or 100% (7.2 GB) to turn the pressure up step by step. Each smaller size is part of the bigger
one, so the steps are comparable.

While driving I counted how often the game failed to show something, per kilometre, and how many different cars and
people were on the street.

79 runs in total. Most of them 6 to 15 minutes, and the longest 21 minutes to see if the fixes hold up.

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
from the world first; the car and ped budgets only follow once the world budget has fallen below 2000 MB. Measured:
the city thins slightly instead of the game falling over.

## Alongside other mods

- **FusionFix**: works with it. When its ExtraStreamingMemory option is on, SISCO assumes FusionFix's larger memory
  credit when sizing; that combination has not been measured.
- **DXVK**: required. The `vulkan.dll` in the download is 3.1.1, the version SISCO is tested against; the log
  records the version it found, and a frame cap or a forced VSync in DXVK's own `dxvk.conf` holds the budget (see above).
- **Anything that sets the same values**: a larger value another mod already set always stands. SISCO only raises.
  A mod that sets a value back after SISCO loaded is seen at sizing, when SISCO reads every raised value back from
  the game: for the pools and the arena that raise then counts as refused and the budget stays the game's own; for
  the VehicleStruct pool the car budget is held at 120 MB instead. The log names the site either way.
  IVTweaker's MaxVehicleStruct hooks the pool's constructor rather than the size, so SISCO reads the count the pool
  was actually built with.
- **Every site is verified before it is written.** SISCO compares the exact bytes it expects at each of the
  twenty-one places it patches, and if one does not match, that feature stays off and the log says which and why. It never
  half-installs: the queue fix in particular goes in completely or not at all, and rolls back if a write fails.
  The two Direct3D vtable slots it writes (CreateDevice and the swap chain's Present) are DXVK's, not the game's:
  each is read and chained, never compared against expected bytes, and written once.

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
