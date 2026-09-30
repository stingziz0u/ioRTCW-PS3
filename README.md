# ioRTCW-PS3

A native homebrew port of [iortcw](https://github.com/iortcw/iortcw)
(Return to Castle Wolfenstein, single player) for the PS3.

## Lineage

iortcw is the maintained, ioquake3-style continuation of the Return to
Castle Wolfenstein source code that id Software released under the GPL in
2010. This project adds a full PS3 platform layer on top of its single
player engine: video, audio, input, file system and system integration.
The platform layer and the OpenGL-to-RSX translation layer (ps3gl) started
from [IoQuake3-PS3](https://github.com/Mayo1970/IoQuake3-PS3). In short:

```
Return to Castle Wolfenstein (Gray Matter / Nerve / id Software, 2001) -> iortcw -> ioRTCW-PS3 (this project)
```

The upstream README (cvars, console commands) is still valid for anything
that isn't PS3 specific:
[iortcw/iortcw](https://github.com/iortcw/iortcw/blob/master/README.md).

## Features

- **The whole single player campaign**: every mission and the six
  cutscene levels between them. Level transitions, the mission briefing
  screens, saves and loads all work.
- **Video**: mostly 60 fps at 1280x720 on the RSX through ps3gl (there are
  some dips here and there).
  - The renderer's back end runs on the PPU's second hardware thread, like
    the original game's `r_smp`.
  - Distance fog is rendered with GPU shaders.
  - TV screen fit (overscan) goes from 70 % to 100 %, applied live from
    the video menu.
- **Audio**: hardware audio on its own thread. The mixer runs at the
  game's native 22050 Hz and is resampled to 48 kHz, with music and voices
  as in the original.
- **Input**: DualShock 3.
  - Every action is rebindable from **Options -> Controls** with the pad,
    and the menus show the PS3 button names.
  - An optional **lock-on** (L2) makes the view follow the nearest visible
    enemy while held.
  - The menus, built for a mouse on PC, are fully usable with the D-pad,
    and the right stick moves a pointer.
  - Save names are typed with the XMB **on-screen keyboard**. An empty
    name saves as `<map>_<nn>`.
- Saves and settings on the HDD, and a log of every run for
  troubleshooting.

## Requirements

- A PS3 capable of running homebrew (HEN or CFW).
- Your own legally-owned copy of Return to Castle Wolfenstein, patched to
  1.41 (the Steam version already is). **No game data is included in this
  repository or in any release build.**
- To build from source: the [ps3dev / PSL1GHT](https://github.com/ps3dev)
  toolchain (`ppu-gcc` 7.2, PSL1GHT SDK, `cgcomp`, `make_self_npdrm`,
  `package_finalize`, etc.) and `python3`.

## Installation

1. Install `iortcw-ps3-1.0.pkg` from the XMB (it shows up as "Return to
   Castle Wolfenstein"). This creates
   `/dev_hdd0/game/IORTCWPS3/USRDIR/main/`, empty and ready to fill.
2. By FTP, copy these files from your game's `Main` folder:

   ```
   IORTCWPS3/USRDIR/main/pak0.pk3       <- required
   IORTCWPS3/USRDIR/main/sp_pak1.pk3    <- required
   IORTCWPS3/USRDIR/main/sp_pak2.pk3    <- required
   IORTCWPS3/USRDIR/main/sp_pak3.pk3    <- required
   IORTCWPS3/USRDIR/main/sp_pak4.pk3    <- required
   ```

**Filenames are case-sensitive** on the PS3: keep them lowercase, exactly
as above.

Saves go to `USRDIR/main/save/` and the settings to
`USRDIR/main/wolfconfig.cfg`. The log is `USRDIR/iortcw-ps3.log` (the
previous run's is kept as `iortcw-ps3.old.log`). If the game doesn't start,
the log says why, and a missing `.pk3` is the usual cause.

## Building from source

1. Install the [ps3dev toolchain](https://github.com/ps3dev/ps3toolchain)
   (third-party, not part of this project -- these are its own official
   instructions):

   ```bash
   git clone https://github.com/ps3dev/ps3toolchain.git
   cd ps3toolchain
   export PS3DEV=/usr/local/ps3dev
   export PSL1GHT=$PS3DEV
   export PATH="$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin"
   ./toolchain.sh
   ```

2. Clone this repository and build, from its root:

   ```bash
   git clone https://github.com/stingziz0u/ioRTCW-PS3.git
   cd ioRTCW-PS3
   make -j4          # -> iortcw-ps3-1.0.pkg
   make verify       # shows the build stamp of the tree and of the PKG
   ```

Every variant builds in its own folder (`obj/<variant>`), so no
`make clean` is needed between them:

```bash
make DIAG=1       # on-screen FPS, frame profile and load hitches in the log
                  #   -> iortcw-ps3-1.0-diag.pkg
make MAPWALK=1    # loads and plays every map, logs memory and frame times, quits
                  #   -> iortcw-ps3-mapwalk.pkg
make MAPWALK=1 WALKSECS=90   # the same, 90 s per map in god mode, cutscene
                             # levels played to the end
make STAGE=1      # headless engine test used to bring up the port
make DEBUG=1      # debug build of any of the above
```

The game modules (qagame, cgame, ui) are linked into the executable: the
PS3 can't load them at runtime. The link step checks the PS3 limits (a
single 64 KB TOC, no `r2off` stubs) and fails if a change breaks them.

The shaders are already assembled into `code/gl/ps3gl_shader_data.h`. They
are written in NV40 assembly (`code/gl/shaders/asm/`), so rebuilding them
with `code/gl/shaders/assemble_shaders.sh` only needs PSL1GHT's `cgcomp`,
not NVIDIA's Cg compiler.

## Controls

| Action | Default | Notes |
|---|---|---|
| Move / Strafe | Left stick | |
| Look | Right stick | Speed: the "Gamepad" slider in Controls |
| Fire | R2 | |
| Alternate fire / Scope | R3 | |
| Lock-on | L2 (hold) | Follows the nearest visible enemy |
| Previous / Next weapon | L1 / R1 | |
| Sprint | L3 | |
| Jump | Cross | |
| Crouch | Circle | |
| Reload | Square | |
| Use / Activate | Triangle | |
| Kick | D-pad up | |
| Use item | D-pad down | |
| Lean left / right | D-pad left / right | |
| Notebook | Select (tap) | |
| Binoculars | Select (hold) | While held |
| Console | Select + Triangle | |
| Menu | Start | Fixed, not rebindable |
| Confirm / Back (menus) | Cross / Circle | |
| Pointer / Click (menus) | Right stick / Square | For the screens made for a mouse |

Everything except Start is rebindable from **Options -> Controls**:
select an action with Cross and press the new button.

## Known limitations

- **Single player only.** RTCW's multiplayer is a separate game and isn't
  part of this port. There's no network code at all.
- **Mods: not supported.** The game code is compiled into the executable,
  and only the base game has been tested.
- **Load times**: each level takes about 20 seconds to load (tested with an
  SSD in the PS3).
- The render resolution is fixed at 1280x720. The game is limited by the
  CPU, so a lower internal resolution wouldn't make it faster.
- USB keyboard and mouse should work, but the game is built for the
  DualShock 3. Whether a keyboard works depends on the model (the PS3's
  own keyboard driver).

## Credits

- [iortcw](https://github.com/iortcw/iortcw) by the iortcw team and
  contributors -- the engine this project is built on.
- id Software, Gray Matter Interactive and Nerve Software, for Return to
  Castle Wolfenstein and its GPL source release.
- [IoQuake3-PS3](https://github.com/Mayo1970/IoQuake3-PS3) by Mayo1970 --
  the PS3 platform layer and ps3gl, the OpenGL-to-RSX layer, started from
  it.
- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and
  [ps3toolchain](https://github.com/ps3dev/ps3toolchain) -- the PS3
  homebrew SDK and toolchain.
- **Apollo Save Tool**, whose on-screen keyboard usage (by way of
  [CrispyCell](https://github.com/stingziz0u/crispycell)) this port follows.

## License

GPLv3 with the additional terms of id Software's Return to Castle
Wolfenstein source release, inherited from iortcw. See
[`LICENSE`](LICENSE). The IoQuake3-PS3 code is GPLv2 or later and is used
here under the GPLv3.

No Return to Castle Wolfenstein data files (`.pk3` or otherwise) are
included in this repository. You need your own legally-owned copy of the
game. Return to Castle Wolfenstein is a trademark of its owners, and this
project is not affiliated with them.

## AI disclosure

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions.
Every bit of testing and debugging were made by me on real hardware.
