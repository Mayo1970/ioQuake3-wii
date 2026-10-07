# ioQuake3-Wii

A port of [ioQuake3](https://github.com/ioquake/ioq3) to the Nintendo Wii,
using devkitPPC/libogc and a custom GL-to-GX direct rendering backend (an
OpenGX fixed-function fallback is also available). Boots from the Homebrew
Channel as `boot.dol`.

Five build flavors are produced from the same source tree:

| Variant | HBC app dir | Output | Game dir on SD/USB |
|---|---|---|---|
| ioQuake3 (Q3A) | `apps/ioquake3/` | `build/boot.dol` | `baseq3` |
| Open Arena | `apps/openarena/` | `build_oa/boot.dol` | `baseoa` |
| Team Arena | `apps/teamarena/` | `build_ta/boot.dol` | `baseq3` + `missionpack` |
| CLASSIC (Dreamcast crossplay) | `apps/ioquake3-classic/` | `build_classic/boot.dol` | `baseq3` (pak0–pak2 only) |
| Mod Selector | `apps/ioquake3-modselect/` | `build_modsel/boot.dol` | `baseq3` + any picked mod folder |

## Status

- Native GX renderer by default, with legacy OpenGX fallback
- Networking works
- Wiimote + Nunchuk with IR aim
- Classic Controller / Classic Controller Pro support
- Wii U GamePad support on vWii [although a Wii-U port exists](https://github.com/Mayo1970/ioQuake3-U)
- GameCube controller support
- Wired USB HID gamepad support (see Controls below)
- USB keyboard and mouse support
- Per-controller button binding persistence across reboots
- Quake III Arena and Open Arena build flavors
- **Team Arena** build flavor: works on real hardware, including the RoQ
  cinematics and pure online servers. Included in future releases.
- **CLASSIC** build flavor: crossplay with real Dreamcast Q3A (protocol 43,
  1.16n-era) servers
- **Mod Selector** build flavor: boot-time menu to pick any `baseq3`-compatible
  mod folder present on the data device
- Boot-time video mode selection — every build contains all three: default,
  240p NTSC, and 264p PAL output for CRTs and retro scalers, chosen with a
  d-pad prompt at boot

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

4. Game data: you need to bring your own `.pk3` files — none are included in
   this repo. See [INSTALLATION.md](INSTALLATION.md) for where to get them.

---

## Building

All ioQ3 sources are vendored under `code/` (already patched for Wii) — no
separate patch step, no external `../ioq3` checkout, no submodule. Fresh
clone → `make dol` builds. Run these from the repository root in the
devkitPro MSYS2 shell.

| Command | Output |
|---|---|
| `make dol` | Q3A release: `build/boot.dol` |
| `make oa` | Open Arena release: `build_oa/boot.dol` |
| `make ta` | Team Arena release: `build_ta/boot.dol` |
| `make classic` | CLASSIC (Dreamcast crossplay) release: `build_classic/boot.dol` |
| `make modsel` | Mod Selector release: `build_modsel/boot.dol` |
| `make debug` | Q3A debug build with device-root logging: `build/boot.dol` |
| `make oa-debug` | OA debug build with device-root logging: `build_oa/boot.dol` |
| `make ta-debug` | TA debug build with device-root logging: `build_ta/boot.dol` |
| `make classic-debug` | CLASSIC debug build with device-root logging: `build_classic/boot.dol` |
| `make modsel-debug` | Mod Selector debug build with device-root logging: `build_modsel/boot.dol` |
| `make all-flavors` | Q3A + OA + TA + CLASSIC release builds |
| `make clean` | Remove build output directories and copied zlib headers |

### Optional build flags

| Flag | Default | Description |
|---|---|---|
| `WII_MAXFPS=30` | `60` | Lower the in-game framerate cap if 60 FPS is unstable on a target console. Menus and loading run at 60 regardless. |
| `WII_VM_NATIVE=0` | `1` | Use the bytecode interpreter instead of the native-PPC QVM JIT. The interpreter can need several more MB of hunk and may OOM on larger maps. OA and TA always use `0`. |
| `WII_FSGAME=modname` | empty | Boot directly into a mod by setting `fs_game`. Superseded by `make modsel` for picking a mod at boot instead of build time. |
| `WII_OPENGX=1` | `0` | Build the legacy OpenGX renderer instead of native GX. Run `make clean` before switching renderer backends. |
| `WII_NATIVE_GX=0` | `1` | Equivalent OpenGX fallback override. Run `make clean` before switching. |
| `WII_GX_PROFILE=1` | `0` | Enable the GX bottleneck profiler. Use with `make debug` so output reaches `diag.txt`. |

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

---

## Installing on Wii

See **[INSTALLATION.md](INSTALLATION.md)** for the full step-by-step,
including where to get each build's required game files and exactly where to
place them on your SD card or USB drive.

Short version: copy the built `boot.dol` into the matching
`<dev>:/apps/<name>/` folder from the variant table above, and drop your
`.pk3` files under `<dev>:/quake3/<gamedir>/`. `<dev>` is `sd:` or `usb:` — at
boot the port mounts FAT storage, probes `sd:/quake3` first, then
`usb:/quake3`, and uses whichever device has the `quake3` directory for
`fs_basepath`, `fs_homepath`, the qkey, configs, and logs. USB boot is also
the normal path for Wii Mini, which has no SD slot.

### Installing custom mods

The engine respects `fs_game`. Drop a mod under its own folder alongside
`baseq3/`:

```
<dev>:/quake3/excessive/pak0.pk3
```

Then either use `make modsel` to pick it from the boot-time menu, rebuild
with `WII_FSGAME=excessive`, or set it from the in-game console
(`\fs_game excessive`, then `\vid_restart`).

---

## Debug Builds

Debug targets enable logging on the active data device:

- `<dev>:/quake3/boot.txt`: boot timeline; the last line is usually the failing call
- `<dev>:/quake3/diag.txt`: engine diagnostics and `Sys_Error` traces
- `<dev>:/quake3/crash.txt`: early checkpoint confirming storage was mounted

`WII_GX_PROFILE=1` writes GP counter windows to `diag.txt` in debug builds.

---

## Controls

All available input methods are active simultaneously. A wired USB HID
gamepad, if plugged in, takes top priority over every other controller and
can be hot-plugged/unplugged during play (detected by periodic polling, not
instant). USB keyboard and mouse must be connected before boot.

Controller priority is: wired USB HID gamepad, Wii U GamePad, Classic
Controller, Wiimote + Nunchuk, then GameCube controller. If the Wiimote
disconnects, input falls back to the GameCube controller.

Per-controller bindings are saved separately:

- `wii_binds_gc.cfg`
- `wii_binds_wm.cfg`
- `wii_binds_cc.cfg`
- `wii_binds_drc.cfg`
- `wii_binds_usb.cfg`

### Binding buttons from the Controls menu

Every button can be rebound in-game from **Setup → Controls**. Because A, B
and the D-pad double as menu-navigation keys, binding uses a modifier:

1. Select the action you want to bind and activate it ("press key" appears).
2. **Hold the modifier button** — **Minus** on Wiimote / Classic Controller /
   Wii U GamePad, **Z** on a GameCube pad, **Back/Share** on a USB pad — and
   press the button you want to assign. That's it; works for A, B, D-pad,
   Start/Plus and the analog triggers too.
3. To bind the modifier button itself, **tap it on its own** (press and
   release while the menu is waiting for a key).

### Wired USB HID Gamepad

Confirmed working on real hardware:

| Controller | Notes |
|---|---|
| PS3 (Sixaxis / DualShock 3) | |
| PS4 (DualShock 4), v1 and v2 | |
| DualSense (PS5) | |
| Switch Pro Controller | |

Present in the device profile table but **not functional** — every one
crashes the console the instant it sends its first real input report, a
libogc/IOS limitation of the vendor-specific USB class these pads use (see
`AGENTS.md`/`CLAUDE.md` for the investigation):

- Xbox One / Xbox One S / Xbox One Elite / Xbox One Elite 2
- Xbox Series X/S
- Xbox 360 (wired)

Each brand's raw report is normalized into one brand-independent virtual
gamepad (face buttons, shoulders/triggers, two sticks, D-pad), so binds
behave the same no matter which supported pad is plugged in. Bindings are
saved to `wii_binds_usb.cfg`.

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

## Team Arena Build

`make ta` builds the Team Arena flavor. It needs `baseq3/` plus
`<dev>:/quake3/missionpack/pak*.pk3` and installs to `<dev>:/apps/teamarena/`.
It works on real hardware, including the RoQ cinematics and pure online
servers, and is included in future releases.

Team Arena's own game code (cgame, game and ui) is compiled to PowerPC and
linked into the DOL instead of running as QVM bytecode. As QVMs it needed
about 12 MB of hunk; as native code it takes about 8.4 MB of static memory,
fixed at link time. This is what makes Team Arena fit. Mods that run on top of
Team Arena still load their own QVMs, through the interpreter. On a pure
server, the built-in code runs only when the server uses the stock Team Arena
QVMs; otherwise the server's QVM loads.

## CLASSIC Build

`make classic` builds a separate flavor for crossplay with original Sega
Dreamcast Quake III Arena (protocol 43, 1.16n-era). It uses only
`pak0-pak2.pk3` from `baseq3/` — the extra assets/QVM changes needed for
protocol-43 compatibility ship as `zpack-classic.pk3`, embedded in the DOL and
auto-extracted to `baseq3/` on first boot. This flavor can join real
Dreamcast-hosted servers; it does not change how the standard Q3A/OA builds
behave, and nothing about it affects those other flavors.

## Mod Selector

`make modsel` builds a flavor that shows a boot-time menu instead of always
loading `baseq3`. It scans `<dev>:/quake3/` for any folder (other than
`baseq3`, `baseoa` and `missionpack`, which have their own builds) that
contains at least one `.pk3` file and lets you pick one with
the d-pad before the engine starts. Picking "baseq3 only" boots normally.
This replaces having to bake a mod name in with `WII_FSGAME` at build time.

---

## Memory Budget

The Wii has 88 MB of RAM: 24 MB of MEM1 and 64 MB of MEM2. IOS reserves part
of MEM2, so about 52 MB of MEM2 (Arena2) is left for the game under IOS58; the
exact amount varies by IOS. At boot, `Wii_MEM2_Init` measures Arena2 and
reserves a bump region at its top for the hunk. Everything else comes from the
normal sbrk heap: the MEM1 that the DOL image does not use, plus the MEM2
below the bump.

| Region | Q3A, OA, CLASSIC, Mod Selector | Team Arena | Location |
|---|---:|---:|---|
| MEM2 bump | 33 MB | `min(Arena2 - 27 MB, 40 MB)` | MEM2, top |
| Hunk (`com_hunkMegs`) | 32 MB | 25 MB (at 52 MB Arena2) | MEM2 bump |
| sbrk heap in MEM2 | about 19 MB (at 52 MB Arena2) | 27 MB | MEM2, below the bump |
| Zone (`com_zoneMegs`) | 8 MB | 8 MB | sbrk |
| Sound pool (`com_soundMegs 2`) | about 6 MB | about 6 MB | sbrk |
| Native game code (cgame, game, ui) | none | about 8.4 MB | MEM1, DOL image |
| SD/USB read cache | 512 KB (Q3A release), 128 KB (others) | 128 KB | sbrk |
| GX FIFO | 256 KB | 256 KB | MEM1 |
| Framebuffers | 2 XFBs | 2 XFBs | MEM1 |
| Stack | 512 KB | 512 KB | MEM1 |

- **Non-TA builds** use a fixed 33 MB bump (less only if Arena2 is smaller).
  The hunk gets the bump minus 1 MB; that 1 MB is headroom for JIT code
  buffers.
- **Team Arena** keeps 27 MB of MEM2 for sbrk: 18 MB for zone, sound and
  overhead such as Bluetooth/WPAD, plus 9 MB to replace the MEM1 that its
  native game code takes. It runs no JIT, so the hunk gets the whole bump.
- **Textures** are stored as 16-bit RGB565/RGB5A3, or as 4 bpp CMPR
  (DXT1-style) for opaque art, encoded on the CPU at load time. Textures with
  alpha stay 16-bit (GX has no DXT5) and lightmaps are never compressed.
  Texture memory comes from sbrk and is freed on map change.
- **Sounds** are stored ADPCM 4:1 (mono), so a full bot match fits the pool
  without mid-game reloads.

Large `calloc` requests (16 MB or more) are served from the MEM2 bump; this
is how the hunk lands there. Smaller heap allocations stay in sbrk.

Q3A, CLASSIC and Mod Selector run QVMs with the native-PPC JIT
(`WII_VM_NATIVE=1`). The compiled code lives in the MEM2 bump through tracked
`mmap`/`munmap` reuse slots, not on the hunk, so large maps that run out of
hunk under the interpreter still load. The JIT adds a short compile stall at
each map load and does not improve framerate. Open Arena always uses the
interpreter: three resident JIT code buffers for its larger QVMs leave too
little memory at map load. Team Arena runs its own game code natively and
interprets only mod QVMs.

## Code Layout

- `code/sys/`, `code/audio/`, `code/input/`, `code/renderer/`: Wii port layer
- `code/qcommon/`, `code/client/`, `code/server/`, `code/botlib/`: vendored and patched ioQ3 engine code
- `code/renderergl1/`, `code/renderercommon/`: renderer frontend shared with the Wii backends
- `code/cgame/`, `code/game/`, `code/ui/`: VM-side source. Q3A, OA, CLASSIC and Mod Selector builds use only the headers; their QVM bytecode comes from the `.pk3` files. The TA build compiles this code to PowerPC and links it into the DOL
- `code/sys/wii_modules.c`, `code/sys/wii_module.ld`: TA native game module loader and link script
- `ui/menudef.h`: menu definitions header shared by the game code
- `code/sys/wii_platform.h`: force-included before every translation unit for Wii platform identity and memory/network tuning
- `libs/opengx/`: prebuilt patched OpenGX library and headers for the fallback renderer
- `libs/wiidrc/`: Wii U GamePad support library and headers
- `code/input/wii_usb_hid.c`/`.h`: wired USB HID gamepad support (raw libogc USB stack)
- `fixes/baseq3/zpack-classic.pk3`: CLASSIC-only assets/QVMs, embedded into the DOL and auto-extracted at boot

## Known Issues

- Browsing many player models before starting a match can exhaust hunk memory
  later in-game and trigger "Memory is low. Using deferred model." The hunk is
  a bump allocator and menu-loaded model meshes are not freed until a map load.
- Wired USB HID gamepads in the Xbox family (One/Series/360) are recognized by
  VID/PID but crash the console on their first real input report — a
  libogc/IOS limitation of the vendor-specific USB class those pads use, not
  fixable from this codebase.
- `r_measureOverdraw` is not supported on the native GX backend because there
  is no stencil path there.
- `r_dynamiclight 0` hard-crashes release builds on the native GX backend (a
  debug build with the same cmdline boots fine). The boot cmdline sets the
  cvar under its old misspelled name (`r_dynamic`) as a workaround, which
  leaves dynamic lights on at the engine default. Root cause not yet isolated.
- Some mirror surfaces can render black from certain viewing angles on the
  native GX backend; under investigation.
- The default boot video mode (true interlaced, whatever
  `VIDEO_GetPreferredMode` returns) can crop the top of the picture on some
  TVs/upscalers over composite or RF. This happens in the display's own
  deinterlacer, after this port's output, so it can't be fixed render-side.
  Pick the 240p or 264p mode at the boot prompt instead if you see this.

---

## Credits

- **[ioQuake3](https://github.com/ioquake/ioq3)** — the upstream engine this port is based on.
- **[devkitPro](https://devkitpro.org/)** — the devkitPPC toolchain and libogc runtime library this port builds and runs against.
- **[OpenGX](https://github.com/devkitPro/opengx)** — the GL-to-GX translation layer used by the `WII_OPENGX=1` fallback renderer (patched in-tree, see `libs/opengx/`).
- **[libwiidrc](https://github.com/FIX94/libwiidrc)** — Wii U GamePad (DRC) input support on vWii (`libs/wiidrc/`).
- **[Lilium Arena Classic](https://github.com/clover-moe/lilium-arena-classic)** (clover-moe / clover-leaf) — reverse-engineered Quake III Arena protocol-43 / Dreamcast compatibility reference used while building the CLASSIC build's crossplay layer.

---

## AI disclosure

Parts of this port were developed with the assistance of **Claude** (Anthropic). AI was used for code generation, debugging, porting guidance, and documentation. All AI-generated code was reviewed and tested on hardware before inclusion.

---

## License

ioQuake3 is GPLv2. This port layer is also GPLv2. See `LICENSE.txt`.