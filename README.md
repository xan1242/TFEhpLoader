# Tag Force EHP Loader

This is a plugin designed to allow easy replacement of embedded EhFolders in the executable (EBOOT) without having to modify it.

This is a useful tool for translators and modders since it avoids the nasty process of editing the executable.

## Usage

Run it as a plugin via PPSSPP or CFW PSP and put your EHPs right next to the plugin in a folder called `ehps/<game_serial>` (where `<game_serial>` is the serial number of the game, example `ULES01183`).



The available EHPs are:

- `cname.ehp` - character names

- `interface.ehp` - window decoration textures

- `rcpset.ehp` -  the deck recipes and their names

- `load_fl.ehp` - the loading widget

- `sysmsg.ehp` - system message text (e.g. "Checking storage media.")

- `packset.ehp` - the card shop pack names

To extract these files, you may use [EHPScanner](https://github.com/xan1242/EHPScanner) with a decrypted EBOOT.

### PPSSPP Note

Be sure to add your game serial to the `games` section in  the included`plugin.ini`!

If you do not do this, the plugin will not load!

Example:

```ini
[games]
ULES01183 = true
; add other IDs here
```

### PSP Note

If you're using a CFW PSP, please only load the `TF-EhpLoaderBoot.prx` plugin. This will properly boostrap `TF-EhpLoader.prx` into the userspace after the game starts. (Both `TF-EhpLoaderBoot.prx` and `TF-EhpLoader.prx` must be present!)

## Compatibility

This should work with all Tag Force games.

This should also work on PPSSPP and real hardware (via the bootstrap plugin).

## Building

You may follow the same build instructions as per [PSPModBase here](https://github.com/xan1242/PSPModBase#usage).
