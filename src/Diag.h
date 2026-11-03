#pragma once

// Diagnostics (KeyRouter.ini [General] Diagnostics=1). Writes facts to KeyRouter.log so the
// dispatch path can be checked against what the game really does:
//   - once a second: how many times Key Router's input hook ran, how many of those frames had no
//     input events, and the longest gap between two calls
//   - for every press Key Router replays: the game state the Favorites and player-controls code
//     checks first (UI pause count, open menus, input context), every handler in the game's
//     MenuControls and PlayerControls lists with its owning DLL and its "enabled" byte, and
//     which input listeners were called with the replayed event
namespace Diag
{
	void Start();

	// input hook, called on every call (any thread)
	void CountHookCall(bool a_empty);

	// a_where: "in frame" or "direct"
	void TraceState(std::string_view a_what, std::uint32_t a_code, std::string_view a_where);

	// one input listener got (or did not get) the replayed event
	void TraceListener(std::string_view a_owner, bool a_sawIt, int a_result);

	// Where the input hook is right now. If one call stays busy for over 2 seconds the
	// diagnostics thread logs the step it is stuck in. a_what and a_detail must stay valid
	// (string literals or cached owner names).
	// Every DirectInput keyboard/mouse read in the game (the game's own poll reads the keyboard
	// this way). Counted per second next to the input hook count: if the reads go on while the
	// input hook stops, something between the poll and Key Router's hook stops calling it.
	void CountDirectInputRead();

	// Logs every SetWindowPos / MoveWindow / SetWindowPlacement call on Skyrim's own windows,
	// with the DLL that made the call. Installed in every module loaded at the time.
	void InstallWindowWatch();

	void HookEnter();
	void HookStep(const char* a_what, const char* a_detail);
	void HookLeave();
}
