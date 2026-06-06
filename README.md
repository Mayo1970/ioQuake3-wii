# ioQuake3-Wii

A port of [ioQuake3](https://github.com/ioquake/ioq3) to the Nintendo Wii,
using devkitPPC + libogc and [OpenGX](https://github.com/devkitPro/opengx)
(OpenGL 1.x to GX translation layer).

## Status

- Boots, connects to servers, loads maps, enters gameplay
- Networking works (Wi-Fi, LAN discovery, internet server browser, content downloads)
- Background music and cinematic playback
- Wiimote + Nunchuk with IR aim
- Wii Classic / Pro Controller support
- Wii U GamePad support
- GameCube controller support
- USB keyboard and mouse support
- Bot support (AI opponents, works offline and on hosted servers)
- Local server hosting
- Per-controller button binding persistence across reboots
- Optional Open Arena standalone build (`make oa`)
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

### 2. Use the devkitPro MSYS2 shell

The devkitPro installer ships MSYS2. Use the **MSYS2 devkitPro shell**
(Start menu → devkitPro → MSYS2) for all build commands. It sets
`DEVKITPRO`, `DEVKITPPC`, and the toolchain `PATH` correctly. Plain Git Bash
or MSYS does not, and the build will error out.

---

## Building

From the devkitPro MSYS2 shell, in the repo root:

```bash
# Standard 480i builds
make dol              # Quake 3 Arena      → build/boot.dol
make oa               # Open Arena         → build_oa/boot.dol

# Debug builds (enable SD card diagnostic logging)
make debug            # Q3A debug          → build/boot.dol
make oa-debug         # OA  debug          → build_oa/boot.dol

# 240p NTSC builds (CRT / retro scaler only — rejects on flat panels)
make 240p             # Q3A 240p NTSC      → build/boot.dol
make oa-240p          # OA  240p NTSC      → build_oa/boot.dol

# 264p PAL builds
make 240p-pal         # Q3A 264p PAL       → build/boot.dol
make oa-240p-pal      # OA  264p PAL       → build_oa/boot.dol

make clean       # Clean all build dirs
```

### Optional build flags

| Flag | Default | Description |
|---|---|---|
| `WII_MAXFPS=60` | `30` | In-game framerate cap. Use 60 on Wii U / vWii. Menus always run at 60 regardless. |
| `WII_VM_NATIVE=1` | `0` | Enable the native-PPC JIT for QVMs. No measurable FPS gain (render-bound), kept for future investigation. |

Example: `make WII_MAXFPS=60 dol`

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
│   └── openarena/
│       ├── boot.dol      ← build_oa/boot.dol
│       └── meta.xml
└── quake3/
    ├── baseq3/           ← Q3A data
    │   ├── pak0.pk3      ← from your Quake III Arena disc / purchase
    │   ├── pak1.pk3
    │   ├── ...
    │   └── pak8.pk3
    └── baseoa/           ← OA data only (baseq3/ not required)
        ├── pak0.pk3      ← from your Open Arena install
        └── ...
```

**Data requirements:**
- **Q3A**: `sd:/quake3/baseq3/pak*.pk3`
- **Open Arena**: `sd:/quake3/baseoa/pak*.pk3` (no `baseq3/` needed)

---

## Controls

All input methods are active simultaneously. Use whichever controller you
prefer, or combine them (e.g. GC controller for movement + USB mouse for
aiming). USB devices must be connected at boot (no hot-plug).

Input priority when multiple controllers are connected:
**Wii U GamePad → Classic Controller → Wiimote+Nunchuk → GameCube**

### GameCube controller

#### In-game

| Input | Action |
|---|---|
| Left stick | Move |
| C-stick | Look  |
| **R** trigger | Fire |
| **L** trigger | Zoom |
| **A** | Jump |
| **B** | Crouch |
| **X** | Next weapon |
| **Y** | Previous weapon |
| **Z** | Use item |
| D-pad up | Scoreboard |
| D-pad left/right | Strafe |
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
| D-pad left/right | Prev/next weapon |
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

### Classic Controller / Pro Controller

Plug the Classic Controller or Pro Controller into a Wiimote; it takes
priority over the Wiimote's own buttons and IR.

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
| **X** | Next weapon |
| **Y** | Previous weapon |
| **+** | Menu (Escape) |
| **-** | Scoreboard |
| D-pad up/down | Move forward/back |
| D-pad left/right | Strafe |

#### Menus

| Input | Action |
|---|---|
| Left stick | Move cursor |
| **A** | Confirm (Enter) |
| **B** | Back (Escape) |
| **ZR** | Click |
| D-pad | Arrow keys |

### Wii U GamePad (DRC)

Available on vWii only. The port detects the GamePad automatically at startup;
on a standard Wii the detection returns false and has no effect. Layout
mirrors the Classic Controller.

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
| **X** | Next weapon |
| **Y** | Previous weapon |
| **+** | Menu (Escape) |
| **-** | Scoreboard |
| D-pad up/down | Next/prev weapon |
| D-pad left/right | Prev/next weapon (alt) |
| **HOME** | Exit to Homebrew Channel |

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

All standard keys are supported: letters, numbers, symbols, F1–F12, arrow
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
| Hunk (`com_hunkMegs`) | up to 32 MB | MEM2 (top) | Maps, shaders, models. Sized dynamically from available MEM2 at boot |
| Zone (`com_zoneMegs`) | 8 MB | sbrk (MEM2) | Dynamic allocs, zlib inflate |
| Sound (`com_soundMegs`) | 4 MB | sbrk (MEM2) | Audio buffers |
| sbrk heap | ~19 MB | MEM2 (bottom) | OpenGX textures, memalign, smaller allocs |
| GX FIFO | 256 KB | MEM1 | Command buffer |
| Framebuffers | ~2.4 MB | MEM1 | Two XFB at 640×480 |
| Stack | 512 KB | MEM1 | Overridden from 16 KB default |

---

## Known issues

- Browsing many player models in the Player Model selection screen before
  starting a match can exhaust hunk memory in-game, causing "Memory is low.
  Using deferred model." messages. The hunk is a bump allocator reset only
  on map load — model meshes loaded in the menu are not freed between screens.

---

## License

ioQuake3 is GPLv2. This port layer is also GPLv2. See `LICENSE`.
