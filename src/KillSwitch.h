#pragma once

// Holding the kill key (default K) for HoldSeconds (default 10) ends the Skyrim process.
// Runs on its own thread and reads the key straight from Windows, so it still works when the
// game is frozen (main thread stuck, hidden error box, endless loading screen). Meant for remote
// play (Moonlight/Sunshine) where Task Manager is out of reach.
namespace KillSwitch
{
	void Start();

	// Re-reads Enabled / Key / HoldSeconds from Settings (the settings page calls this).
	// Returns false when the key name could not be read; the old key then stays in use.
	bool Apply();
}
