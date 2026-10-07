# AGENTS.md

Quick reference for AI coding agents in this repo. It is the only agent doc
tracked in git, so it is self-contained. Read it before you change code.

ioQuake3-Wii is a Wii port of ioQuake3. It boots from the Homebrew Channel as
`boot.dol` and plays Quake III Arena, Open Arena and Team Arena from SD or USB
data. There is no debugger. The maintainer tests every change on real
hardware; "HW-confirmed" below means that test passed. Don't call a change
done before it is HW-confirmed.

---

## Current status (2026-10-07)

| Area | Status |
|---|---|
| Q3A (`make dol`) | Stable. Bot matches, big maps (JIT), mirrors, screenshots and repeated map changes HW-confirmed. |
| Open Arena (`make oa`) | Works. QVMs always run in the interpreter. Less soak time than Q3A. |
| Team Arena (`make ta`) | Works, HW-confirmed 2026-10-03: boot, gameplay, RoQ cinematics, pure online servers. Game code is linked into the DOL as native PPC. Included in future releases. |
| CLASSIC (`make classic`) | Joins real Dreamcast protocol-43 servers, HW-confirmed 2026-07-02. Hosting for Dreamcast clients is unfinished. |
| Mod Selector (`make modsel`) | Boot-time picker that sets `fs_game` to any `<dev>:/quake3/<mod>/` folder with a `.pk3`. It skips `baseq3`, `baseoa` and `missionpack` (`Wii_ScanModDirs`). |
| Native GX renderer | Default. HW-confirmed: 2D/3D, effects, mirrors/portals (CPU clip, 2026-10-04), decal depth offset (2026-10-03), CMPR textures (TA only so far, 2026-10-03). Open: visual parity vs OpenGX. |
| TV picture geometry | VI width 704, logical 640x480 in every mode, `wii_[xy]offset`/`wii_[xy]stretch` overscan cvars. Built, **not HW-confirmed**. |
| Input | GC pad, Wiimote + Nunchuk (IR aim), Classic Controller, Wii U GamePad (vWii), USB keyboard/mouse, wired USB pads (PS3, PS4, DualSense, Switch Pro). Xbox USB pads are unsupported. |
| Network | Online play and the server browser work. Downloads are enabled. |

### Open issues

- **Mods-menu crash.** Switching `fs_game` back and forth through the retail
  Mods menu (`Com_GameRestart`: full client, FS, cvar and net restart) crashes
  after one or two round trips. Root cause unknown. Get a debug-build
  `diag.txt` of the crash before you patch anything.
- **`r_dynamiclight 0` crashes release builds** at boot; a debug build with
  the same cmdline boots. The boot cmdline sets the misspelled `r_dynamic 0`
  on purpose (it does nothing, dynamic lights stay on). Don't "fix" the typo.
  Diagnosis needs a hardware debugger (USB Gecko + GDB).
