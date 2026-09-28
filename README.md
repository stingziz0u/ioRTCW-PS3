# ioRTWC-PS3

Port of [iortcw](https://github.com/iortcw/iortcw) (Return to Castle
Wolfenstein, single player) to the PlayStation 3 homebrew scene, built with
[PSL1GHT](https://github.com/ps3dev/PSL1GHT).

**Work in progress — not playable yet.**

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
