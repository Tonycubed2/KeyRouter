#pragma once

// Keyboard map: hold the map key (default Backspace) for HoldSeconds (default 10) and a window
// shows a 104-key US keyboard plus the mouse buttons, each key colored by how many mods use it:
//   green  = no mod
//   orange = one mod
//   yellow = two or more, but all except one are listed in [PassiveMods] (no conflict)
//   red    = two or more that conflict
// Mouse wheel zooms, left-drag pans, a click selects a key; zoomed in far enough, the keys show
// the mods' names. Drawn with SKSE Menu Framework (required for the map; Key Router works
// without it).
namespace Viewer
{
	void Start();  // kPostLoad, after every plugin is loaded

	// Re-reads [KeyboardMap] Enabled / Key / HoldSeconds (the settings page calls this).
	// False when the key name could not be read; the old key then stays in use.
	bool Apply();

	// Builds the map and opens the window (from any thread). False when the window is not available.
	bool Open();
}
