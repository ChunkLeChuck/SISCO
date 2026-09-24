# Changelog

## 1.0.3 (2026-09-24)

- Engine sounds: 48 engine-sound slots instead of the game's 15. Each different car engine near you needs one, a car
  keeps it until the car is gone, and past the last one a car plays no engine sound, which the raised car budget made
  easy to reach. This is revd's fix, built in without its extras: the audio heap grows by exactly what the slots need
  (152 MiB instead of 126, where revd uses 192), SISCO writes its own `pc\audio\config\waveslots_sisco.xml` from the
  game's file (FusionFix's `update` copy when it is there) and points the game at it, and no game file is edited. If
  revd or another audio mod already changed the slots, SISCO leaves them to it.
- The ped budget is 180 MB instead of 200. The other 20 MB goes to the world: car and ped models are paid from the
  same streaming budget.
- The address-space brake's last-resort line is 192 MB again, as it was designed and tested: with less free address
  space than that, it lowers the budget every 10 seconds. 1.0.2 lowered the line to 128 MB by mistake.
- A game started while an earlier one was still closing (an error box or a crash dialog still open) ran without
  SISCO and said nothing. Each game now gets its own.
- FusionFix installed in `scripts\` is found too, so an audio mod in its `update` folder keeps its slots; and a line or
  a slot commented out in `driverSettings.xml` or a slot file is ignored, as the game ignores it.
- To uninstall, delete `pc\audio\config\waveslots_sisco.xml` as well as `SISCO.asi` and `SISCO.ini`.
- Loading is fast with VSync and frame limiters on. Under `-managed`, which the raised budget needs, the game's
  loading waited on every present, so VSync or any frame cap made it crawl. SISCO now skips most presents while a
  loading screen is up and while the world builds after it (one still goes through every 33 ms on a loading screen,
  every 100 ms while the world builds). Measured with VSync on: 82 seconds from launch to the world built before,
  10 now; under a 30 fps cap: 90 before, 17 now.
- A frame cap no longer keeps the budget at the game's own. 1.0.1 and 1.0.2 held it under a cap they could see.
- SISCO no longer forces presents out without VSync (1.0.1 and 1.0.2 did while loading): that could stall a loading
  screen whose window was not in the foreground until it was clicked.
- SISCO-SLOW-LOAD.txt is no longer written, and one left by 1.0.1 or 1.0.2 is deleted: its advice was to lift a
  frame limiter, which no longer slows loading. SISCO no longer reads dxvk.conf either, and the load line no longer
  gives a frame rate.
- The download extracts correctly on Linux and the Steam Deck: the zip stored its folders with backslashes, which some
  Linux tools turn into a file name instead of the plugins folder.
- The log says when each loading screen went up and how long it was up before the player appeared. The size line
  says the pool is managed.

## 1.0.2 (2026-09-24)

- Steam Deck and Proton: SISCO now knows it is running under Wine. Proton loads its own DXVK ahead of the game
  folder's vulkan.dll, and the log says so instead of asking for 3.x. The 32-bit address space is laid out
  differently there, and 1.0.1's brake read it as nearly full a second after sizing, taking the budget back to the
  game's own with the car and ped budgets. Now the layout is measured at sizing: where its largest free block is under
  1 GB the budget is raised to at most 2000 MB, a desktop Linux machine whose layout reads like Windows is sized as
  Windows is, the brake's lines follow the layout either way, and the log's `size` and `brake` lines record it.
- Render targets under 24 MB are never sized against. On a Deck the first build stage is 5 MB and held past the
  45-second settle on a slow load, so 1.0.1 sized the budget in the middle of the load and judged the build finished.
  No playable resolution has a finished set that small.
- The slow-load note names the Deck's own frame limit, MangoHud's and the driver's as the usual causes under Wine.

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