- **Player-model browsing fills the hunk** ("Memory is low. Using deferred
  model."). The hunk resets only on map load. `r_lodbias` can't help: the
  GEKKO MD3 loader loads LoD 0 only.
- **Native GX visual parity vs OpenGX.** Suspects: RGB565/RGB5A3
  quantization, the mip chain and `GX_LIN_MIP_LIN` bias, wrap/clamp mapping,
  TEV env modes (ADD/DECAL/BLEND), alpha-test and blend mapping in `GL_State`.
- **CLASSIC hosting.** The retail `CS_*` configstring slots are not remapped.
  A code audit predicts that Dreamcast clients fail with "Client/Server game
  mismatch" (not seen on hardware yet). Pin the slots first: one `classic-debug` connect to a DC server logs
  `[CLASSIC] gamestate cs N` (`cl_parse.c`). Also unpinned: retail events
  51/53 (RAILTRAIL/BULLET order), shotgun `origin2` semantics, events 63+.
- **The QVM interpreter has no `dataAlloc` bound** on scalar loads/stores
  (`vm_interpreted.c` masks with `dataMask` only). The JIT checks it. OA
  release builds and TA mod QVMs run the interpreter.
- **Interlaced default video mode** can crop the top of the picture on some
  TVs (in the display's deinterlacer). Users pick 240p/264p at the boot
  prompt. An automatic switch to those modes was rejected: it halves the
  resolution for everyone.
- **`r_anaglyphMode`** does nothing useful under native GX (`qglColorMask` is
  a no-op). Not a goal.

---

## Build

- Use the **devkitPro MSYS2 shell** (Start → devkitPro → MSYS2). Git Bash
  doesn't set `DEVKITPRO`/`DEVKITPPC` and the Makefile stops.
- Fresh clone → `make dol`. The ioQ3 sources are vendored and patched in
  `code/`: no patch step, no submodule. Needs `ppc-zlib` (`pacman -S ppc-zlib`).

| Command | Output | App path |
|---|---|---|
| `make dol` | `build/boot.dol` (Q3A) | `<dev>:/apps/ioquake3/` |
| `make oa` | `build_oa/boot.dol` (Open Arena) | `<dev>:/apps/openarena/` |
| `make ta` | `build_ta/boot.dol` (Team Arena) | `<dev>:/apps/teamarena/` |
| `make classic` | `build_classic/boot.dol` (Dreamcast crossplay) | `<dev>:/apps/ioquake3-classic/` |
| `make modsel` | `build_modsel/boot.dol` (Mod Selector) | `<dev>:/apps/ioquake3-modselect/` |
| `make debug`, `oa-debug`, `ta-debug`, `classic-debug`, `modsel-debug` | same dirs, debug build | logs to `<dev>:/quake3/` |
| `make all-flavors` | Q3A + OA + TA + CLASSIC release | |
| `make clean` | removes every `build*/` dir | |

- **Use the hyphenated debug targets.** `make ta debug` is two goals: a
  release TA build plus a Q3A debug build. It writes no TA logs.
- Defaults: native GX renderer, native-PPC QVM JIT, `WII_MAXFPS=60`. Escape
  hatches: `WII_VM_NATIVE=0` (interpreter), `WII_OPENGX=1` (legacy OpenGX
  renderer), `WII_MAXFPS=30`, `WII_FSGAME=<mod>`, `WII_GX_PROFILE=1` (with a
  debug target).
- OA and TA set `override WII_VM_NATIVE := 0`. Don't remove it.
- Header and CFLAGS edits rebuild automatically (`-MMD` dep files and
  `$(BUILD)/.cflags-stamp`). Only a renderer-backend switch needs
  `make clean`; the `$(BUILD)/.backend-*` stamp aborts the build otherwise.
- Release builds define `NDEBUG` (no asserts, no `HUNK_DEBUG` headers). Debug
  builds define `WII_DEBUG`.
- **Compile-rule zones:** `-DWII_INCLUDE_NET` only for the port-layer dirs and
  the explicit `net_ip.o` rule. Never for vendored ioQ3: `huffman.c`'s static
  `send()` collides with BSD `send()`. A vendored file that needs sockets gets
  its own per-file rule.
- Video mode (default / 240p NTSC / 264p PAL) is chosen at boot with a d-pad
  prompt (`Wii_VideoModeBootPrompt`), not at build time.

### Game data (`<dev>` = `sd:` or `usb:`)

- Q3A and Mod Selector: `<dev>:/quake3/baseq3/pak*.pk3`, plus any mod folder.
- OA: `<dev>:/quake3/baseoa/pak*.pk3`.
- TA: `baseq3/` + `<dev>:/quake3/missionpack/pak*.pk3`.
- CLASSIC: `baseq3/pak0-pak2.pk3`. The DOL embeds `zpack-classic.pk3` and
  writes it to `baseq3/` at boot when the size or CRC32 differ.

`Wii_MountSD` mounts SD and uses `sd:/quake3` if it exists. Only otherwise
does it mount USB and use `usb:/quake3` (Wii Mini has no SD slot). Every path,
config, `qkey` and log uses that root (`wii_dev_root`).

---

## Code layout

- `code/sys/`, `code/audio/`, `code/input/`, `code/renderer/`: Wii port layer.
  - `sys/wii_main.c`: `main()`, boot prompts, storage mount, boot cmdline.
  - `sys/wii_sys.c`: `Sys_*`, `Wii_MEM2_Init`, `calloc`/`mmap` wrappers,
    sound shims (`S_Init`), `GLimp_*`.
  - `sys/wii_glimp.c`: GX/VI init, video modes, `Wii_GX_EndFrame`.
  - `sys/wii_modules.c`, `sys/wii_module.ld`: TA native game modules.
  - `sys/wii_platform.h`: force-included in every TU. Wii sizing and
    identity overrides (integer literals, not strings).
  - `renderer/tr_gx*.c`, `tr_gx.h`: native GX backend. `renderer/qgl_wii.c`:
    `qgl*` bindings.
  - `input/wii_input.c`: all controllers. `input/wii_usb_hid.c`: wired USB
    pads.
- `code/qcommon/`, `code/client/`, `code/server/`, `code/botlib/`,
  `code/renderergl1/`, `code/renderercommon/`: vendored ioQ3, patched in
  place. Edit directly; most Wii changes sit under `GEKKO`, `WII_*` or flavor
  `#ifdef`s.
- `code/cgame/`, `code/game/`, `code/ui/` (missionpack UI): VM-side code.
  Q3A, OA, CLASSIC and Mod Selector use only the headers; TA compiles the
  code into the DOL. `ui/menudef.h` (repo root) is their `../../ui/` include.
- `libs/opengx/`: prebuilt patched OpenGX for `WII_OPENGX=1`.
  `libs/wiidrc/`: Wii U GamePad library.
- `fixes/baseq3/zpack-classic.pk3`: prebuilt CLASSIC QVMs and assets.
- **Not in the repo:** `code/q3_ui/`, a QVM toolchain, game data. Changes to
  `code/cgame`, `code/game` or `code/ui` reach TA directly. They reach other
  flavors only through an external QVM rebuild and repack.

---

## Memory (88 MB: 24 MB MEM1 + 64 MB MEM2)

`Wii_MEM2_Init` (`wii_sys.c`) measures Arena2 (about 52 MB under IOS58) and
reserves a bump region at its top. The hunk lives in the bump. Everything
else comes from sbrk: free MEM1 after the DOL image, then MEM2 below the bump.

| Region | Q3A, OA, CLASSIC, Mod Selector | TA |
|---|---|---|
| MEM2 bump | fixed 33 MB | `min(Arena2 - SBRK_RESERVE, 40 MB)`, `SBRK_RESERVE` = 27 MB |
| Hunk (`hunk_mb`, `wii_main.c`) | bump - 1 MB = 32 MB | whole bump: 25 MB at 52 MB Arena2 |
| sbrk in MEM2 | about 19 MB | 27 MB |
| Zone | 8 MB, sbrk | 8 MB |
| Sound pool (`com_soundMegs 2`) | about 6 MB, sbrk | about 6 MB |
| Native game modules | none | about 8.4 MB static in MEM1 (mostly bss) |
| FAT read cache | 512 KB (Q3A release), 128 KB (others) | 128 KB |
| GX FIFO / XFBs / stack | 256 KB / 2 XFBs / 512 KB, MEM1 | same |

- Read `Wii_MEM2_Init` and `hunk_mb` in `wii_main.c` before you change any
  `*Megs` value or reserve.
- **Don't move the non-TA arm onto the `SBRK_RESERVE` formula.** Tried
  2026-07-17: it broke CLASSIC on hardware, because the embedded zpack uses
  memory before `Wii_MEM2_Init` runs. The fixed 33 MB is HW-proven.
- Never size the bump to all of Arena2 without a floor for sbrk (a fixed
  40 MB once crashed boot).
- TA's 27 MB = 18 MB (zone, sound, overhead incl. Bluetooth/WPAD) + 9 MB for
  the MEM1 its modules took. Before you retune it, read `hunk_remaining=` and
  `fordblks` from a `ta-debug` `diag.txt`. Trust `mallinfo().fordblks` only;
  `arena`/`uordblks` read back wrong on this libogc.
- `__wrap_calloc` sends requests of 16 MB or more to the bump. That is how the
  hunk gets there.
- The bump can't free. Anything served from it that is later freed must go
  through a reuse table like `s_vmcode` (JIT code buffers), or not use it.
- Texture buffers use `memalign(32)`/`free` (sbrk), never the bump:
  `R_DeleteTextures` frees them on map change.
- FAT cache: 16 pages only on the Q3A release build. Never on CLASSIC (it
  caused the JIT's "Not enough memory"), TA, OA, Mod Selector or debug
  builds. Every mounted disc gets its own cache, so USB is mounted only when
  SD has no `quake3` dir.
- `com_soundMegs`, `com_maxfps` and `vm_*` are `CVAR_ARCHIVE`: a saved config
  overrides the cmdline.

---

## VMs

- **JIT** (`WII_VM_NATIVE=1`; Q3A, CLASSIC, Mod Selector) needs all three:
  `HAVE_VM_COMPILED` in `wii_platform.h`; `vm_*` = 2 (cmdline `vm_ui` and
  `Wii_Input_SetCvars()`); `|| defined(GEKKO)` in `vm_powerpc.c`'s top guard
  (devkitPPC doesn't define `__powerpc__`; without it the file compiles empty
  and the link fails).
- The JIT gives **no FPS gain** (the game is render-bound). Its value is lower
  hunk use: compiled code lives in the bump. Compile-time IR comes from a
  512 KB chunked arena reset at both ends of `VM_Compile`.
- `VM_LoadQVM` (`vm.c`, GEKKO arm) sizes `dataAlloc` to the real data plus the
  64 KB stack, not the power of 2. `dataMask` stays a power of 2. The JIT
  bounds-checks loads and stores against `dataAlloc` (`needsDataAllocCheck`);
  so do `VM_BlockCopy` and `VM_ArgPtr`.
- **OA forces the interpreter:** three resident JIT code buffers for its
  larger QVMs starve the map load.
- **TA native modules** (`WII_NATIVE_TA`, set by `make ta`). The missionpack
  cgame, qagame and ui compile to PPC with the engine's exact `CFLAGS` plus
  `-fno-common -DMISSIONPACK -D<MOD> -DvmMain=vmMain_<mod>
  -DdllEntry=dllEntry_<mod>`. Each is `ld -r`-linked with
  `code/sys/wii_module.ld`; `objcopy -G` keeps only the entry points and the
  `.data`/`.bss` markers global. `wii_modules.c` restores pristine `.data` and
  zeroes `.bss` on every load.
  - Never give the modules struct-affecting defines the engine doesn't have.
    A previous (abandoned) Elite Force port died of that ABI class.
  - Undefined symbols left in the modules must be libc/libm only. Re-check
    with `nm -u` after a game-code sync.
  - Selection: `vm_*` = 0 and `fs_game` = `missionpack`
    (`Sys_WiiUseBuiltinModule`). `vm_*` 0 is set in the cmdline (`vm_ui`), in
    `Wii_Input_SetCvars()`, and in `Com_GameRestart` **after**
    `Com_ExecuteCfg` (a stale archived `1` would load the QVMs onto the hunk).
  - Mods (`fs_game` other than `missionpack`) load their own QVMs through the
    interpreter.
  - Pure servers: native cgame/ui run only when `FS_WiiStockVM()` finds the
    QVM in a stock missionpack pak; it marks that pak referenced so the `cp`
    check passes. qagame is never pure-checked.
  - No sandbox: a game-code bug is a DSI crash, not `ERR_DROP`.

---

## Renderer (native GX, `WII_NATIVE_GX=1`)

Files: `tr_gx.c` (state, draw path, projection, portal clip),
`tr_gx_texture.c` (uploads, CMPR, readbacks), `tr_gx.h` (never include it from
`code/sys/`; use a bare `extern`), `qgl_wii.c`, plus `WII_NATIVE_GX` arms in
`renderergl1/` and `sys/wii_glimp.c`.

- Call `GX_InvVtxCache()` before indexed draws whose arrays changed and after
  any direct/indexed descriptor flip. Without it: garbled text and models.
- `GXBE_Upload32` runs inside `Upload32` at level 0 with the final scaled
  buffer. Formats: RGB565, RGB5A3, or CMPR (`gx_want_cmpr`: opaque picmip
  images and opaque 2D art from 256 px; TA 2D from 128 px, fonts excluded).
  Alpha never goes CMPR (GX has no DXT5). Never CMPR: lightmaps
  (`IMGFLAG_NO_COMPRESSION`), `*` builtins (incl. `*scratch` cinematics),
  images under 8x8. `R_CreateImage` passes the hint via
  `GXBE_SetUploadImage`; the next upload consumes it.
- `texnum` is 0-based, a counter into `s_gx_texobjs[GX_MAX_TEXOBJS]` (2048).
- `tess` is `aligned(32)` for `GX_SetArray`.
- `GXBool`, `GXCompare` and `GXTexMapID` are anonymous enums in this libogc:
  don't declare variables of those types.
- The GX projection uses 6 elements only (`[0][0] [0][2] [1][1] [1][2] [2][2]
  [2][3]`) with `zGX = (zGL - w) / 2`. No oblique near plane is possible, so
  portal/mirror clipping is done on the CPU in `GXBE_DrawTess`
  (`GXBE_SetClipPlane`).
- Decal polygon offset emulates both GL terms: a constant NDC bias in the
  projection and a per-vertex slope pull in `GXBE_DrawTess`. The slope term
  is load-bearing (marks sank into floors without it).
- Staging ring: `GX_SetDrawSync(0)` and `s_syncToken = 0` always together. On
  ring-alloc failure, `GX_DrawDone` right after `GX_End`, never deferred.
- Never call `ogx_*` in a native build: `ogx_prepare_swap_buffers` stomps the
  ring's fence tokens.
- `GLimp_Init()` calls `Wii_GX_Init()` (idempotent), because every
  `vid_restart` runs `Wii_GX_Shutdown()`. Framebuffers are allocated once
  (`if (!s_framebuf[0])`).
- `Wii_GX_EndFrame` has no `VIDEO_WaitVSync()`: hard vsync halves FPS.
- TV picture: the engine always sees a logical 640x480. The 240p/264p line
  scale and the overscan border are applied in `GXBE_MapRect`; every new
  viewport, scissor or readback must go through it. `WII_VI_WIDTH` (704)
  widens the VI horizontally only. Never fix overscan with vertical VI
  fields. The EFB-to-XFB copy scissor stays full size.
- Never use `r_fastsky 1`: it silently kills portal and mirror rendering.
- The frame-time ceiling is the render backend (`bk`, about 13-16 ms of a
  22 ms frame), not the VMs. For FPS, cut geometry. An unbuilt option is a
  hybrid indexed emitter for world surfaces; first measure whether `bk` is
  dispatch-bound or FIFO-bound.
- OpenGX fallback: `libs/opengx/` carries load-bearing patches (viewport Y,
  `glDepthRange` sets the viewport and its dirty flag, 2D projection origin
  0). Re-apply them if you re-vendor. Never add an `fb_height` field. OpenGX
  `printf` doesn't reach the console; use `wii_diag()`.

---

## Boot, cmdline and shutdown

- Boot order (`wii_main.c`): console → `Wii_MEM2_Init` → mount → CLASSIC
  zpack extract → `Wii_Input_Init` → video-mode prompt → mod picker (Mod
  Selector) → `Wii_Net_Init` → `Wii_Input_USBHIDInit` → audio →
  `Wii_GX_Init` → `Com_Init` → `Wii_Input_SetCvars()` → frame loop.
  `Wii_Input_USBHIDInit` must stay after network init; earlier hangs the
  console.
- **Cmdline limit:** `Com_ParseCommandLine` splits at most **31** `+` lines
  (`MAX_CONSOLE_LINES` 32; slot 0 is the prefix). Extra lines merge silently
  into the previous line's value. Current use: Q3A/CLASSIC/Mod Selector 30
  (31 with `fs_game`), OA 29, TA 30 (incl. `fs_game missionpack`). Recount
  before you add a `+set`. Cvars that don't gate `FS_InitFilesystem` go in
  `Wii_Input_SetCvars()`, which runs after `Com_Init`. A `+set` on a cvar
  that is registered later as `CVAR_ARCHIVE` is unreliable.
- `Sys_Milliseconds` subtracts a base `gettime()`. Without it the `int`
  overflows, `NET_Sleep` spins and the boot hangs on a black screen.
- `Wii_Net_Init` uses 3 DHCP retries (20 hung boot with no network).
  `NET_Sleep` skips `select()` on an empty fd set (libogc hangs on it).
- Shutdown: SD writes go in `IN_Shutdown` (FS still alive), never in
  `Sys_Quit` (runs after `FS_Shutdown`).
- Exit: `exit(0)` returns to HBC. Never `SYS_ResetSystem(SYS_RETURNTOMENU, …)`.
  Power button: `WPAD_Shutdown()`, then `SYS_ResetSystem(SYS_POWEROFF, 0, 0)`;
  without `WPAD_Shutdown()` the poweroff fails silently.
- Power/reset callbacks run in IOS context. They only latch flags; the main
  loop runs `Com_Quit_f()`.

---

## Input

- Priority: wired USB pad → Wii U GamePad → Classic → Nunchuk → Wiimote → GC
  pad, plus USB keyboard/mouse. Everything goes through `Wii_Input_Frame()`.
- The button label tables in `cl_keys.c` (`Key_KeynumToString`) are parallel
  to the button maps in `wii_input.c` (`s_gc_buttons` etc.). Change both.
- `wii_binds_{gc,wm,cc,drc,usb}.cfg` are saved in `IN_Shutdown` with
  canonical `JOYn` names (`wii_keynumstr_raw = 1`). Friendly labels ("A",
  "ZR") don't parse back.
- Menu bind capture (`MenuRawLayer`): holding the modifier (Minus; GC Z; USB
  Back/Share) in a menu switches emission to the in-game `K_JOYn` map;
  tapping it alone emits its own code. Never emit a menu key and a JOY code
  for one press. Keep modifier codes outside `K_JOY1`-`K_JOY4`. New menu-key
  paths must respect the raw layer and its release-suppression mask.
- Analog triggers go through `InjectKey()`; `key_held[]` is the only edge
  source. A controller-type switch calls `ReleaseAllKeys()`.
- `cl_sensitivity` is the only user aim-speed control. `j_yaw`/`j_pitch` are
  fixed and force-set every boot.
- A new Classic Controller needs a `WPAD_SetDataFormat` retrigger before its
  buttons report (`wii_input.c` and `Wii_BootPad_Poll`).
- USB HID (`wii_usb_hid.c`):
  - Never call `USB_SetConfiguration()`.
  - Every IPC buffer is heap `memalign(32)`, never on the stack.
  - `USBHID_MAX_REPORT` (64) must cover the largest report of any pad.
  - No blocking IOS calls and no file I/O (incl. `wii_diag`) in IOS
    callbacks. Set a flag for `USBHID_Poll()` instead.
  - Hotplug by polling `USB_GetDeviceList()`, never
    `USB_DeviceChangeNotifyAsync()` (froze the console on attach).
  - Scan class 0x03 (HID) and 0xFF (vendor).
  - Each `_Parse()` outputs up = negative Y; `USBPad_Input_Frame()` must not
    negate again.

---

## Audio

- `S_Init` in `wii_sys.c` bypasses `snd_main.c`. It must register every sound
  command `snd_main.c` would (`play`, `music`, `stopmusic`), or they leak to
  the server as "unknown cmd".
- Delegate to the outermost `S_Base_*` functions, never an inner helper
  (that broke background music).
- ASND uses `ASND_SetInfiniteVoice` + `ASND_GetTickCounterVoice`; the
  callback version raced and played on one speaker.
- `ResampleSfxRaw` must not `LittleShort()`: samples are already
  native-endian (a second swap gives static).
- `MAX_RAW_SAMPLES` stays 16384 (4096 gives crunchy RoQ audio). Keep the
  `SND_setup` null checks.

---

## Filesystem and network

- `MAX_RELIABLE_COMMANDS` stays 64 (16 broke servers with custom content).
  `PACKET_BACKUP` 16, `MAX_DOWNLOAD_WINDOW` 48.
- `Sys_ListFiles` honors `filter`, `wantsubs` and the `"/"` directory-only
  convention.
- `files.c` never loads `autoexec.cfg` or the saved config from a `.pk3` or
  `.pk3dir` (downloads are on, so a server could push one).
- `weapnext`/`weapprev` are registered engine-side (`cl_input.c`) so they
  don't leak to the server as "unknown cmd" during a cgame reload.

---

## Flavor rules

- **OA** (`-DSTANDALONEOA`): must **not** define `STANDALONE`; the legacy
  protocol and master name need the `#ifndef STANDALONE` paths.
  `GAMENAME_FOR_MASTER` is "Quake3Arena" ("openarena" gives "Game
  mismatch"). Master `dpmaster.deathmask.net`. Config `oaconfig.cfg`.
- **TA** (`-DSTANDALONETA -DWII_NATIVE_TA`; defines `STANDALONE`):
  `BASEGAME` stays `baseq3`. `+set fs_game missionpack` puts missionpack first
  in the search path. With `BASEGAME "missionpack"`, baseq3's `ui.qvm`
  (API 4) shadows missionpack's (API 6) and the native-module gate fails.
- **CLASSIC** (`-DCLASSIC -DLEGACY_PROTOCOL -DPROTOCOL_VERSION=43`):
  everything is `#ifdef CLASSIC`. Config `classicconfig.cfg`.
  - `bg_public.h` uses the **retail** `persEnum_t` under CLASSIC. The QVM
    event enum stays modern; retail/modern event numbers are translated at
    the engine boundary (`msg.c` `classicEventMap`, used by `cl_cgame.c`,
    `sv_snapshot.c`, `sv_client.c`). Don't redefine the QVM enum.
  - `CL_CompatUserinfo` (`cl_main.c`) sends a retail userinfo whitelist and
    advertises a rate of at most 5000. Dreamcast hosts need it to stay up.
  - `sv_pure` is `"0"` + `CVAR_ROM` (Dreamcast clients can't validate).
  - The "read past end of server message" print on connect is benign.

---

## Debugging on hardware

Build a debug target. Logs go to `<dev>:/quake3/`:
- `boot.txt`: boot timeline; the last line is the failing call.
- `diag.txt`: engine diagnostics, `Sys_Error` trace, VM load/compile, hunk
  and `mmap` failures (grep `hunk_remaining=`, `Hunk_Alloc FAILED`, `mmap:`).
- `crash.txt`: exists once storage is mounted (early checkpoint).

Rules:
- `wii_diag()` keeps one file handle open and only `fflush`es. Never
  open/close per call: it stalls the GX FIFO mid-frame and corrupts frames.
- `wii_diag_sync()` adds `fsync`, so the line survives a crash. Use it on
  load and error paths only, never per frame.
- A symptom in several different builds points at shared state, not code:
  the saved config (`q3config.cfg`, `oaconfig.cfg`, `classicconfig.cfg`),
  `wii_binds_*.cfg`, or the SD card. Rename the config before you ask for a
  rebuild.
- Release builds hide the auto full-screen console while disconnected or
  loading (`cl_console.c`); debug builds keep it for boot output.

---

## Do not re-chase (settled on hardware)

- The JIT, `r_vertexLight 1` or SIMD for FPS (Broadway has no AltiVec). The
  cost is backend geometry and draw volume.
- `r_lodbias` (no effect: only LoD 0 is loaded).
- Oblique near-plane projection on GX (caused the black q3dm0 mirror).
- `VIDEO_WaitVSync()` or a `VIDEO_GetCurrentFramebuffer()` guard in
  `Wii_GX_EndFrame` (the guard made frame pacing unplayable).
- CMPR through OpenGX (crashes). The native CMPR encoder is separate and
  works.
- The `SBRK_RESERVE` formula for non-TA builds; the 512 KB FAT cache on
  CLASSIC.
- Automatic switch of the interlaced default mode to 240p/264p.
- Xbox One/Series/360 USB pads: vendor class 0xFF crashes inside libogc/IOS
  on the first report. Also `USB_DeviceChangeNotifyAsync()` and
  `USB_SetConfiguration()`.
- Wii Mini USB2 or IOS reload. vWii RAM or CPU clock (fixed at 88 MB and by
  c2w_patcher).
- Quake3e beyond the `.pk3dir` fix (ppc64 JIT, VBO/GLSL renderers); the
  macro-op interpreter (no gain).
- `MAX_RELIABLE_COMMANDS` below 64, `MAX_RAW_SAMPLES` below 16384, more than
  3 DHCP retries, an `fb_height` field in OpenGX.

---

## Keeping this file current

Edit the relevant section in place when a rule, build command or status
changes. Dated history belongs in git, not here.
