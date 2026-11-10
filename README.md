# Key Router

An SKSE plugin for Skyrim Anniversary Edition (tested on **1.6.1170**) that finds key conflicts between mods and lets you decide which mod gets the key.

> ## ⚠️ Beta: best effort, no guarantees
> This mod is in **beta**. It hooks Skyrim's input system, which is one of the busiest and most heavily modded parts of the game. It works in the author's own heavily modded setup, but every load order is different and they cannot all be tested.
>
> - Provided **as is**, on a best-effort basis, with **no guarantees** of any kind (see [LICENSE](LICENSE)).
> - **Make a save before installing it**, and keep backups of your mod config files.
> - The Move feature **edits other mods' config files**. Every change is written to `KeyRouter.log` so it can be undone by hand.
> - Nothing is stored in your save game. To get rid of it, turn it off in the settings or remove the DLL.
> - Bug reports are welcome. Please attach `KeyRouter.log`.

## What it does

Mods often grab the same key without knowing about each other. You press L and two mods react, or the one you wanted doesn't react at all. Key Router finds every mod that wants a key. When two or more want the same press, a small window asks which one should get it.

## Features

### Conflict detection
- **Script (Papyrus) mods**: the exact scripts registered for each key, read live from the game. They are not guessed from files.
- **SKSE DLL mods**: their hotkeys are read from their config files (`.ini`, `.toml`, `.json`, `.yaml`, and MCM Helper settings).
- DLL mods that listen to the game's input directly, and DLLs that read the keyboard on their own (`GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`, their own DirectInput device).
- **Skyrim's own controls** (Activate, Sprint, Ready Weapon...) can count as a competitor.
- Left and right Alt, Ctrl and Shift are separate keys. Combos such as `Left Ctrl+T` are understood.
- A plugin and a DLL with the same name are treated as one mod.

### The choice window
- Lists every mod that wants the key, plus **Nobody**.
- The mod you pick gets the key press. Hold the key while you click to give it a hold instead of a tap.
- **Remember my choice**: that key then goes straight to your pick, even after reloading. It asks again if a mod that wants the key is added or removed.

### Keyboard map (needs SKSE Menu Framework)
- Hold **Backspace for 10 seconds** (you can change the key), or open it from the menu. It shows the keyboard plus mouse buttons, and the game pauses while it is open.
- Switchable **US (ANSI, 104 keys)** or **UK (ISO, 105 keys)** layout: the Layout button on the map, the settings page, or `Layout=` in `KeyRouter.ini`. The UK map has the tall Enter, the `#` key, the extra `\` key next to Left Shift, and UK labels.
- Colors:
  - green: no mod uses the key
  - orange: one mod
  - yellow: shared, but no conflict (passive mods)
  - red: conflict
  - blue corner: a remembered choice exists for that key
- The mouse wheel zooms and dragging moves the map. Zoom in to see mod names on the keys.
- **Click a key** to see every mod using it, where its key is set, and how to change it.

### Fix conflicts from the map
- **DLL mods with a config file**: click **Move...**, then press the new key.
  - Key Router rewrites that setting in the mod's own file. Only the key changes; any Ctrl, Alt or Shift in the same setting stays.
  - If the new key is already in use, it asks first.
  - The mod reads its file at startup, so **restart Skyrim** to use the new key.
  - For MCM Helper mods, the change goes to your own `Data\MCM\Settings\<mod>.ini`.
- **Script mods**: the map tells you whether the mod has an MCM menu (a quest script that extends `SKI_ConfigBase`), where you can change the key. If not, the key is set in its scripts.
- **Skyrim's own controls**: change them in the game's Controls settings.

### In-game settings (needs SKSE Menu Framework)
- Every `KeyRouter.ini` option is under **Key Router** in the SKSE Menu Framework menu (F1 by default). Changes apply at once and are saved to the INI, keeping its comments. The few options that need a restart say so.
- A **Remembered choices** page with Forget and Forget all buttons.
- Passive mods: mods that only watch a key and never count as a competitor.
- DLL hotkey corrections, for when a DLL's config file could not be read.

### Kill switch
- Hold **K for 10 seconds** (you can change the key and time) and Skyrim is ended at once. It works even when the game is frozen.
- Made for remote play (Moonlight/Sunshine), where Task Manager is out of reach.
- Nothing is saved when it fires.

### Logging
- `Documents\My Games\Skyrim Special Edition\SKSE\KeyRouter.log` lists:
  - every hotkey found
  - every Move change (old and new line)
  - one `[map]` line per used key each time the map opens
- Optional per-key-press logging (`LogKeys`) and diagnostics help with bug reports.

## Requirements
- Skyrim AE **1.6.1170**, the only version tested. It should work on other 1.6.x and 1.7.x versions (untested). Not for SE 1.5.x or VR: key routing refuses to install there.
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352): optional, but needed for the keyboard map and the in-game settings.

## Known limits
- Keyboard and mouse only, no gamepad. Mouse buttons are off by default.
- By default it only checks during normal play, not while menus are open (there is an option for that).
- It only rewrites DLL keys written as numbers. Keys written as names are spelled differently by every mod, so the map says which file and setting to change by hand.
- Script mods keep their keys in your save or MCM, so Key Router cannot change those. The map tells you where to change them.
- Hotkeys of a DLL that has no config file can be detected, but there is nothing to edit.
- Updating or reinstalling a mod can bring its old key back. Just move it again.

## Building

Built with Visual Studio 2022, CMake and vcpkg against [CommonLibSSE NG](https://github.com/alandtse/CommonLibSSE-NG) 6.7.0.

1. Get the CommonLibSSE-NG source and set the environment variable `CommonLibSSEPath_NG` to its folder (or put it at `C:\dllmaker\CommonLibSSE-NG`).
2. Set `VCPKG_ROOT` (Visual Studio's bundled vcpkg works).
3. From a Developer Command Prompt for VS 2022, in this folder:

```
cmake --preset vs2022-windows
```

```
cmake --build build --config Release
```

The DLL and PDB are in `build\Release`. Install them with `KeyRouter.ini` into `Data\SKSE\Plugins`.

## Files
- `src/`: the plugin source
- `KeyRouter.ini`: all settings, with explanations
- `README.txt`: the in-game feature notes shipped with the mod
- `src/SKSEMenuFramework.h`: the SKSE Menu Framework API header by Thiago Kaique (QTR-Modding), MIT; see `THIRD-PARTY-SKSEMenuFramework-LICENSE.txt`

## License
- Key Router's own source code: **MIT**, see [LICENSE](LICENSE).
- `KeyRouter.dll` statically links [CommonLibSSE NG](https://github.com/alandtse/CommonLibSSE-NG), which is **GPL-3.0-or-later** with the Modding Exception and the GPL-3.0 Linking Exception (with Corresponding Source). So the compiled DLL as a whole is distributed under **GPL-3.0-or-later** with those exceptions. See [GPL-3.0.txt](GPL-3.0.txt) and [CommonLibSSE-NG-EXCEPTIONS.txt](CommonLibSSE-NG-EXCEPTIONS.txt).
- Corresponding source for each release: this repository at the release tag (for example `v1.6.2`), plus CommonLibSSE NG 6.7.0.
- Other libraries in the build (spdlog, {fmt}, DirectXTK, Xbyak, and the SKSE Menu Framework API header) are listed with their licenses in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

## Credits
- The SKSE team, the Address Library and CommonLibSSE-NG authors
- Thiago Kaique (QTR-Modding) for SKSE Menu Framework
