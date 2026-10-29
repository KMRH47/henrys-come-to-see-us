<p align="center"><img src="docs/page/banner.png?v=690da305e755" width="850" alt="Henry&#x27;s Come to See Us!, an M4A1 for Kingdom Come: Deliverance II."><br>
<img src="docs/page/notice.png?v=96feb7b88f86" width="850" alt="Version 7, The Red Dot Sight: Fit it to any M4, take it off again. Each gun keeps its own."><br>
<img src="docs/page/panels.png?v=63013dde09e1" width="850" alt="How to install and remove the mod, what it is compatible with, and the M4A1, 5.56 magazine, red dot sight and iron sights in the game."><br>
<img src="docs/page/features.png?v=8fa4c4b524b3" width="850" alt="What the M4 does in the game."><br>
<img src="docs/page/commands.png?v=ea2e5fe8164e" width="850" alt="Console commands that give the M4, its ammunition and its sights."><br>
<img src="docs/page/gallery.png?v=167494890226" width="850" alt="Pictures from the game."><br>
<img src="docs/page/forge.png?v=148a295b1ec1" width="850" alt="Built with kcd2-forge, the CLI and studio the mod is made with."></p>

### Launch options for Linux and Steam Deck

```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

### Console commands

```
m4_give gun
m4_give gun_red_dot
m4_give ammo
m4_give red_dot
m4_give irons
```

## Download

**[Download the newest release](https://github.com/KMRH47/henrys-come-to-see-us/releases/latest)**

Unzip it and run `install_windows.bat` on Windows or `install_linux.sh` on Linux.
Or install it by hand with the steps above.

The installers find the game through Steam, copy the zip's `game` folder into it and list what they copied in `m_carbine_install.txt`.
Run one again to update or remove the mod; removing deletes only the files on that list.
Read them first in [`install/`](install/).
The Nexus download leaves them out because Nexus's virus scan flags them.
The mod page is on [Nexus Mods](https://www.nexusmods.com/kingdomcomedeliverance2/mods/3886).

## What is here

This repo holds the parts of the mod that were made for it.

| Folder | What it holds |
|---|---|
| `plugin/` | The native plugin (`m4_autofire.asi`) in C. It adds full auto fire, the M4's own ammo, damage and sound, and the reload and aim fixes. |
| `mod/` | The mod's Lua scripts, item, weapon and ammo tables, sound and particle setup, round counter and text. |
| `model/` | The M4 model in Blender, with its baked textures packed in. |
| `sounds/` | The shot and reload sounds. |
| `install/` | The optional Windows and Linux installers, shipped in the GitHub zip only. |

## What is not here

The animations, the in-game model export and the animation database are built on top of the game's own files.
Those belong to Warhorse Studios, so they are not in this repo.
You cannot rebuild the full mod from this repo alone; use the download above to play.

## Build the plugin

Install [Zig](https://ziglang.org/) 0.14, then run:

```
plugin/build.sh
```

It writes `plugin/m4_autofire.asi`.
The game loads it through Ultimate ASI Loader, with `m4_autofire.ini` next to it.

## Credits

- Ultimate ASI Loader by ThirteenAG (MIT license), shipped in the Nexus zip.
- The sounds were generated with Stable Audio 3 Small. Powered by Stability AI.
- The model, plugin and scripts were made for this mod.

## License

MIT, see `LICENSE`.
