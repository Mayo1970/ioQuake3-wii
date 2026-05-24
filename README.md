# ioQuake3-Wii

A port of [ioQuake3](https://github.com/ioquake/ioq3) to the Nintendo Wii,
using devkitPPC + libogc and [OpenGX](https://github.com/devkitPro/opengx)
(OpenGL 1.x to GX translation layer).

## Status

- Boots, connects to servers, loads maps, enters gameplay
- Networking works (Wi-Fi, LAN discovery, internet server browser, content downloads)
- Background music and cinematic playback
- Wii Pro/Classic and GameCube controller support
- Wiimote + Nunchuk with IR aim
- USB keyboard and mouse support
- Bot support (AI opponents, works offline and on hosted servers)
- Local server hosting
- Optional Open Arena standalone build (`make oa dol`)
- Optional 240p / 264p video output for CRTs and retro scalers

## Prerequisites (Windows)

### 1. Install devkitPro

1. Download the devkitPro installer:
   https://github.com/devkitPro/installer/releases/latest
2. Run it. When asked which packages to install, select:
   - **devkitPPC** (the PowerPC cross-compiler)
   - **Wii Libraries** (`wii-dev`)
   - **libfat-ogc**, **libogc**, **wiiuse**, **asndlib** are included in wii-dev
3. Install zlib:
   ```
   pacman -S ppc-zlib
   ```
4. Accept the default install path (`C:/devkitPro`).
5. The installer sets `DEVKITPRO` and `DEVKITPPC` environment variables
   automatically. Open a new terminal and verify:
   ```
   echo %DEVKITPRO%    → C:/devkitPro
   echo %DEVKITPPC%    → C:/devkitPro/devkitPPC
   ```

### 2. Install MSYS2 or use the devkitPro shell

The devkitPro installer ships MSYS2. Use the **MSYS2 devkitPro shell**
(Start menu → devkitPro → MSYS2) for all build commands. It sets
`DEVKITPRO`, `DEVKITPPC`, and the toolchain `PATH` correctly. Plain Git Bash
or MSYS does not, and the build will error out.

---

## Building

From the devkitPro MSYS2 shell, in the repo root:

```bash
make dol              # Quake 3           → build/boot.dol
make oa dol           # Open Arena        → build_oa/boot.dol
make debug dol        # Q3 debug build    → build/boot.dol
make oa-debug dol     # OA debug build    → build_oa/boot.dol

make 240p dol         # Q3  240p NTSC     → build/boot.dol
make 240p-pal dol     # Q3  264p PAL      → build/boot.dol
make oa-240p dol      # OA  240p NTSC     → build_oa/boot.dol
make oa-240p-pal dol  # OA  264p PAL      → build_oa/boot.dol

make all-flavors dol          # Q3 + OA release
make all-flavors-240p dol     # Q3 + OA 240p NTSC
make all-flavors-240p-pal dol # Q3 + OA 264p PAL

make clean       # Clean Q3 build dir (build/)
make oa clean    # Clean OA build dir (build_oa/)
```

Debug builds enable SD card diagnostic logging to `sd:/quake3/`.

### 240p / 264p mode

By default the port uses whatever video mode the Wii's video system reports
(`VIDEO_GetPreferredMode`), which is 480i on NTSC consoles and 576i on PAL.

The `240p` / `240p-pal` targets switch to a single-field low-line mode:
`TVNtsc240Ds` (240p, 60 Hz) or `TVPal264Ds` (264p, 50 Hz).

Benefits over interlaced: cleaner signal on CRTs and retro scalers (RetroTINK,
OSSC, etc.), frees ~1.2 MB of MEM1 (XFBs shrink from 640×480 to 640×240),
and eliminates the EFB→XFB vertical upscale pass.

**Do not use on modern flat panels** — most will reject the signal entirely.

---

## SD card layout

```
SD:/
├── apps/
│   ├── ioquake3/
│   │   ├── boot.dol      ← build/boot.dol
│   │   └── meta.xml
│   └── openarena/        ← OA build only
│       ├── boot.dol      ← build_oa/boot.dol
│       └── meta.xml
└── quake3/
    ├── baseq3/           ← Q3 data
    │   ├── pak0.pk3      ← from your Quake III Arena disc / purchase
    │   ├── pak1.pk3
    │   ├── ...
    │   └── pak8.pk3
    └── baseoa/           ← OA data
        ├── pak0.pk3      ← from your Open Arena install
        └── ...
```

**You need the original Quake III Arena data files** (`pak0.pk3` through
`pak8.pk3`). The demo pk3 files will also work for testing.

---

## Controls

All input methods are active simultaneously. Use whichever controller you
prefer, or combine them (e.g. GC controller for movement + USB mouse for
aiming). USB devices must be connected at boot (no hot-plug).

### GameCube controller

#### In-game

| Input | Action |
|---|---|
| Left stick | Move |
| C-stick | Look  |
| **R** trigger | Fire |
| **L** trigger | Walk |
| **A** | Jump |
| **B** | Crouch |
| **X** | Previous weapon |
| **Y** | Next weapon |
| **Z** | Zoom |
| D-pad up | Scoreboard |
| D-pad down | Fire (alt) |
| D-pad left/right | Prev/next weapon |
| **Start** | Menu (Escape) |

#### Menus

| Input | Action |
|---|---|
| Left stick / C-stick | Move cursor |
| **A** | Confirm (Enter) |
| **B** | Back (Escape) |
| **X** | Click |
| **Y** | Toggle console |
| D-pad | Arrow keys |
| **R** trigger | Click |
| **Start** | Escape |

> The GC controller has no HOME button. Use Start to open the menu and quit
> from there, or use the Wii's Power/Reset buttons to return to the
> Homebrew Channel.

### Wiimote + Nunchuk

IR pointer aiming with nunchuk stick movement. If the Wiimote disconnects,
input falls back to the GameCube controller automatically.

#### In-game

| Input | Action |
|---|---|
| Nunchuk stick | Move (forward/back + strafe) |
| IR pointer | Aim (yaw + pitch) |
| **B** (trigger) | Fire |
| **A** | Jump |
| Nunchuk **Z** | Zoom |
| Nunchuk **C** | Crouch |
| **+** | Menu (Escape) |
| **-** | Scoreboard |
| D-pad up/down | Next/prev weapon |
| D-pad left/right | Prev/next weapon (alt) |
| **1** | Walk |
| **HOME** | Exit to Homebrew Channel |

#### Menus

| Input | Action |
|---|---|
| IR pointer | Move cursor |
| Nunchuk stick | Move cursor (fallback) |
| **A** | Confirm (Enter) |
| **B** | Back (Escape) |
| **+** | Escape |
| **1** | Click |
| Nunchuk **Z** | Click |
| D-pad | Arrow keys |

### Classic Controller

Plug the Pro/Classic Controller into a Wiimote; it takes priority over the Wiimote's own input.

#### In-game

| Input | Action |
|---|---|
| Left stick | Move (forward/back + strafe) |
| Right stick | Look (yaw + pitch) |
| **ZR** | Fire |
| **L** | Walk |
| **R** | Use item |
| **A** | Jump |
| **B** | Crouch |
| **ZL** | Zoom |
| **X** | Previous weapon |
| **Y** | Next weapon |
| **+** | Menu (Escape) |
| **-** | Scoreboard |
| D-pad up/down | Next/prev weapon |
| D-pad left/right | Prev/next weapon (alt) |

#### Menus

| Input | Action |
|---|---|
| Left stick | Move cursor |
| **A** | Confirm (Enter) |
| **B** | Back (Escape) |
| **+** | Escape |
| **ZR** | Click |
| D-pad | Arrow keys |

### USB keyboard

Plug a standard USB keyboard into the Wii to type console commands, server
IPs, and chat messages. Press `~` (tilde) to toggle the Q3 console.

All standard keys are supported: letters, numbers, symbols, F1-F12, arrow
keys, numpad, and modifiers (Shift, Ctrl, Alt).

### USB mouse

Plug a USB mouse into the Wii for desktop-style aiming. Left/right/middle
buttons and scroll wheel are supported.

---

## Bots

Bot AI opponents work in both local and hosted games. Use the in-game
menus to start a local match and add bots (Start New Server → select map
→ add bots). Up to 7 bots can be active at once (`MAX_CLIENTS=8`, minus
the local player).

---

## Memory budget

| Region | Size | Location | Notes |
|---|---|---|---|
| Hunk (`com_hunkMegs`) | up to 32 MB | MEM2 (top) | Maps, shaders, models. Sized dynamically from MEM2 available at boot |
| Zone (`com_zoneMegs`) | 8 MB | sbrk (MEM2) | Dynamic allocs, zlib inflate |
| Sound (`com_soundMegs`) | 4 MB | sbrk (MEM2) | Audio buffers |
| sbrk heap | ~19 MB | MEM2 (bottom) | OpenGX textures, memalign, smaller allocs |
| GX FIFO | 256 KB | MEM1 | Command buffer |
| Framebuffers | ~2.4 MB | MEM1 | Two XFB at 640x480 |
| Stack | 512 KB | MEM1 | Overridden from 16 KB default |
## Known issues

- [ ] Missing Q3 logo at the top of the main menu
- [ ] Missing player model in the Player Setup menu
- [ ] No mod support (loading mods such as Team Arena crashes)

---

## License

ioQuake3 is GPLv2. This port layer is also GPLv2. See `LICENSE`.
