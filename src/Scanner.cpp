#include "Scanner.h"

#include "Keys.h"
#include "Owners.h"
#include "Settings.h"

namespace Scanner
{
	namespace
	{
		namespace fs = std::filesystem;

		std::vector<Dll> g_dlls;

		constexpr std::uintmax_t kMaxFileSize = 2 * 1024 * 1024;

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		std::string StripWrapping(std::string a_text)
		{
			for (bool changed = true; changed && a_text.size() >= 2;) {
				changed = false;
				const char f = a_text.front();
				const char b = a_text.back();
				if ((f == '"' && b == '"') || (f == '\'' && b == '\'') || (f == '[' && b == ']') || (f == '{' && b == '}')) {
					a_text = Trim(std::string_view(a_text).substr(1, a_text.size() - 2));
					changed = true;
				}
			}
			return a_text;
		}

		bool IsConfigFile(const fs::path& a_path)
		{
			const auto ext = Owners::Lower(a_path.extension().string());
			return ext == ".ini" || ext == ".toml" || ext == ".json" || ext == ".yaml" || ext == ".yml";
		}

		// Translation / language files hold words shown in a mod's menu ("ctrlKey=Ctrl"), not settings
		bool IsTranslationFile(const fs::path& a_path)
		{
			constexpr std::array words{ "translation"sv, "language"sv, "locale"sv, "i18n"sv, "l10n"sv };
			const auto           file = Owners::Lower(a_path.filename().string());
			if (std::any_of(words.begin(), words.end(), [&](std::string_view w) { return file.find(w) != std::string::npos; })) {
				return true;
			}
			// themes: color tables ("KEYM": [255, 153, 76, 255] in Modex) are not hotkeys either
			constexpr std::array dirs{ "themes"sv, "theme"sv, "languages"sv, "language"sv, "lang"sv, "translations"sv, "translation"sv,
				"locales"sv, "locale"sv, "strings"sv, "i18n"sv, "l10n"sv };
			for (auto parent = a_path.parent_path(); !parent.empty() && parent != parent.parent_path(); parent = parent.parent_path()) {
				const auto name = Owners::Lower(parent.filename().string());
				if (std::find(dirs.begin(), dirs.end(), name) != dirs.end()) {
					return true;
				}
			}
			return false;
		}

		struct KeyValue
		{
			std::string name;
			std::string value;
		};

		// Reads "name = value" (ini, toml, yaml) or "\"name\": value" (json) from one line
		std::optional<KeyValue> ParseLine(std::string_view a_line)
		{
			const auto line = Trim(a_line);
			if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[' || line.starts_with("//")) {
				return std::nullopt;
			}

			bool        quoted = false;
			std::size_t sep = std::string::npos;
			for (std::size_t i = 0; i < line.size(); ++i) {
				const char c = line[i];
				if (c == '"') {
					quoted = !quoted;
				} else if (!quoted && (c == '=' || c == ':')) {
					sep = i;
					break;
				}
			}
			if (sep == std::string::npos || sep == 0) {
				return std::nullopt;
			}

			auto name = StripWrapping(Trim(std::string_view(line).substr(0, sep)));
			auto value = Trim(std::string_view(line).substr(sep + 1));
			if (!value.empty() && value[0] != '"') {
				for (const auto marker : { ";"sv, "#"sv, "//"sv }) {
					if (const auto pos = value.find(marker); pos != std::string::npos) {
						value.erase(pos);
					}
				}
			}
			value = Trim(value);
			while (!value.empty() && value.back() == ',') {
				value.pop_back();
				value = Trim(value);
			}
			value = StripWrapping(value);

			if (name.empty() || value.empty() || name.size() > 80) {
				return std::nullopt;
			}
			for (const char c : name) {
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != '-' && c != ' ') {
					return std::nullopt;
				}
			}
			return KeyValue{ std::move(name), std::move(value) };
		}

		// a_hotkeySection: the setting is in a section such as [Hotkeys] or [Keybinds], where the
		// names need not say "key" (SkyrimUpscaler: [Hotkeys] mTogglePDFrameWarp = NumpadAdd)
		bool LooksLikeHotkeySetting(const std::string& a_lowerName, bool a_hotkeySection = false)
		{
			constexpr std::array want{ "key"sv, "bind"sv, "shortcut"sv };
			constexpr std::array skip{ "keyword"sv, "keyframe"sv, "monkey"sv, "turkey"sv, "whiskey"sv, "gamepad"sv,
				"controller"sv, "joystick"sv, "xinput"sv, "dualsense"sv, "modifier"sv, "time"sv, "delay"sv, "duration"sv,
				"count"sv, "enable"sv, "disable"sv, "length"sv, "speed"sv, "threshold"sv, "sensitivity"sv, "color"sv,
				"colour"sv, "opacity"sv, "alpha"sv, "scale"sv, "size"sv, "chance"sv, "volume"sv, "cooldown"sv };

			const bool wanted = a_hotkeySection ||
			                    std::any_of(want.begin(), want.end(), [&](std::string_view w) { return a_lowerName.find(w) != std::string::npos; });
			if (!wanted) {
				return false;
			}
			return std::none_of(skip.begin(), skip.end(), [&](std::string_view s) { return a_lowerName.find(s) != std::string::npos; });
		}

