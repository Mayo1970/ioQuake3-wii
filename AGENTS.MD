# AGENTS.md

A Wii port of ioQuake3. Boots from the Homebrew Channel as `boot.dol` and
plays Quake III Arena, Open Arena, and Team Arena from SD card data.

---

## Setup

- Build from the **devkitPro MSYS2 shell** (Start → devkitPro → MSYS2). Plain Git Bash won't work — it won't set `DEVKITPRO`/`DEVKITPPC`.
- No sibling clone, no patch step, no submodule — fresh clone builds immediately.

---

## Build commands

| Command | Output |
|---|---|
| `make dol` | `build/boot.dol` (Q3A) |
| `make oa` | `build_oa/boot.dol` (Open Arena) |
| `make ta` | `build_ta/boot.dol` (Team Arena) |
| `make debug` / `oa-debug` / `ta-debug` | debug build with SD logging |
| `make all-flavors` | Q3A + OA + TA in one go |
| `make clean` | wipe all `build*/` dirs |

Video mode (default / 240p NTSC / 264p PAL) is chosen at **boot**, not build
time — every DOL contains all three; a d-pad prompt after controller init
(UP/LEFT/RIGHT, 10s timeout → default) picks one. See
`Wii_VideoModeBootPrompt()` in `code/sys/wii_main.c`.

Default build (`make dol`) is **native GX renderer + native-PPC JIT QVMs +
`WII_MAXFPS=60`** — no flags needed. Escape hatches:
- `make WII_VM_NATIVE=0 dol` — bytecode interpreter instead of the JIT (needs
  several MB more hunk; can OOM on big maps).
- `make WII_OPENGX=1 dol` — legacy OpenGX renderer instead of native GX.
- `make WII_MAXFPS=30 dol` — lower the in-game cap (menus/loading stay 60).

**Switching renderer backends requires `make clean`** — a
`$(BUILD)/.backend-*` stamp aborts the build otherwise, since the `.o` files
don't depend on the Makefile and would link with mixed flags. Same hazard
applies to any CFLAGS change (e.g. release `-DNDEBUG` vs debug): `make clean`
first.

Copy the produced `boot.dol` to the matching app path on the data device:
- Q3A: `<dev>:/apps/ioquake3/boot.dol`
- OA: `<dev>:/apps/openarena/boot.dol`
- TA: `<dev>:/apps/teamarena/boot.dol`

Data required (`<dev>` is `sd:` or `usb:` — see device-root note below):
- Q3A: `<dev>:/quake3/baseq3/pak*.pk3`
- OA: `<dev>:/quake3/baseoa/pak*.pk3`
- TA: baseq3 + `<dev>:/quake3/missionpack/pak*.pk3`

At boot the port probes `sd:/quake3` first, then `usb:/quake3`, and uses the
device that has the `quake3` dir for all storage paths (`wii_dev_root`). SD is
tried first, so normal Wii behaviour is unchanged; USB is the path for Wii Mini
(no SD slot).

---

## Code layout

- `code/sys/`, `code/audio/`, `code/input/`, `code/renderer/` — Wii port layer (original port code).
- `code/qcommon/`, `code/client/`, `code/server/`, `code/botlib/`, `code/renderergl1/`, `code/renderercommon/` — vendored ioQ3 engine, patched in-tree. Edit directly.
- `code/cgame/`, `code/game/`, `code/ui/` — vendored VM-side code. Engine only uses the `.h` files; `.c` files compile to QVM bytecode (shipped in the `.pk3`s).
- `code/renderer/tr_gx*.c` — native GX backend (default renderer). `code/sys/wii_glimp.c` drives the GX/VI setup.
- `code/sys/wii_platform.h` — force-included before every TU; supplies Wii memory budget and platform identity.
- `libs/opengx/` — prebuilt patched OpenGX (static lib + headers) for the `WII_OPENGX=1` fallback renderer. Source lives outside this repo.
- `libs/wiidrc/` — Wii U GamePad (DRC) support library and headers (vWii only).

---

## Debugging on hardware

No debugger available. Build a `debug` target and read SD logs:
- `sd:/quake3/boot.txt` — main boot timeline; last line = failing call
- `sd:/quake3/diag.txt` — engine diagnostics + `Sys_Error` trace
- `sd:/quake3/crash.txt` — existence means libfat mounted (early checkpoint)

---

## Critical rules (do not break these)

