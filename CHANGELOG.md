# Changelog

## 1.0.1 (2026-09-24)

**The download**

- Three files now. `SISCO.asi` and `SISCO.ini` go in `plugins`; `vulkan.dll` goes in the game folder next to
  `GTAIV.exe`, replacing the one FusionFix put there. `vulkan.dll` is DXVK 3.1.1, unmodified.
- DXVK 3.x is required. Running SISCO on the game's own Direct3D 9 renderer is not supported: the budget and the car
  and ped budgets are left alone there, and the log says so.

**Loading**

- Slow loading with SISCO has a measured cause and a fix for the common case. A raised budget needs the `-managed`
  launch option, and with it the world build after a load waits on every present: VSync alone turned a 2-second
  build into 27 seconds, a 30 fps cap into 61. SISCO now presents without waiting for VSync from the moment the
  player appears until the world has built, then hands VSync back. VSync no longer slows loading.
- Frame-rate limiters still do (RTSS, the driver's panel, DXVK's own), and so does a VSync forced in `dxvk.conf`.
  SISCO reads DXVK's config the way DXVK does; under a cap or a forced VSync found there it leaves `-managed` unset
  and the budget at the game's own, and says why.
- Every load is timed in the log (the `load` line), on 1.0.8.0 with its frame rate. A build over 20 seconds with
  `-managed` in effect writes `plugins\SISCO-SLOW-LOAD.txt`, a plain note of what SISCO could see and what to do.
- The budget is no longer sized in the middle of a save load. 1.0.0 could size it while the game's render targets
  were still being built, which made the rest of the load much slower. SISCO now waits for the targets to hold for
  ten seconds and never sizes against their first build stage.

**Sizing**

- The streaming arena is raised on every install. 1.0.0 guessed from a config file whether to raise it, and a wrong
  guess left a raised budget loading through the game's own 160 MiB arena, which made the world disappear for one
  player.
- Below 1080p the budget is sized too. A minimum introduced during testing would have stopped it on 1366x768 and 720p.
- The sizing starts from the budget the game actually runs on: 800 MB at High under FusionFix, 550 without it, on
  1.0.8.0 and the Complete Edition alike. The brake can no longer take the budget below that.
- Your own launch options are respected. `-availablevidmem` bounds the sizing; `-memrestrict` leaves the budget
  alone, because the game does not read the budget table then; `-unmanaged` or `-nominimize` keeps the budget at the
  game's own, because a raised budget behind an unmanaged pool is not safe. The log says which applied.
- The car budget is held at 120 MB where the VehicleStruct pool was not raised (another mod sets it), because a
  larger car budget pays for models the pool cannot hold.

**Safety**

- SISCO reads back what the game actually built (the pools, the arena, the VehicleStruct count) when it sizes, and
  refuses the raised budget if another mod undid a raise after SISCO loaded. The log names the site.
- The raised budget goes in only behind everything it needs: the two pools, the release-queue fix, the link-pool
  lock fix and the arena. If any of them was refused, the budget stays the game's own and the log says which.

**The log and the settings**

- New lines: the DXVK version and where it was read from, what DXVK's config says (`dxvkconf`), the moment the
  player appears after a load (`world`), how long the world took to build (`load`), the presents that went out
  without VSync (`present`), your view distance, detail distance and car density (`display`), the pools as the game
  built them (`built`), and every decision about the budget (`size`).
- `SISCO.ini`: four switches (Enabled, Fixes, Limits, Budget), all on. Turn one off only to find out which part of
  the mod is causing a problem.

## 1.0.0 (2026-09-23)

First release, for GTA IV 1.0.8.0 and the Complete Edition 1.2.0.59.

- Fixes three things a raised streaming budget runs into: a release queue with no bounds check, a spinlock the
  game's own eviction takes twice (the freeze), and a link pool that runs dry.
- Raises the drawable-slot pool to 32,768, the link pool to 65,536, the VehicleStruct pool to 100 and the streaming
  arena to 400 MiB.
- Sizes the streaming budget to the card and the resolution, raises the car and ped budgets to 200 MB each, and
  keeps the 32-bit address space safe with a brake that takes from the world budget only.