		// "34", "0x22", "G", "Left Shift+G", "42,34", "LCtrl|F5"
		std::optional<Binding> ParseCombo(std::string_view a_value, bool a_vk, std::string_view a_separators)
		{
			std::vector<std::string> tokens;
			std::string              current;
			for (const char c : a_value) {
				if (a_separators.find(c) != std::string_view::npos) {
					tokens.push_back(Trim(current));
					current.clear();
				} else {
					current.push_back(c);
				}
			}
			tokens.push_back(Trim(current));
			std::erase_if(tokens, [](const std::string& t) { return t.empty(); });
			if (tokens.empty() || tokens.size() > 4) {
				return std::nullopt;
			}

			std::vector<std::uint32_t> codes;
			std::vector<std::uint8_t>  eitherSide;  // per token: both bits for "Alt"/"Ctrl"/"Shift", else 0
			for (const auto& t : tokens) {
				const auto code = Keys::ParseToken(t, a_vk);
				if (!code || !Keys::IsValidCode(*code)) {
					return std::nullopt;
				}
				codes.push_back(*code);
				eitherSide.push_back(Keys::EitherSideMask(t));
			}

			// The main key is the last token that is not a modifier, or else the last token
			// ("Left Ctrl+Right Ctrl" = Right Ctrl while Left Ctrl is held)
			std::size_t mainIndex = codes.size() - 1;
			for (std::size_t i = codes.size(); i-- > 0;) {
				if (Keys::ModifierBit(codes[i]) == 0) {
					mainIndex = i;
					break;
				}
			}
			const std::optional<std::uint32_t> main = codes[mainIndex];

			// 1 = Esc: almost always a boolean "on" value, not a hotkey
			if (*main <= 1) {
				return std::nullopt;
			}
			if (*main >= Keys::kMouseBase && !Settings::includeMouseButtons) {
				return std::nullopt;
			}

			Binding b;
			b.code = *main;
			b.origin = Origin{};
			b.origin->token = tokens[mainIndex];
			b.origin->vk = a_vk;
			for (std::size_t i = 0; i < codes.size(); ++i) {
				if (i == mainIndex) {
					continue;
				}
				const auto bit = Keys::ModifierBit(codes[i]);
				if (bit == 0) {
					return std::nullopt;  // two ordinary keys: not a combo Key Router understands
				}
				const std::uint8_t group = eitherSide[i] ? eitherSide[i] : bit;
				if (std::find(b.modGroups.begin(), b.modGroups.end(), group) == b.modGroups.end()) {
					b.modGroups.push_back(group);
				}
			}
			std::sort(b.modGroups.begin(), b.modGroups.end());
			return b;
		}

		void AddBinding(Dll& a_dll, Binding a_binding)
		{
			for (const auto& b : a_dll.keys) {
				if (b.code == a_binding.code && b.modGroups == a_binding.modGroups) {
					return;
				}
			}
			a_dll.keys.push_back(std::move(a_binding));
		}

