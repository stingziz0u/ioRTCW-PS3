# ioRTWC-PS3

Port of [iortcw](https://github.com/iortcw/iortcw) (Return to Castle
Wolfenstein, single player) to the PlayStation 3 homebrew scene, built with
[PSL1GHT](https://github.com/ps3dev/PSL1GHT).

The whole single player campaign plays on a real PS3 at 60 fps (720p output),
with the DualShock 3, fog, saves and the XMB on-screen keyboard.

## Install

1. Install `iortcw-ps3-1.0.pkg` (Package Manager, HEN or CFW). It creates
   `/dev_hdd0/game/IORTCWPS3/USRDIR/main/`.
2. Copy your game data there (below).
3. Start **Return to Castle Wolfenstein** from the XMB.

The log of the last run is `USRDIR/iortcw-ps3.log` (the one before,
`iortcw-ps3.old.log`); saves go to `USRDIR/main/save/`.

## Game data

No game data is included. You need your own copy of Return to Castle
Wolfenstein (patched to 1.41, e.g. the Steam version). Copy these files from
its `Main` folder to `/dev_hdd0/game/IORTCWPS3/USRDIR/main/` over FTP:

- `pak0.pk3`
- `sp_pak1.pk3`
- `sp_pak2.pk3`
- `sp_pak3.pk3`
- `sp_pak4.pk3` (GOTY/Steam; if yours lacks it, it comes in iortcw's
  `patch-data-141.zip`)

## Controls

In game (every button can be rebound in Options → Controls with the pad):

| Button | Action |
|---|---|
| Left stick | move |
| Right stick | look (speed: the "Gamepad" slider in Controls) |
| R2 | fire |
| R3 | alternate fire / weapon scope |
| L2 (hold) | lock-on: the view follows the nearest visible enemy |
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

In the menus: D-pad or left stick to move, Cross to accept, Circle to go
back, the right stick moves a pointer and Square clicks. Cross on the save
name opens the on-screen keyboard; an empty name saves as `<map>_<nn>`.
A USB keyboard and mouse work too.

## Video

The game renders at 1280×720. **Screen Fit** (in the video options, where
"Fullscreen" was) shrinks the picture for TVs that crop the edges (overscan),
from 70 % to 100 %, applied live.

## Building

PSL1GHT with the ps3toolchain gcc 7.2 in `/usr/local/ps3dev`:

```
export PS3DEV=/usr/local/ps3dev PSL1GHT=$PS3DEV
make -j4        # -> iortcw-ps3-1.0.pkg
make verify     # patch level of the tree and of the pkg
```

`make DIAG=1` adds the on-screen fps and a frame profile in the log;
`make MAPWALK=1` builds a test that plays every map and quits (see the
Makefile for its options).

## Credits

- **iortcw** team, and id Software / Gray Matter / Nerve for the RTCW source
  release.
- **Mayo1970** for [IoQuake3-PS3](https://github.com/Mayo1970/IoQuake3-PS3),
  whose PS3 platform layer and GL→RSX translation layer this port builds on.
- The **ps3dev / PSL1GHT** developers.

## License

GPLv3 with the additional terms of the RTCW source release (see `LICENSE` and
the upstream notices). Return to Castle Wolfenstein is a trademark of its
respective owners; this project is not affiliated with them.
