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
| `make 240p` / `make 240p-pal` | 240p NTSC / 264p PAL output |
| `make all-flavors` | Q3A + OA + TA in one go |
| `make clean` | wipe all `build*/` dirs |
| `make WII_VM_NATIVE=1 dol` | native-PPC JIT QVMs (no FPS gain — render-bound) |

Copy the produced `boot.dol` to the matching SD path:
- Q3A: `sd:/apps/ioquake3/boot.dol`
- OA: `sd:/apps/openarena/boot.dol`
- TA: `sd:/apps/teamarena/boot.dol`

SD data required:
- Q3A: `sd:/quake3/baseq3/pak*.pk3`
- OA: `sd:/quake3/baseoa/pak*.pk3`
- TA: baseq3 + `sd:/quake3/missionpack/pak*.pk3`

---

## Code layout

- `code/sys/`, `code/audio/`, `code/input/`, `code/renderer/` — Wii port layer (original port code).
- `code/qcommon/`, `code/client/`, `code/server/`, `code/botlib/`, `code/renderergl1/`, `code/renderercommon/` — vendored ioQ3 engine, patched in-tree. Edit directly.
- `code/cgame/`, `code/game/`, `code/ui/` — vendored VM-side code. Engine only uses the `.h` files; `.c` files compile to QVM bytecode (shipped in the `.pk3`s).
- `code/sys/wii_platform.h` — force-included before every TU; supplies Wii memory budget and platform identity.
- `libs/opengx/` — prebuilt patched OpenGX (static lib + headers). Source lives outside this repo.

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
- **`com_soundMegs`:** never lower below 4 for Q3/OA — values of 1 or 2 cause choppy audio and frame drops.
- **`MAX_RELIABLE_COMMANDS`:** keep at stock 64. Lowering it broke connecting to servers with custom content.
- **`MAX_RAW_SAMPLES`:** keep at stock 16384. 4096 starves the RoQ cinematic decoder → crunchy audio.
- **`Sys_Milliseconds` base subtraction** (`wii_sys.c`): captures `gettime()` on first call and subtracts it before casting to `int`. Removing it causes integer overflow → negative return → permanent black-screen hang.
- **`S_Init` in `wii_sys.c`** bypasses `snd_main.c` entirely. Any sound console command `snd_main.c` would register (e.g. `play`) must also be registered here, or it leaks to the server as "unknown cmd".
- **Shutdown FS ordering:** SD writes during shutdown must happen in `IN_Shutdown` (called while FS is alive), not `Sys_Quit` (called after `FS_Shutdown`).
- **Exit:** use `exit(0)` to return to HBC. Never `SYS_ResetSystem` — that goes to the Wii system menu.
- **OpenGX patches** in `libs/opengx/` are load-bearing (depth range, viewport Y-flip, 2D projection origin). If you re-vendor OpenGX from upstream, re-apply them. Never add an `fb_height` field back to OpenGX — it was tried and made icons disappear.
- **Memory budget:** hunk is ~32 MB (MEM2 bump), zone 8 MB (sbrk), sound ~12.6 MB (sbrk). TA uses a dynamic bump formula (`min(total − 15 MB, 40 MB)`). Read `wii_sys.c:Wii_MEM2_Init` before changing any `*Megs` constant.
- **QVM JIT (`WII_VM_NATIVE=1`):** works but gives no FPS gain — the bottleneck is the OpenGX→GX render backend (`bk`), not VM execution. Don't chase this.
- **`vm.c` power-of-2 rounding:** the rounding loop sets only `dataMask`; `dataAlloc` uses the real unrounded size. This saves several MB for TA. Never revert it.
- **Button label tables in `cl_keys.c`** are parallel arrays to the button maps in `wii_input.c`. If you reorder the button maps, update the label tables to match.
- **Per-controller cfg files** (`wii_binds_gc.cfg`, etc.) are saved in `IN_Shutdown`, not `Sys_Quit`. Don't move them later in the shutdown sequence.
- **`STANDALONEOA` must NOT define `STANDALONE`** — OA needs the `#ifndef STANDALONE` guards active for the legacy protocol version and master-server name.
- **TA search-path ordering:** `BASEGAME` must stay `"baseq3"`. Setting it to `"missionpack"` puts baseq3 at the head of the search path and its `ui.qvm` (API v4) shadows missionpack's (API v6) — the TA menu never appears.
- **`r_lodbias`** does not reduce memory use — it only selects an already-loaded LoD tier. It cannot fix the "Memory is low / deferred model" issue.