		// a_mcmUser: for MCM Helper files, the user settings path when a_path is the defaults file, else ""
		void ScanFile(const fs::path& a_path, Dll& a_dll, std::set<std::string>* a_seenNames, bool a_skipSeen, const std::string* a_mcmUser = nullptr)
		{
			if (IsTranslationFile(a_path)) {
				return;
			}
			std::error_code ec;
			const auto      size = fs::file_size(a_path, ec);
			if (ec || size > kMaxFileSize) {
				return;
			}
			std::ifstream file(a_path);
			if (!file) {
				return;
			}

			const auto           label = a_path.filename().string();
			std::vector<Binding> found;      // this file's hotkeys
			std::vector<Binding> modifiers;  // settings that are only Shift, Ctrl or Alt
			std::string          line;
			std::string          section;
			std::size_t          lineNo = 0;
			for (; std::getline(file, line); ++lineNo) {
				if (const auto t = Trim(line); t.size() >= 2 && t.front() == '[' && t.back() == ']' && t.find('"') == std::string::npos) {
					section = Trim(std::string_view(t).substr(1, t.size() - 2));
					continue;
				}
				const auto kv = ParseLine(line);
				if (!kv) {
					continue;
				}
				const auto lowerName = Owners::Lower(kv->name);
				// "Open Menu Modifier": 42 (Modex) - the Shift/Ctrl/Alt held with this file's hotkeys.
				// 0 means none and is skipped by ParseCombo.
				if (lowerName.find("modifier") != std::string::npos && lowerName.find("enable") == std::string::npos &&
					lowerName.find("disable") == std::string::npos) {
					if (auto m = ParseCombo(kv->value, lowerName.find("vk") != std::string::npos, "+,|"sv);
						m && m->modGroups.empty() && Keys::ModifierBit(m->code) != 0) {
						m->from = std::format("{}: {}", label, kv->name);
						m->origin.reset();
						modifiers.push_back(std::move(*m));
					}
					continue;
				}
				const auto lowerSection = Owners::Lower(section);
				const bool hotkeySection = lowerSection.find("hotkey") != std::string::npos || lowerSection.find("keybind") != std::string::npos ||
				                           lowerSection.find("key bind") != std::string::npos || lowerSection.find("shortcut") != std::string::npos ||
				                           lowerSection == "keys";
				if (!LooksLikeHotkeySetting(lowerName, hotkeySection)) {
					continue;
				}
				if (a_seenNames) {
					if (a_skipSeen && a_seenNames->contains(lowerName)) {
						continue;
					}
					a_seenNames->insert(lowerName);
				}
				const bool vk = lowerName.find("vk") != std::string::npos;
				if (auto b = ParseCombo(kv->value, vk, "+,|"sv)) {
					b->from = std::format("{}: {}", label, kv->name);
					b->origin->path = a_path.generic_string();
					b->origin->line = lineNo;
					b->origin->setting = kv->name;
					b->origin->section = section;
					if (a_mcmUser) {
						b->origin->mcmDefaults = !a_mcmUser->empty();
						b->origin->userPath = *a_mcmUser;
					}
					if (b->modGroups.empty() && Keys::ModifierBit(b->code) != 0) {
						modifiers.push_back(std::move(*b));
					} else {
						found.push_back(std::move(*b));
					}
				}
			}

			// A setting that is only Shift, Ctrl or Alt ("ModKey = 29", "iPrimaryKey = 29" next to
			// "iSecondaryKey = 47") is the key held together with the mod's other keys, not a hotkey
			// on its own. Add it to every other hotkey of the same file.
			for (auto& b : found) {
				for (const auto& m : modifiers) {
					const auto bit = Keys::ModifierBit(m.code);
					if (std::find(b.modGroups.begin(), b.modGroups.end(), bit) == b.modGroups.end()) {
						b.modGroups.push_back(bit);
					}
				}
				std::sort(b.modGroups.begin(), b.modGroups.end());
				AddBinding(a_dll, std::move(b));
			}
			for (const auto& m : modifiers) {
				logger::info("{}: {} = {} is a modifier key, used with that file's other keys", a_dll.display, m.from, Keys::Name(m.code));
			}
		}

		bool NameMatches(const std::string& a_fileStemLower, const std::string& a_dllStem)
		{
			if (a_fileStemLower == a_dllStem) {
				return true;
			}
			if (!a_fileStemLower.starts_with(a_dllStem) || a_fileStemLower.size() <= a_dllStem.size()) {
				return false;
			}
			const char next = a_fileStemLower[a_dllStem.size()];
			return next == '_' || next == '-' || next == '.' || next == ' ';
		}

