# ioRTWC-PS3

Return to Castle Wolfenstein (single player) for the PlayStation 3 homebrew
scene: a port of [iortcw](https://github.com/iortcw/iortcw) built with
[PSL1GHT](https://github.com/ps3dev/PSL1GHT), on top of the PS3 platform and
GL→RSX layers of [IoQuake3-PS3](https://github.com/Mayo1970/IoQuake3-PS3).

The whole campaign plays on a real PS3 at mostly 60 fps in 720p: all 32 maps,
including the six cutscene levels (there's some dips here and there).

## Features

- The complete single player campaign, with saves and level transitions.
- 60 fps at 1280×720. The renderer back end runs on the PPU's second thread.
- DualShock 3 controls, rebindable from the Controls menu with the pad.
- Lock-on with L2: the view follows the nearest visible enemy.
- The XMB on-screen keyboard names your saves.
- Screen Fit slider for TVs with overscan, applied live.
- Distance fog.
- A USB keyboard and mouse should also work.
- No network code: single player only.

## Requirements

- A PS3 that runs homebrew (HEN or CFW).
- Your own copy of Return to Castle Wolfenstein, patched to 1.41 (the Steam
  version is). **No game data is included.**

## Install

1. Install `iortcw-ps3-1.0.pkg` from the
   [releases](https://github.com/stingziz0u/ioRTWC-PS3/releases) with the
   Package Manager. It creates `/dev_hdd0/game/IORTCWPS3/USRDIR/main/`.
2. Copy these files from the game's `Main` folder to
   `/dev_hdd0/game/IORTCWPS3/USRDIR/main/` (FTP works well):
   - `pak0.pk3`
   - `sp_pak1.pk3`
   - `sp_pak2.pk3`
   - `sp_pak3.pk3`
   - `sp_pak4.pk3`
3. Start **Return to Castle Wolfenstein** from the XMB.

Each level takes about 20 seconds to load from the HDD (tested with an SSD).

## Controls

In game (every button can be rebound in Options → Controls with the pad):

| Button | Action |
|---|---|
| Left stick | move |
| Right stick | look (speed: the "Gamepad" slider in Controls) |
| R2 | fire |
| R3 | alternate fire / weapon scope |
| L2 (hold) | lock-on |
| L1 / R1 | previous / next weapon |
| L3 | sprint |
| Cross | jump |
| Circle | crouch |
| Square | reload |
| Triangle | use / activate |
| D-pad up | kick |
| D-pad down | use item |
| D-pad left / right | lean |
| Select (tap) | notebook |
| Select (hold) | binoculars |
| Start | menu |
| Select + Triangle | console |

In the menus:

- D-pad or left stick: move between items.
- Cross: accept.
- Circle: go back.
- Right stick: moves a pointer. Square clicks where it points.

Cross on the save name opens the on-screen keyboard. If you leave the name
empty, the game saves as `<map>_<nn>`.

## Video

The game renders at 1280×720. **Screen Fit** replaces "Fullscreen" in the
video options. It shrinks the picture, from 70 % to 100 %, for TVs that crop
the edges (overscan).

## Files and troubleshooting

Everything lives in `/dev_hdd0/game/IORTCWPS3/USRDIR/`:

| File | Contents |
|---|---|
| `main/save/` | saves |
| `main/wolfconfig.cfg` | settings and key binds |
| `iortcw-ps3.log` | log of the last run (`iortcw-ps3.old.log` is the run before) |
| `VERSION.TXT` | the build |

If the game quits at startup, the log says why. A common cause is a missing
`.pk3` file.

If you open an issue, attach the log.

## Building

You need the ps3toolchain (gcc 7.2) and PSL1GHT in `/usr/local/ps3dev`:

```
export PS3DEV=/usr/local/ps3dev PSL1GHT=$PS3DEV
make -j4        # -> iortcw-ps3-1.0.pkg
make verify     # build stamp of the tree and of the pkg
```

Other builds:

| Command | Builds |
|---|---|
| `make DIAG=1` | the game with the on-screen fps counter, plus a frame profile and load hitches in the log |
| `make MAPWALK=1` | a test that loads and plays every map, logs memory and frame times, then quits |
| `make STAGE=1` | a headless engine test |

The Makefile header lists all the options.

The link step checks the PS3 limits: a single 64 KB TOC, and no `r2off`
stubs.

## Credits

- **iortcw** team, and id Software / Gray Matter / Nerve for the RTCW
  source release.
- **Mayo1970** for [IoQuake3-PS3](https://github.com/Mayo1970/IoQuake3-PS3).
  This port builds on its PS3 platform layer and its GL→RSX translation
  layer (ps3gl).
- The **ps3dev / PSL1GHT** developers.
- The PS3 homebrew scene. The on-screen keyboard follows the recipe used by
  Apollo Save Tool.

## License

GPLv3 with the additional terms of the RTCW source release: see `LICENSE`.

The IoQuake3-PS3 code is GPLv2 or later and is used here under the GPLv3.

Return to Castle Wolfenstein is a trademark of its owners. This project is
not affiliated with them and includes no game data.
