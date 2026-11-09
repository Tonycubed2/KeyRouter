#pragma once

// KeyRouter.ini. Read once at load; the in-game settings page (SKSE Menu Framework) changes the
// values below while the game runs and writes them back with Save().
// Scalars are atomic because the input hook, the kill switch / map threads and the menu's render
// thread all read them. The lists have their own locks.
namespace Settings
{
	// [General]
	inline std::atomic<bool>          enabled{ true };              // needs a restart
	inline std::atomic<bool>          includeGameControls{ true };
	inline std::atomic<bool>          includeMouseButtons{ false };  // DLL config part needs a restart
	inline std::atomic<bool>          checkInMenus{ false };
	inline std::atomic<bool>          scanDllConfigs{ true };        // needs a restart
	inline std::atomic<bool>          pollHooks{ true };             // needs a restart
	inline std::atomic<bool>          directInputHooks{ true };      // needs a restart
	inline std::atomic<std::uint32_t> syntheticPressMs{ 50 };
	inline std::atomic<bool>          logKeys{ false };              // one log line per key press: who wanted it and what happened
	inline std::atomic<bool>          diagnostics{ false };
	inline std::atomic<bool>          quickConflict{ false };        // ConflictWindow=Quick: the wheel list instead of the message box          // [diag] lines: input hook rate, game state and handlers on each replayed press

	// [KillSwitch]
	inline std::atomic<bool>          killSwitchEnabled{ true };
	inline std::string                killSwitchKey = "K";           // only touched at load and by the menu
	inline std::atomic<std::uint32_t> killSwitchHoldSeconds{ 10 };

	// [KeyboardMap]
	inline std::atomic<bool>          mapEnabled{ true };
	inline std::string                mapKey = "Backspace";          // only touched at load and by the menu
	inline std::atomic<std::uint32_t> mapHoldSeconds{ 10 };
	inline std::atomic<int>           mapLayout{ 0 };  // kLayoutUS or kLayoutUK
	constexpr int                     kLayoutUS = 0;   // US ANSI 104 keys
	constexpr int                     kLayoutUK = 1;   // UK ISO 105 keys

	// [PassiveMods]: passiveMods = lowercase names used for matching, passiveNames = as written.
	// Same order and size. Hold listLock to read or change them.
	inline std::mutex               listLock;
	inline std::vector<std::string> passiveMods;
	inline std::vector<std::string> passiveNames;

	// [DllKeys]: lowercase DLL name (no .dll) and the raw key list. Read by the DLL config scan at
	// data load; changes need a restart.
	inline std::vector<std::pair<std::string, std::string>> dllKeyOverrides;
	// The [DllKeys] lines as they will be written by Save() (name as written, value). Only the
	// load and the menu touch this.
	inline std::vector<std::pair<std::string, std::string>> dllKeysToSave;

	// Limits shared by the loader and the menu
	constexpr std::uint32_t kPressMsMin = 30, kPressMsMax = 2000;
	constexpr std::uint32_t kKillMin = 3, kKillMax = 60;
	constexpr std::uint32_t kMapMin = 1, kMapMax = 60;

	void Load();

	// Writes every value back to KeyRouter.ini, keeping its comments and layout. False on error.
	bool Save();

	std::string StripDll(std::string a_name);
}