		void ScanDll(const Owners::Module& a_module, const std::vector<fs::directory_entry>& a_pluginDir, Dll& a_dll)
		{
			for (const auto& entry : a_pluginDir) {
				try {
					std::error_code ec;
					const auto&     path = entry.path();
					const auto      stemLower = Owners::Lower(path.stem().string());
					if (entry.is_regular_file(ec) && IsConfigFile(path) && NameMatches(stemLower, a_module.stem)) {
						ScanFile(path, a_dll, nullptr, false);
					} else if (entry.is_directory(ec) && Owners::Lower(path.filename().string()) == a_module.stem) {
						for (auto it = fs::recursive_directory_iterator(path, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
							if (it.depth() > 2) {
								it.disable_recursion_pending();
								continue;
							}
							if (it->is_regular_file(ec) && IsConfigFile(it->path())) {
								ScanFile(it->path(), a_dll, nullptr, false);
							}
						}
					}
				} catch (const std::exception& e) {
					logger::warn("Skipped a file while scanning {}: {}", a_module.fileName, e.what());
				}
			}

			// Data\Interface\<name>: some DLL mods keep their settings next to their UI files
			// (Modex: Data\Interface\Modex\user\settings.json, "Open Menu Keybind": 211)
			try {
				std::error_code ec;
				const fs::path  ui = fs::path("Data/Interface") / a_module.stem;
				if (fs::is_directory(ui, ec)) {
					for (auto it = fs::recursive_directory_iterator(ui, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
						if (it.depth() > 2) {
							it.disable_recursion_pending();
							continue;
						}
						if (it->is_regular_file(ec) && IsConfigFile(it->path())) {
							ScanFile(it->path(), a_dll, nullptr, false);
						}
					}
				}
			} catch (const std::exception& e) {
				logger::warn("Skipped Data/Interface files for {}: {}", a_module.fileName, e.what());
			}

			// MCM Helper: the player's settings win over the mod's defaults
			try {
				std::set<std::string> seen;
				const fs::path        user = fs::path("Data/MCM/Settings") / (a_module.stem + ".ini");
				const fs::path        defaults = fs::path("Data/MCM/Config") / a_module.stem / "settings.ini";
				std::error_code       ec;
				const std::string none;
				const std::string userPath = user.generic_string();
				if (fs::exists(user, ec)) {
					ScanFile(user, a_dll, &seen, false, &none);
				}
				if (fs::exists(defaults, ec)) {
					ScanFile(defaults, a_dll, &seen, true, &userPath);
				}
			} catch (const std::exception& e) {
				logger::warn("Skipped MCM settings for {}: {}", a_module.fileName, e.what());
			}
		}
	}

	void Run()
	{
		g_dlls.clear();

		if (Settings::scanDllConfigs) {
			std::vector<fs::directory_entry> pluginDir;
			std::error_code                  ec;
			for (auto it = fs::directory_iterator("Data/SKSE/Plugins", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
				pluginDir.push_back(*it);
			}

			const auto modules = Owners::SksePluginModules();
			logger::info("Scanning config files of {} SKSE plugin DLLs", modules.size());
			for (const auto& module : modules) {
				if (module.stem == "keyrouter") {
					continue;
				}
				Dll dll{ module.stem, module.fileName, {} };
				ScanDll(module, pluginDir, dll);
				if (!dll.keys.empty()) {
					g_dlls.push_back(std::move(dll));
				}
			}
		}

		// [DllKeys] corrections replace what the scan found for that DLL
		for (const auto& [stem, value] : Settings::dllKeyOverrides) {
			auto it = std::find_if(g_dlls.begin(), g_dlls.end(), [&](const Dll& d) { return d.stem == stem; });
			if (it == g_dlls.end()) {
				it = g_dlls.insert(g_dlls.end(), Dll{ stem, stem + ".dll", {} });
			}
			it->keys.clear();
			if (Owners::Lower(Trim(value)) != "none") {
				std::string item;
				auto        flush = [&]() {
                    if (const auto t = Trim(item); !t.empty()) {
                        if (auto b = ParseCombo(t, false, "+"sv)) {
                            b->from = "KeyRouter.ini [DllKeys]";
                            b->origin.reset();
                            AddBinding(*it, std::move(*b));
                        } else {
                            logger::warn("[DllKeys] {}: could not read key '{}'", stem, t);
                        }
                    }
                    item.clear();
				};
				for (const char c : value) {
					if (c == ',') {
						flush();
					} else {
						item.push_back(c);
					}
				}
				flush();
			}
		}
		std::erase_if(g_dlls, [](const Dll& d) { return d.keys.empty(); });

		for (const auto& dll : g_dlls) {
			for (const auto& b : dll.keys) {
				logger::info("DLL hotkey: {} uses {} ({})", dll.display, Keys::ComboName(b.code, b.modGroups), b.from);
			}
		}
		logger::info("{} DLL mods have hotkeys Key Router knows about", g_dlls.size());
	}

	bool Origin::Movable() const
	{
		if (path.empty() || setting.empty() || token.empty()) {
			return false;
		}
		if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
			return std::all_of(token.begin() + 2, token.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
		}
		return std::all_of(token.begin(), token.end(), [](char c) { return c >= '0' && c <= '9'; });
	}

	std::string Origin::FileName() const
	{
		return fs::path(mcmDefaults ? userPath : path).filename().string();
	}

	const std::vector<Dll>& Dlls()
	{
		return g_dlls;
	}
}
