Key Router 1.4.0 (BETA)
=======================

BETA - BEST EFFORT, NO GUARANTEES. Make a save before installing. The Move feature edits other
mods' config files; every change is written to KeyRouter.log so it can be undone by hand.

Targets: Skyrim AE 1.6.1170 (the only version tested). Should work on other 1.6.x and 1.7.x
  versions (untested). Not for SE 1.5.x or VR: key routing refuses to install there.
Requires: SKSE, Address Library for SKSE Plugins.
Optional: SKSE Menu Framework (keyboard map and in-game settings).
Source: https://github.com/Tonycubed2/keyrouter
License: Key Router's code is MIT. The DLL links CommonLibSSE NG (GPL-3.0-or-later with
  exceptions), so the DLL as a whole is GPL-3.0-or-later; see KeyRouter_THIRD-PARTY-NOTICES.txt.

What it does
  When you press a key that two or more mods want, the press is held back from all of them and
  a small window lists those mods plus "Nobody". The mod you click gets the press; the others
  never see it. No mod is favored. Unless you tell it to remember, the window comes back on every
  press of that key; to make it stop for good, change the key in one of the named mods (the
  keyboard map below can do that for most DLL mods).

  Remember my choice: the window has a "Remember my choice: OFF" button. Click it to switch it ON
  (the window reopens), then pick a mod or Nobody. From then on that key goes straight to your pick
  whenever exactly those mods want it, without asking; if the mods on that key change, it asks again.
  Choices are saved in Data\SKSE\Plugins\KeyRouter_Choices.ini (under MO2 the file lands in the
  overwrite folder). Delete a line there to be asked again.

  The window is the game's own message box, so it works with the mouse and keyboard and with
  ENB, DXVK, ReShade and upscalers (nothing is drawn on top of the game). The game is not paused
  by Key Router itself; with Skyrim Souls RE handling MessageBoxMenu, the world keeps running
  while the window is open. Without Skyrim Souls RE the vanilla message box pauses.

Who counts as wanting a key
  1. Papyrus mods (MCM hotkeys and any script using RegisterForKey): found live, every press.
     SKSE's key listener is run first with its Papyrus events held back, so Key Router sees
     exactly which scripts would get OnKeyDown for that key. Named by their plugin (.esp/.esl).
  2. SKSE DLL mods: their config files are read at game start (Data\SKSE\Plugins\<Name>.ini,
     .toml, .json, a <Name> folder there, and MCM Helper settings). Every hotkey found is in the
     log; wrong or missing ones can be corrected in KeyRouter.ini [DllKeys].
     Left and Right Alt/Ctrl/Shift are separate keys in combos; plain Alt/Ctrl/Shift in a
     config file means either side.
  3. Skyrim's own controls: read from the game's live control map, so key remapping mods
     (custom controlmap.txt, ControlMap_Custom.txt, mods that free up keys) are taken into
     account automatically.

How the picked mod gets the press
  - Papyrus mods: the held OnKeyDown/OnKeyUp events are sent to that mod's scripts only.
  - DLL mods using the game's input events: each listener is given only the events it is
    allowed to see during each frame, and the press is replayed to the picked one.
  - Mods that plug a handler into the game's own MenuControls or PlayerControls lists (Dragon's
    Eye Minimap does): the game skips a handler whose enabled byte is 0, so for an event a
    handler must not see, that byte is cleared for that one event and restored right after.
  - DLL mods that read the keyboard themselves (GetAsyncKeyState, GetKeyState,
    GetKeyboardState, DirectInput): those calls are hooked in each DLL that has a known
    hotkey; while a key is held back they read it as up, and the picked DLL reads it as down.
  - Skyrim's own controls: the press is replayed to the game's control handler, and scripts
    listening with OnControlDown/OnControlUp get it too.
  If you are still holding the key when you click, the picked mod gets the rest of that hold
  and the release. If you already let go, it gets a short tap (SyntheticPressMs, 50 ms).

Kill switch
  Hold K for 10 seconds while Skyrim is the active window and Skyrim is ended at once, even when
  it is frozen. One beep 3 seconds before the end. Key, time and on/off are in KeyRouter.ini
  [KillSwitch]. Meant for remote play (Moonlight/Sunshine) where Task Manager is out of reach.
  It works even with Enabled=0 in [General].

Limits
  - A DLL whose hotkey is in a file Key Router cannot read, or set only in code, is not known
    until you add it under [DllKeys].
  - A DLL that reads the game's own keyboard state directly (not through input events or the
    Windows/DirectInput calls above) cannot be held back.
  - Holding a key down for a "hold" action (sprint, charge) works only if you keep holding it
    while you click.
  - Gamepad buttons are not checked.

