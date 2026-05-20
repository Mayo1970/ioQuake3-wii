# ioQuake3-Wii

A port of [ioQuake3](https://github.com/ioquake/ioq3) to the Nintendo Wii,
using devkitPPC + libogc and [OpenGX](https://github.com/devkitPro/opengx)
(OpenGL 1.x to GX translation layer).

## Status

- Boots, connects to servers, loads maps, enters gameplay
- All textures render correctly (map, icons, models, particles)
- Networking works (Wi-Fi, LAN discovery, internet server browser)
- GameCube controller support
- Wiimote + Nunchuk with IR pointer aiming
- Classic Controller support
- USB keyboard and mouse support
- Bot support (AI opponents, works offline and on hosted servers)
- Local server hosting

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

This repo is self-contained — the patched ioQuake3 source is vendored under
`code/qcommon/`, `code/client/`, `code/server/`, `code/botlib/`,
`code/renderergl1/`, `code/renderercommon/`. A prebuilt OpenGX library and
its headers are vendored under `libs/opengx/`. No sibling clone, patch step,
or OpenGX build is required.

From the devkitPro MSYS2 shell, in the repo root:

```bash
# Release build
make dol

# Debug build (enables SD card diagnostic logging to sd:/quake3/)
make DEBUG=1 dol
```

**Build flags:**

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `DEBUG` | `0`, `1` | `0` | Enable diagnostic logging to SD card |

**Output:** `build/ioquake3_wii.dol`

---

## SD card layout

```
SD:/
├── apps/
│   └── ioquake3/
│       ├── boot.dol      ← build/ioquake3_wii.dol
│       └── meta.xml
└── quake3/
    └── baseq3/
        ├── pak0.pk3      ← from your Quake III Arena disc / purchase
        ├── pak1.pk3
        ├── ...
        └── pak8.pk3
```

**You need the original Quake III Arena data files** (`pak0.pk3` through
`pak8.pk3`). The demo pk3 files will also work for testing.

---

## Controls

All input methods are active simultaneously. Use whichever controller you
prefer, or combine them (e.g. GC controller for movement + USB mouse for
aiming). USB devices must be connected at boot (no hot-plug).

### GameCube controller

Dual-stick FPS layout with analog movement and look. Buttons are rebindable
from the Q3 menu.

#### In-game

| Input | Action |
|---|---|
| Left stick | Move (forward/back + strafe) |
| C-stick | Look (yaw + pitch) |
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

Dual-stick FPS layout with analog movement and look. Plug the Classic
Controller into a Wiimote; it takes priority over the Wiimote's own input.

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
| Hunk (`com_hunkMegs`) | 32 MB | MEM2 (top) | Maps, shaders, models |
| Zone (`com_zoneMegs`) | 8 MB | MEM1 | Dynamic allocs, zlib inflate |
| Sound (`com_soundMegs`) | 4 MB | MEM1 | Audio buffers |
| sbrk heap | ~19 MB | MEM2 (bottom) | OpenGX textures, memalign |
| GX FIFO | 256 KB | MEM1 | Command buffer |
| Framebuffers | ~2.4 MB | MEM1 | Two XFB at 640x480 |
| Stack | 512 KB | MEM1 | Overridden from 16 KB default |
## Known missing pieces

- [ ] Missing Q3 logo at the top of the main menu
- [ ] Missing player model in the Player Setup menu
- [ ] No mod support (loading mods such as Team Arena crashes)

---

## License

ioQuake3 is GPLv2. This port layer is also GPLv2. See `LICENSE`.
