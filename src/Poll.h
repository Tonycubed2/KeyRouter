#pragma once

// Phase 3: DLL mods that read the keyboard themselves instead of through the game's input events.
//   - GetAsyncKeyState / GetKeyState / GetKeyboardState, hooked in the import table of each DLL
//     that has a known hotkey
//   - DirectInput keyboard reads (GetDeviceState / GetDeviceData) made from those DLLs
// The router tells this module, per key, which DLLs must see the key as up (blocked) and which
// one DLL must see it as down (forced) while a picked press is being delivered.
namespace Poll
{
	void InstallDirectInput();                                // plugin load
	void InstallImportHooks(const std::vector<std::string>& a_dllStems);  // data loaded

	// Also hook the keyboard reads of the DLLs in the game folder (ENB, ReShade...): they only get
	// the quick-list rule below. Data loaded, after InstallImportHooks.
	void InstallListGuard();

	// The quick conflict list opened: Enter, Up and Down belong to it. Every hooked module sees
	// them as up until the list is closed and they are let go (ENB's Shift+Enter editor key would
	// otherwise fire when Shift is the key in conflict and Enter picks).
	void ListOpened();

	void Set(std::uint32_t a_code, std::vector<std::string> a_blocked, std::string a_forced);

	// The router has looked at the current press of this key (true), or the press ended (false).
	// Until it has, a watched DLL that reads one of its own hotkeys itself sees that key as up
	// (at most kDeferMs): such a DLL can poll the keyboard between the key going down and the
	// router's next input frame, and would otherwise act on a press the router then holds back.
	void Seen(std::uint32_t a_code, bool a_seen);
	void Clear(std::uint32_t a_code);
	void ClearAll();
}
