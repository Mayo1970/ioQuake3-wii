# ioQuake3-Wii

A Nintendo Wii port of [ioQuake3](https://github.com/ioquake/ioq3). It boots
from the Homebrew Channel as `boot.dol` and runs Quake III Arena, Open Arena,
and Team Arena from SD or USB data.

The default renderer is the native GX backend. The older OpenGX path is still
available as a build-time fallback. The default VM mode is the native-PPC QVM
JIT.

## Status

- Boots, connects to servers, loads maps, and enters gameplay
- Native GX renderer by default, with legacy OpenGX fallback
- Networking works: Wi-Fi, LAN discovery, internet server browser, and content downloads
- Background music and RoQ cinematic playback
- Wiimote + Nunchuk with IR aim
- Classic Controller / Classic Controller Pro support
- Wii U GamePad support on vWii
- GameCube controller support
- USB keyboard and mouse support
- Bot support for offline and hosted games
- Local server hosting
- Per-controller button binding persistence across reboots
- Quake III Arena, Open Arena, and Team Arena build flavors
- Optional 240p NTSC / 264p PAL video output for CRTs and retro scalers

## Prerequisites

Build from the devkitPro MSYS2 shell on Windows. Plain Git Bash or a generic
MSYS shell will not set `DEVKITPRO`, `DEVKITPPC`, and the toolchain `PATH`
correctly.

1. Install devkitPro from <https://github.com/devkitPro/installer/releases/latest>.
2. Select `devkitPPC` and the Wii development libraries (`wii-dev`).
3. Install zlib for PowerPC:

```bash
pacman -S ppc-zlib
```

The Makefile can use an internal ioQ3 zlib if one is present, but the normal
fresh-clone path expects `ppc-zlib` from devkitPro portlibs. It also links
libjpeg from portlibs.

## Building

Run these from the repository root in the devkitPro MSYS2 shell.

| Command | Output |
|---|---|
| `make dol` | Q3A release: `build/boot.dol` |
| `make oa` | Open Arena release: `build_oa/boot.dol` |
| `make ta` | Team Arena release: `build_ta/boot.dol` |
| `make debug` | Q3A debug build with device-root logging: `build/boot.dol` |
| `make oa-debug` | OA debug build with device-root logging: `build_oa/boot.dol` |
| `make ta-debug` | TA debug build with device-root logging: `build_ta/boot.dol` |
| `make 240p` | Q3A 240p NTSC: `build/boot.dol` |
| `make oa-240p` | OA 240p NTSC: `build_oa/boot.dol` |
| `make ta-240p` | TA 240p NTSC: `build_ta/boot.dol` |
| `make 240p-pal` | Q3A 264p PAL: `build/boot.dol` |
| `make oa-240p-pal` | OA 264p PAL: `build_oa/boot.dol` |
| `make ta-240p-pal` | TA 264p PAL: `build_ta/boot.dol` |
| `make all-flavors` | Q3A + OA + TA release builds |
| `make all-flavors-240p` | Q3A + OA + TA 240p NTSC builds |
| `make all-flavors-240p-pal` | Q3A + OA + TA 264p PAL builds |
| `make clean` | Remove build output directories and copied zlib headers |

### Optional build flags

| Flag | Default | Description |
|---|---|---|
| `WII_MAXFPS=30` | `60` | Lower the in-game framerate cap if 60 FPS is unstable on a target console. Menus and loading run at 60 regardless. |
| `WII_VM_NATIVE=0` | `1` | Use the bytecode interpreter instead of the native-PPC QVM JIT. The interpreter can need several more MB of hunk and may OOM on larger maps. |
| `WII_FSGAME=modname` | empty | Boot directly into a mod by setting `fs_game`. |
| `WII_OPENGX=1` | `0` | Build the legacy OpenGX renderer instead of native GX. Run `make clean` before switching renderer backends. |
| `WII_NATIVE_GX=0` | `1` | Equivalent OpenGX fallback override. Run `make clean` before switching. |
| `WII_GX_PROFILE=1` | `0` | Enable the GX bottleneck profiler. Use with `make debug` so output reaches `diag.txt`. |
| `INPUT_BACKEND=gamecube` | `wiimote` | Build without WPAD/Wii U GamePad support; GameCube, USB keyboard, and USB mouse remain. |

Example:

```bash
make WII_MAXFPS=30 dol
make WII_VM_NATIVE=0 dol
make WII_OPENGX=1 debug
```

The build directory records which renderer backend was used. If you switch
between native GX and OpenGX without cleaning, the Makefile aborts instead of
linking mixed object files.

Release targets define `NDEBUG`, so assertions and hunk/zone debug headers are
compiled out. If you change build flags or switch between release and debug,
run `make clean` first so stale objects do not keep the old ABI.

### 240p / 264p mode

Normal builds use `VIDEO_GetPreferredMode`, usually 480i on NTSC consoles and
576i on PAL consoles.

The 240p targets force `TVNtsc240Ds`. The PAL 240p targets force `TVPal264Ds`
(264p at 50 Hz). These modes are intended for CRTs and retro scalers such as
RetroTINK or OSSC. Many modern flat panels reject the signal entirely.

## Storage Layout

The port can run from SD or USB. At boot it mounts FAT storage, probes
`sd:/quake3` first, then `usb:/quake3`, and uses the device that contains the
`quake3` directory for `fs_basepath`, `fs_homepath`, qkey, configs, and logs.

Copy the produced `boot.dol` to the matching Homebrew Channel app directory on
the same device.

```text
<dev>:/
|-- apps/
|   |-- ioquake3/
|   |   |-- boot.dol       <- build/boot.dol
|   |   `-- meta.xml
|   |-- openarena/
|   |   |-- boot.dol       <- build_oa/boot.dol
|   |   `-- meta.xml
|   `-- teamarena/
|       |-- boot.dol       <- build_ta/boot.dol
|       `-- meta.xml
`-- quake3/
    |-- baseq3/            <- Q3A data
    |   |-- pak0.pk3
    |   |-- pak1.pk3
    |   |-- ...
    |   `-- pak8.pk3
    |-- baseoa/            <- Open Arena data
    |   |-- pak0.pk3
    |   `-- ...
    `-- missionpack/       <- Team Arena data
        |-- pak0.pk3
        `-- ...
```

Data requirements:

- Q3A: `<dev>:/quake3/baseq3/pak*.pk3`
- Open Arena: `<dev>:/quake3/baseoa/pak*.pk3`
- Team Arena: `<dev>:/quake3/baseq3/pak*.pk3` plus `<dev>:/quake3/missionpack/pak*.pk3`

Use `sd:` for SD card installs or `usb:` for USB installs. USB boot is also the
normal path for Wii Mini, which has no SD slot.

## Debug Builds

Debug targets enable logging on the active data device:

- `<dev>:/quake3/boot.txt`: boot timeline; the last line is usually the failing call
- `<dev>:/quake3/diag.txt`: engine diagnostics and `Sys_Error` traces
- `<dev>:/quake3/crash.txt`: early checkpoint confirming storage was mounted

`WII_GX_PROFILE=1` writes GP counter windows to `diag.txt` in debug builds.

## Controls

All available input methods are active. USB devices must be connected before
boot; hot-plug is not supported.

Controller priority is: Wii U GamePad, Classic Controller, Wiimote + Nunchuk,
then GameCube controller. If the Wiimote disconnects, input falls back to the
GameCube controller.

Per-controller bindings are saved separately:

- `wii_binds_gc.cfg`
- `wii_binds_wm.cfg`
- `wii_binds_cc.cfg`
- `wii_binds_drc.cfg`

### GameCube Controller

| Input | In-game action |
|---|---|
| Left stick | Move |
| C-stick | Look |
| R trigger | Fire |
| L trigger | Zoom |
| A | Jump |
| B | Crouch |
| X | Next weapon |
| Y | Previous weapon |
| Z | Use item |
| D-pad up | Scoreboard |
| D-pad left/right | Strafe |
| Start | Menu |

| Input | Menu action |
|---|---|
| Left stick / C-stick | Move cursor |
| A | Confirm |
| B | Back |
| X | Click |
| Y | Toggle console |
| R trigger | Click |
| D-pad | Arrow keys |

The GameCube controller has no HOME button. Use Start to open the menu and
quit from there, or use the Wii Power/Reset buttons.

### Wiimote + Nunchuk

| Input | In-game action |
|---|---|
| Nunchuk stick | Move |
| IR pointer | Aim |
| B trigger | Fire |
| A | Jump |
| Nunchuk Z | Zoom |
| Nunchuk C | Crouch |
| + | Menu |
| - | Scoreboard |
| D-pad left/right | Previous / next weapon |
| 1 | Walk |
| HOME | Exit to Homebrew Channel |

| Input | Menu action |
|---|---|
| IR pointer | Move cursor |
| Nunchuk stick | Move cursor fallback |
| A | Confirm |
| B | Back |
| + | Back |
| 1 | Click |
| Nunchuk Z | Click |
| D-pad | Arrow keys |

### Classic Controller / Pro Controller

| Input | In-game action |
|---|---|
| Left stick | Move |
| Right stick | Look |
| ZR | Fire |
| L | Walk |
| R | Use item |
| A | Jump |
| B | Crouch |
| ZL | Zoom |
| X | Next weapon |
| Y | Previous weapon |
| + | Menu |
| - | Scoreboard |
| D-pad up/down | Move forward/back |
| D-pad left/right | Strafe |

| Input | Menu action |
|---|---|
| Left stick | Move cursor |
| A | Confirm |
| B | Back |
| ZR | Click |
| D-pad | Arrow keys |

### Wii U GamePad

The Wii U GamePad is available on vWii. On a standard Wii, detection fails
safely and has no effect.

| Input | In-game action |
|---|---|
| Left stick | Move |
| Right stick | Look |
| ZR | Fire |
| L | Walk |
| R | Use item |
| A | Jump |
| B | Crouch |
| ZL | Zoom |
| X | Previous weapon |
| Y | Next weapon |
| + | Menu |
| - | Scoreboard |
| D-pad up/right | Next weapon |
| D-pad down/left | Previous weapon |
| HOME | Exit to Homebrew Channel |

| Input | Menu action |
|---|---|
| Left stick | Move cursor |
| A | Confirm |
| B | Back |
| + | Back |
| ZR | Click |
| D-pad | Arrow keys |

### USB Keyboard

Use a USB keyboard to type console commands, server IPs, and chat messages.
Press `~` to toggle the console. Letters, numbers, symbols, F1-F12, arrows,
numpad keys, and modifiers are mapped.

### USB Mouse

Use a USB mouse for desktop-style aiming. Left, right, middle, and wheel input
are supported.

## Bots

Bot AI works in local and hosted games. Use the in-game menus to start a local
match and add bots. The build uses `MAX_CLIENTS=8`, so up to 7 bots can join
with one local player.

## Runtime Defaults

The Wii boot cmdline sets conservative defaults before `Com_Init`, including:

- `com_hunkMegs`: dynamic MEM2 bump minus 1 MB
- `com_zoneMegs 8`
- `com_soundMegs 2`
- `com_maxfps`: from `WII_MAXFPS`, default 60
- `r_fastsky 0`
- `r_lodbias 1`
- `r_picmip 2`
- `r_dynamic 0`
- `r_flares 0`
- `sv_maxclients 8`
- `cl_allowDownload 1`

Controller, joystick, and IR cvars that do not need to exist before
`Com_Init` are set later by `Wii_Input_SetCvars()` to keep the startup cmdline
under `MAX_CONSOLE_LINES=32`.

## Memory Budget

| Region | Size | Location | Notes |
|---|---:|---|---|
| Hunk (`com_hunkMegs`) | about 32 MB for Q3A/OA; dynamic for TA | MEM2 bump | Q3A/OA reserve up to a 33 MB MEM2 bump, then give hunk one MB less. TA uses `min(Arena2 - 18 MB, 40 MB)` for the MEM2 bump to leave sbrk headroom. |
| Zone (`com_zoneMegs`) | 8 MB | sbrk | Dynamic allocations and zlib inflate. |
| Sound (`com_soundMegs`) | 2 MB | sbrk | Audio pool; mono sounds are stored ADPCM 4:1 on Wii. |
| GX FIFO | 256 KB | MEM1 | Command buffer. |
| Framebuffers | 2 XFBs | MEM1 | Allocated from the selected video mode; smaller in 240p/264p modes. |
| Stack | 512 KB | MEM1 | Overridden from the small libogc default. |

Large `calloc` requests are wrapped and served from the MEM2 bump when
possible. Smaller heap allocations stay in the normal sbrk arena.

The default `WII_VM_NATIVE=1` build moves compiled VM code off the hunk through
tracked `mmap`/`munmap` reuse slots. It adds a short compile stall at VM load
time and does not improve framerate, but it avoids interpreter-mode hunk OOMs
on larger maps. The JIT compiler's transient allocations use a 512 KB chunked
arena so large OA/TA QVMs do not burn sbrk on allocator overhead.

## Code Layout

- `code/sys/`, `code/audio/`, `code/input/`, `code/renderer/`: Wii port layer
- `code/qcommon/`, `code/client/`, `code/server/`, `code/botlib/`: vendored and patched ioQ3 engine code
- `code/renderergl1/`, `code/renderercommon/`: renderer frontend shared with the Wii backends
- `code/cgame/`, `code/game/`, `code/ui/`: VM-side source; engine builds use the headers, while shipped `.pk3` files provide QVM bytecode
- `code/sys/wii_platform.h`: force-included before every translation unit for Wii platform identity and memory/network tuning
- `libs/opengx/`: prebuilt patched OpenGX library and headers for the fallback renderer
- `libs/wiidrc/`: Wii U GamePad support library and headers

## Known Issues

- Browsing many player models before starting a match can exhaust hunk memory
  later in-game and trigger "Memory is low. Using deferred model." The hunk is
  a bump allocator and menu-loaded model meshes are not freed until a map load.
- Team Arena has the tightest memory budget. It has its own build flavor and
  dynamic MEM2 bump sizing, but large TA loads can still expose memory pressure
  before Q3A or OA do.
- `r_measureOverdraw` is not supported on the native GX backend because there
  is no stencil path there.

## License

ioQuake3 is GPLv2. This port layer is also GPLv2. See `LICENSE.txt`.