- **Build shell:** always use devkitPro MSYS2. Never Git Bash.
- **Cmdline `+set` limit:** `MAX_CONSOLE_LINES=32`. Keep cmdline slots ≤ 32. Anything that doesn't need to be set before `Com_Init` goes in `Wii_Input_SetCvars()` instead.
- **`-DWII_INCLUDE_NET`:** only add to port-layer files (`code/sys/`, `code/audio/`, etc.) and the explicit `net_ip.o` rule. Never to vendored ioQ3 source — `huffman.c`'s static `send()` collides with BSD socket `send()`.
- **`r_fastsky 1`:** never use on Wii — it silently kills all portal/mirror rendering via an early-out in `R_MirrorViewBySurface`.
- **`com_soundMegs`:** all flavors intentionally use `2` now. ADPCM 4:1 sound storage keeps the working set inside the smaller pool while freeing sbrk for QVM/JIT and map loads.
- **`MAX_RELIABLE_COMMANDS`:** keep at stock 64. Lowering it broke connecting to servers with custom content.
- **`MAX_RAW_SAMPLES`:** keep at stock 16384. 4096 starves the RoQ cinematic decoder → crunchy audio.
- **`Sys_Milliseconds` base subtraction** (`wii_sys.c`): captures `gettime()` on first call and subtracts it before casting to `int`. Removing it causes integer overflow → negative return → permanent black-screen hang.
- **`S_Init` in `wii_sys.c`** bypasses `snd_main.c` entirely. Any sound console command `snd_main.c` would register (e.g. `play`) must also be registered here, or it leaks to the server as "unknown cmd".
- **Shutdown FS ordering:** SD writes during shutdown must happen in `IN_Shutdown` (called while FS is alive), not `Sys_Quit` (called after `FS_Shutdown`).
- **Exit:** use `exit(0)` to return to HBC. Never `SYS_ResetSystem` — that goes to the Wii system menu.
- **Native GX is the default renderer.** `code/renderer/tr_gx*.c` is reached by `make dol`; OpenGX is the `WII_OPENGX=1` fallback only. Never call `ogx_*` functions from a native (`WII_NATIVE_GX`) build — `ogx_prepare_swap_buffers` runs `GX_SetDrawSync(0)` every frame and stomps the native staging-ring fence tokens (rare geometry corruption).
- **OpenGX patches** in `libs/opengx/` are load-bearing for the fallback build (depth range, viewport Y-flip, 2D projection origin). If you re-vendor OpenGX from upstream, re-apply them. Never add an `fb_height` field back to OpenGX — it was tried and made icons disappear.
- **Memory budget:** hunk is ~32 MB (MEM2 bump), zone 8 MB (sbrk), sound ~6.3 MB (sbrk, `com_soundMegs 2`). TA uses a dynamic bump formula (`min(total − 18 MB, 40 MB)`). Read `wii_sys.c:Wii_MEM2_Init` before changing any `*Megs` constant.
- **QVM JIT (default `WII_VM_NATIVE=1`):** works but gives no FPS gain — the bottleneck is the render backend (`bk`), not VM execution. The JIT's value is lower hunk use (compiled code lives off-hunk via the `s_vmcode` reuse table), so big maps that OOM the interpreter load. Don't chase it for framerate.
- **`vm.c` uses stock power-of-2 data rounding.** Older docs claimed a non-power-of-2 `dataAlloc` change saves TA memory — that change is **NOT in the tree** (verified absent). TA's headroom comes from the dynamic MEM2 bump in `wii_sys.c` alone. Don't assume the vm.c change exists.
- **Button label tables in `cl_keys.c`** are parallel arrays to the button maps in `wii_input.c`. If you reorder the button maps, update the label tables to match.
- **Per-controller cfg files** (`wii_binds_gc.cfg`, etc.) are saved in `IN_Shutdown`, not `Sys_Quit`. Don't move them later in the shutdown sequence.
- **`STANDALONEOA` must NOT define `STANDALONE`** — OA needs the `#ifndef STANDALONE` guards active for the legacy protocol version and master-server name.
- **TA search-path ordering:** `BASEGAME` must stay `"baseq3"`. Setting it to `"missionpack"` puts baseq3 at the head of the search path and its `ui.qvm` (API v4) shadows missionpack's (API v6) — the TA menu never appears.
- **`r_lodbias`** does not reduce memory use — it only selects an already-loaded LoD tier. It cannot fix the "Memory is low / deferred model" issue.
