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

	void Set(std::uint32_t a_code, std::vector<std::string> a_blocked, std::string a_forced);
	void Clear(std::uint32_t a_code);
	void ClearAll();
}
