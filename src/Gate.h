#pragma once

// Window-message gate. Some DLL mods take their hotkeys from the game window's key messages
// (WM_KEYDOWN / WM_KEYUP) in a window procedure they put on the game window, not from the game's
// input events. SkyrimUpscaler does this (checked in its code: its window procedure matches
// WM_KEYDOWN against its four hotkeys and acts on the first, non-repeated press).
// Skyrim itself reads the keyboard through DirectInput, so during play these messages only matter
// to such mods. The gate sits in front of them on the game window and, for a key some DLL mod
// wants, lets the message through, holds it until Key Router has decided about the press, or drops
// it when the press went to a script, the game or nobody.
namespace Gate
{
	void Install();  // kDataLoaded, after the router's hooks

	// The router has made decisions: look at held messages again (any thread)
	void Kick();
}
