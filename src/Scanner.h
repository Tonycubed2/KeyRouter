#pragma once

// Learns which keys SKSE DLL mods want, by reading their config files
// (Data\SKSE\Plugins\<Name>.ini/.toml/.json, a <Name> folder there, and MCM Helper settings),
// plus the player's corrections in KeyRouter.ini [DllKeys].
namespace Scanner
{
	// Where a hotkey was read, so Key Router can change it there
	struct Origin
	{
		std::string   path;     // file as opened ("Data/SKSE/Plugins/Foo.ini")
		std::size_t   line{ 0 };  // 0-based line number in that file
		std::string   setting;  // "ToggleKey"
		std::string   section;  // ini/toml section the line is in ("" when none)
		std::string   token;    // the main key exactly as written ("38", "0x26", "L")
		bool          vk{ false };  // the setting holds Windows virtual-key codes, not DirectX scan codes
		bool          mcmDefaults{ false };  // read from MCM Helper's defaults: changes go to userPath
		std::string   userPath;  // MCM Helper user settings file for mcmDefaults

		// Key Router only rewrites numbers: key names are spelled differently by every mod
		[[nodiscard]] bool Movable() const;
		[[nodiscard]] std::string FileName() const;
	};

	struct Binding
	{
		std::uint32_t             code{ 0 };
		std::vector<std::uint8_t> modGroups;  // each must be held: one key (Left Alt) or either side (Alt)
		std::string               from;       // "Foo.ini: ToggleKey"
		std::optional<Origin>     origin;     // none for KeyRouter.ini [DllKeys] corrections
	};

	struct Dll
	{
		std::string          stem;     // "foo"
		std::string          display;  // "Foo.dll"
		std::vector<Binding> keys;
	};

	void Run();

	// DLLs with at least one key
	const std::vector<Dll>& Dlls();
}
