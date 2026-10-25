# Changelog

## 1.0.5 (2026-10-25)

- The log is shorter: one line per thing with its figures, two lines per load instead of four, the engine-sound peak
  at most once a minute, and nothing written where all is as expected (the read-back of the raised pools, the
  Direct3D hook). The size line gives the game's own budget and car and ped budgets beside SISCO's, as every other
  raise already did (`budget 800 -> 4000 MB`, `car 40 -> 200 MB, ped 50 -> 180 MB`).
- SISCO recognises the game by its code instead of its file header. A GTAIV.exe with 1.0.8.0's or 1.2.0.59's code
  behind a changed header (one that starts without the launcher) was refused as an unknown game; it works now, and
  the log's first line names the header when it is not the stock one. Other builds of the game are still refused,
  and the log now names the two supported versions by number (1.0.8.0 and 1.2.0.59): "not the Complete Edition" read
  as a fault to a player on 1.2.0.43, which is a Complete Edition too.
- On the Complete Edition SISCO now tells the game how much video memory the card has, as it always did on 1.0.8.0
  (the `-availablevidmem` launch option; your own value stands). It was left to the game there, and a player's game
  believed a 16 GB card had 2048 MB: the game keeps the world under that figure whatever the budget says, so the
  raised car and ped budgets took room from textures and LODs, and Budget=1 looked worse than Budget=0. The figure
  SISCO gives is the one FusionFix already gives where the game's own answer is right (the card's budget, at most
  half the RAM, never under 2048 MB), so nothing changes on an install that worked. Not on Linux: there the game's
  own figure stands.
- The budget, and the car and ped budgets with it, are never sized past the video memory the game believes it has,
  less the render targets, on either build. If the game does not take SISCO's figure (another mod answers for it),
  SISCO is then no worse than the game's own, and the `memory` and `size` lines of the log name the figure.
- Linux (Proton): the log shows the budget the game can really reach. The Complete Edition's own video-memory figure
  there is 3072 MB, which already held the world under SISCO's 4000 (a player's two logs, 2,763 MB held = 0.9 x 3072);
  the budget is now sized under it, so the world the game holds is the same as in 1.0.4 and the number is the real one.
- The brake acts at once on a fast fall. When the largest free block of the address space drops by 96 MB or more
  within one second, SISCO lowers the budget two steps on that very second instead of waiting the usual 10 to 20
  seconds between steps, and a hold reached on such a fall goes two steps under what is in use. A player's game ran
  out of memory a few minutes into play: the address space went from over 512 MB free to 398 MB in one second, and the
  game crashed before the next step could come. None of the recorded test runs fell that fast near the brake's line.
- Linux (Proton): the brake now sees the whole address space. In Proton's older 32-bit mode the graphics driver and
  its mappings of video memory share the game's 4 GB, and Windows' view of memory does not show them: a player's
  game crashed when the driver could not map more memory while SISCO's brake, which saw nothing change, never acted.
  On Linux SISCO now reads the free space from the game's own Linux memory map, and keeps Windows' view in the newer
  64-bit mode (`PROTON_USE_WOW64=1`), where it is the right one. The `size` line says which view it uses.
- Linux (Proton): the `size` line points to `PROTON_USE_WOW64=1` when the address space is fragmented and it is not
  set, and the `platform` line says when it is on. The same player's install had 103 MB of address space free at
  sizing without it and 2,438 MB with it.

## 1.0.4 (2026-09-27)

- The address-space brake no longer lowers the budget on a dip that recovers. It measured each fall from the most
  free address space seen since its last step, so free space rising by one of DXVK's chunks and falling back counted
  as a fall, and a heavy install lost two steps that way. A fall is now measured from where it was at the last step.
- The budget comes back after the brake lowers it. A budget level that has held for 5 minutes with no address space
  lost, and that the game is filling when the 5 minutes end, becomes the ceiling. When a burst (a taxi skip, a mission
  start) forces the budget under it, the budget climbs back one step at a time, each step after 5 quiet minutes and
  only while the game is using what it has. After a climb, a slow loss of address space (drift), a fast one while the
  game grows into the climb, or an emergency lowers the ceiling for good; a burst more than 5 minutes after the game
  took up the last climb does not, and the budget climbs back to the same level after each one. Time in menus and on
  loading screens counts for none of these minutes. Not with FusionFix's ExtraStreamingMemory, and not on Wine layouts
  where the brake's lines are lowered.
- SISCO watches the game's own video and system memory figures. When a Direct3D texture create fails for want of
  memory, the game lowers them to about what it is using and holds the world smaller until the display device resets.
  The log says when that happens and which of the two figures moved, and the brake lowers its budget to match, so its
  next step shrinks the world instead of changing nothing, and no raise goes past that level again, even when the game
  puts its figures back before SISCO can act. SISCO never puts the figures back itself. Never seen in 167 recorded
  sessions; the log line is how one will be found.
- The brake's log lines give the free address space in all, beside the largest free block.
- The previous session's log is kept as `SISCO.prev.log`, so a crashed session's log survives the next start. Only
  the game moves it aside: another program that loads SISCO (a launcher with an ASI loader) adds its note to the end
  of `SISCO.log` instead.
- A higher resolution lowers the budget on cards that cannot hold both (about 6 GB and under at 4K); going back to
  the lower resolution now gives it back, up to what SISCO sized at the start. Where the brake is holding the budget
  for the address space, only the card's figure comes back and the brake stays in charge.
- Taxi skips and other loads without a loading screen are fast again. While the game showed the same screen over
  and over, the render thread presented it as fast as it could, and each present held the lock the game's loader
  needs, so a skip took 15 to 20 seconds instead of 2. SISCO now lets one of those presents through every 33 ms,
  as it does on a loading screen. It starts only while the game draws under 16 frames a second and the same screen
  is presented at least 8 times per frame, and stops within half a second of normal frames. Normal play, menus and
  frame generation are not touched. It works with FusionFix builds from August 2026 on, which hook the game's frame
  counter; it is off while that counter stands still, and back on once it moves again.
- The main menu runs at its full frame rate again. The game shows it as part of its loading screen, so 1.0.3's
  loading-screen rule held it to 30 frames a second. Now every frame goes out while a menu is open, and the rule
  comes back the moment the load starts.

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
