# Installation

This port produces **five separate Homebrew Channel apps** from one source
tree. Each is a standalone `boot.dol` with its own app folder — install only
the ones you want. None of them include the copyrighted game data; you
provide your own `.pk3` files, bought legally, and copy them to your SD card
or USB drive.

## What each build is

| Build | What it is | Notable features |
|---|---|---|
| **ioQuake3** | Quake III Arena | Full gameplay, bots, online/LAN multiplayer, mods via `fs_game` |
| **Open Arena** | Free/open Q3A-compatible game | Same engine features, uses free OA game data — no retail purchase needed |
| **CLASSIC** | Q3A speaking the original 1999 protocol | Crossplay with Sega Dreamcast community servers; no mod support, pak0–pak2 only |
| **Mod Selector** | Q3A with a boot-time mod picker | Scans the data device for extra mod folders and lets you choose one with the d-pad instead of hardcoding it into the build |

## Where to get the required files

Buy the base game(s) legally, then copy the `.pk3` files from your
install/disc into the paths below.

- **Quake III Arena** (needed for ioQuake3, Team Arena, CLASSIC, and Mod Selector): [Here](https://www.gog.com/en/game/quake_iii_arena)
- **Open Arena**: [Here](https://openarena.ws/)
- **Dreamcast community map pack** (optional, for CLASSIC): [Here](https://lvlworld.com/download/id:999)

---

## Common layout

Every build's `boot.dol` goes in its own folder under `<dev>:/apps/`, but
all builds share one game-data root, `<dev>:/quake3/`, so switching between
builds never requires re-copying paks. `<dev>` is `sd:` for an SD card or
`usb:` for a USB drive — USB is also the normal path for Wii Mini, which has
no SD slot.

```text
<dev>:/
|-- apps/
|   |-- ioquake3/
|   |   |-- boot.dol       <- build/boot.dol
|   |   `-- meta.xml
|   |-- openarena/
|   |   |-- boot.dol       <- build_oa/boot.dol
|   |   `-- meta.xml
|   |-- ioquake3-classic/
|   |   |-- boot.dol       <- build_classic/boot.dol
|   |   `-- meta.xml
|   `-- ioquake3-modselect/
|       |-- boot.dol       <- build_modsel/boot.dol
|       `-- meta.xml
`-- quake3/
    |-- baseq3/            <- Q3A data (also Team Arena, CLASSIC: pak0-pak2.pk3 only)
    |   |-- pak0.pk3
    |   |-- pak1.pk3
    |   |-- ...
    |   `-- pak8.pk3
    |-- baseoa/            <- Open Arena data
    |   |-- pak0.pk3
    |   `-- ...
    `-- <modname>/         <- any extra mod folder, picked at boot by Mod Selector
        `-- *.pk3
```

`meta.xml` is the standard Homebrew Channel app descriptor (name, author,
short description) shown in the HBC menu — any minimal one works; it is not
tracked in this repo, so create your own alongside each `boot.dol`.

---

## ioQuake3 (Q3A)

App folder: `apps/ioquake3/`

```
<dev>:/quake3/baseq3/pak0.pk3 … pak8.pk3
```

## Open Arena

App folder: `apps/openarena/`

No `baseq3` needed:

```
<dev>:/quake3/baseoa/pak0.pk3 …
```

## CLASSIC (Dreamcast crossplay)

App folder: `apps/ioquake3-classic/`

Only `pak0`–`pak2` are read (byte-identical to the Dreamcast data files);
higher paks are ignored. `zpack-classic.pk3` (the extra assets/QVMs needed
for protocol-43 compatibility) is embedded in the DOL and auto-extracted to
`baseq3/` on first boot — you don't need to copy it yourself.

```
<dev>:/quake3/baseq3/pak0.pk3
<dev>:/quake3/baseq3/pak1.pk3
<dev>:/quake3/baseq3/pak2.pk3
```

To play on Dreamcast community servers, also add the community map pack:

```
<dev>:/quake3/baseq3/dc-mappack.pk3
```

## Mod Selector

App folder: `apps/ioquake3-modselect/`

Needs the default Q3A data plus any extra mod folder you want to appear in
the boot menu:

```
<dev>:/quake3/baseq3/pak0.pk3 … pak8.pk3
<dev>:/quake3/<modname>/*.pk3
```

Any folder under `<dev>:/quake3/` (other than `baseq3`) containing at least
one `.pk3` is listed at boot; pick "baseq3 only" to boot normally.

---

## Installing

Every [release](https://github.com/Mayo1970/ioQuake3-wii/releases) includes a
prebuilt `boot.dol` for each build — no need to compile anything yourself
unless you want to.

1. Download the `boot.dol` for the build you want (or build it yourself, see
   the main [README.md](README.md)).
2. Copy it to `<dev>:/apps/<name>/boot.dol` on your SD card or USB drive,
   matching the app folder names in the Common Layout section above, along
   with a `meta.xml`.
3. Copy the matching game data from the sections above into
   `<dev>:/quake3/`.
4. Insert the SD card or connect the USB drive, launch the Homebrew Channel,
   and start the app.

## Troubleshooting

If a build misbehaves after switching between builds or changing settings
(stuck at a stale framerate cap, wrong sound pool size, or other unexpected
behavior), delete that build's config on the device
(`<dev>:/quake3/baseq3/q3config.cfg`, `baseoa/oaconfig.cfg`) and let it regenerate — `CVAR_ARCHIVE`
settings persist across rebuilds and can silently override the new defaults.