Install: with your mod manager. Files (all in Data\SKSE\Plugins):
  KeyRouter.dll, KeyRouter.pdb, KeyRouter.ini (all settings, with explanations),
  KeyRouter_README.txt (this file), and the license files: KeyRouter_THIRD-PARTY-NOTICES.txt,
  KeyRouter_GPL-3.0.txt, KeyRouter_CommonLibSSE-NG-EXCEPTIONS.txt
Building from source: see README.md at https://github.com/Tonycubed2/keyrouter

Log: Documents\My Games\Skyrim Special Edition\SKSE\KeyRouter.log

KEYBOARD MAP (1.2.0)
--------------------
Hold Backspace for 10 seconds (Skyrim must be the active window) and a map of a 104-key
keyboard plus the mouse buttons opens. The game is paused while it is open.

  green  = no mod uses the key
  orange = one mod uses it
  yellow = two or more use it, but all except one are listed in [PassiveMods] (no conflict)
  red    = two or more use it and they conflict
  blue corner = a remembered choice exists for that key (KeyRouter_Choices.ini)

Mouse wheel zooms around the pointer, left-drag moves the map, a click shows every mod using
that key in the panel at the bottom, "Fit" resets the view.
Zoomed in far enough, each key lists the mods using it (cut to fit). Hover a key for the
full list, with where each bind came from. Key combos (Ctrl+L and so on) are listed on the
key but do not change its color. Esc or "Close" closes the map.

The map is built when it opens, from the same data the popup uses: Papyrus registrations
(asked of the game's script system key by key), DLL configs, and the game's controls.
Needs SKSE Menu Framework (Nexus 120352). Without it, Key Router works but has no map.
Settings: [KeyboardMap] Enabled / Key / HoldSeconds in KeyRouter.ini.

IN-GAME SETTINGS (1.2.0)
------------------------
With SKSE Menu Framework, open its menu (F1 by default) and go to Key Router. Three pages:

  Settings            Every KeyRouter.ini option. A change applies at once and is written to
                      KeyRouter.ini right away (comments in the file are kept). A few options
                      only take effect after restarting Skyrim; the page says which ones when
                      you change them: key routing on/off, reading DLL config files, the two
                      "watch DLLs" options, the DLL config part of mouse buttons, and the DLL
                      hotkey corrections. The kill switch and keyboard map keys can be typed or
                      set with "Press a key...".
  Remembered choices  Every "Remember my choice" answer, with Forget and Forget all.
  Keyboard map        Opens the keyboard map.

Key Router does not check key presses while an SKSE Menu Framework window is open (its menu,
the keyboard map), the same as in Skyrim's own menus, unless "Also check while menus are open"
is on.
With key routing off, the settings page and the keyboard map still work, but the map cannot
show script (Papyrus) hotkeys, because finding them needs the routing hooks.

FIXING CONFLICTS FROM THE MAP (1.3.0)
-------------------------------------
Click a key on the keyboard map: the panel at the bottom lists every mod that uses it and how
each one can be changed:

  DLL mod with a config file   "iToggleKey in UnsearchedCorpsesIndicator.ini" and a Move... button.
                               Click Move..., press the new key, and Key Router rewrites that line
                               in the mod's file (only the key; Shift/Ctrl/Alt in the same setting
                               stay). If the new key is already used, it asks first. The mod reads
                               its file when the game starts: restart Skyrim to use the new key.
                               Until then the map shows it on the new key, "after restart".
                               For MCM Helper settings the change goes to the player's own file
                               (Data\MCM\Settings\<mod>.ini), which wins over the mod's defaults.
                               Keys written as names (not numbers) are not rewritten, because every
                               mod spells key names its own way: change those by hand.
  Script mod (ESP/ESL)         "change it in this mod's MCM menu" when the mod has an MCM menu
                               (a quest script that extends SKI_ConfigBase), else "the key is set
                               in the mod's scripts".
  Skyrim's own control         change it in the game's Controls settings.

The change is made in the mod's own file. Updating or reinstalling that mod can bring its old
key back; move it again then. Every change is written to KeyRouter.log (old and new line).
Each time the map opens, KeyRouter.log gets one [map] line per used key.

1.4.0
-----
- Keyboard map: switchable US (ANSI, 104 keys) or UK (ISO, 105 keys) layout. Use the Layout
  button on the map, the Keyboard map section of the settings page, or Layout=US / UK in
  [KeyboardMap]. The UK map has the tall Enter, the # key, the extra \ key next to Left Shift
  and UK labels (AltGr, #).
- The extra ISO key (scan code 0x56, between Left Shift and Z) is now known everywhere: it shows on
  the map, DLL config files that use it are read, and it can be used as a key name ("ISO \").
- Key routing now installs only on Anniversary Edition (1.6 and newer, including 1.7.x). On SE
  1.5.x the function Key Router hooks has a different Address Library ID, so it refuses instead of
  risking a wrong hook (KeyRouter.log says so).
