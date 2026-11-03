#include "Settings.h"

namespace Settings
{
	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/KeyRouter.ini";

		std::string Trim(std::string a_s)
		{
			auto notSpace = [](unsigned char c) { return !std::isspace(c); };
			a_s.erase(a_s.begin(), std::find_if(a_s.begin(), a_s.end(), notSpace));
			a_s.erase(std::find_if(a_s.rbegin(), a_s.rend(), notSpace).base(), a_s.end());
			return a_s;
		}

		std::string Lower(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_s;
		}

		struct Entry
		{
			std::string section;
			std::string key;     // lowercase
			std::string rawKey;  // as written
			std::string value;
		};

		std::vector<Entry> ReadIni()
		{
			std::vector<Entry> entries;
			std::ifstream      file(kIniPath);
			if (!file) {
				logger::warn("{} not found, using defaults", kIniPath);
				return entries;
			}

			std::string section;
			std::string line;
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.empty() || line.front() == ';' || line.front() == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = Lower(Trim(line.substr(1, line.size() - 2)));
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos || eq == 0) {
					continue;
				}
				auto value = line.substr(eq + 1);
				if (const auto c = value.find(';'); c != std::string::npos) {
					value.erase(c);
				}
				auto rawKey = Trim(line.substr(0, eq));
				entries.push_back({ section, Lower(rawKey), rawKey, Trim(value) });
			}
			return entries;
		}

		bool ToBool(const Entry& a_entry, bool a_default)
		{
			const auto v = Lower(a_entry.value);
			if (v == "1" || v == "true" || v == "yes" || v == "on") {
				return true;
			}
			if (v == "0" || v == "false" || v == "no" || v == "off") {
				return false;
			}
			logger::warn("Bad value for {}: '{}', keeping {}", a_entry.key, a_entry.value, a_default);
			return a_default;
		}

		void ReadNumber(const Entry& a_entry, std::atomic<std::uint32_t>& a_out, std::uint32_t a_min, std::uint32_t a_max)
		{
			std::uint32_t value = 0;
			const auto    res = std::from_chars(a_entry.value.data(), a_entry.value.data() + a_entry.value.size(), value);
			if (res.ec == std::errc{}) {
				a_out = std::clamp(value, a_min, a_max);
			} else {
				logger::warn("Bad value for [{}] {}: '{}', keeping {}", a_entry.section, a_entry.rawKey, a_entry.value, a_out.load());
			}
		}

		// ---- writing ----

		struct Scalar
		{
			const char* section;  // lowercase
			const char* key;      // as written into a new line
			std::string value;
		};

		std::vector<Scalar> Scalars()
		{
			auto b = [](bool v) { return std::string(v ? "1" : "0"); };
			return {
				{ "general", "Enabled", b(enabled) },
				{ "general", "IncludeGameControls", b(includeGameControls) },
				{ "general", "IncludeMouseButtons", b(includeMouseButtons) },
				{ "general", "CheckInMenus", b(checkInMenus) },
				{ "general", "ScanDllConfigs", b(scanDllConfigs) },
				{ "general", "PollHooks", b(pollHooks) },
				{ "general", "DirectInputHooks", b(directInputHooks) },
				{ "general", "SyntheticPressMs", std::to_string(syntheticPressMs.load()) },
				{ "general", "LogKeys", b(logKeys) },
				{ "general", "Diagnostics", b(diagnostics) },
				{ "killswitch", "Enabled", b(killSwitchEnabled) },
				{ "killswitch", "Key", killSwitchKey },
				{ "killswitch", "HoldSeconds", std::to_string(killSwitchHoldSeconds.load()) },
				{ "keyboardmap", "Enabled", b(mapEnabled) },
				{ "keyboardmap", "Key", mapKey },
				{ "keyboardmap", "HoldSeconds", std::to_string(mapHoldSeconds.load()) },
				{ "keyboardmap", "Layout", mapLayout == kLayoutUK ? "UK" : "US" },
			};
		}

		const char* SectionTitle(const std::string& a_lower)
		{
			if (a_lower == "general") {
				return "General";
			}
			if (a_lower == "killswitch") {
				return "KillSwitch";
			}
			if (a_lower == "keyboardmap") {
				return "KeyboardMap";
			}
			if (a_lower == "passivemods") {
				return "PassiveMods";
			}
			return "DllKeys";
		}
	}

	std::string StripDll(std::string a_name)
	{
		a_name = Lower(Trim(a_name));
		if (a_name.ends_with(".dll")) {
			a_name.resize(a_name.size() - 4);
		}
		return a_name;
	}

	void Load()
	{
		for (const auto& e : ReadIni()) {
			if (e.section == "general") {
				if (e.key == "enabled") {
					enabled = ToBool(e, enabled);
				} else if (e.key == "includegamecontrols") {
					includeGameControls = ToBool(e, includeGameControls);
				} else if (e.key == "includemousebuttons") {
					includeMouseButtons = ToBool(e, includeMouseButtons);
				} else if (e.key == "checkinmenus") {
					checkInMenus = ToBool(e, checkInMenus);
				} else if (e.key == "scandllconfigs") {
					scanDllConfigs = ToBool(e, scanDllConfigs);
				} else if (e.key == "pollhooks") {
					pollHooks = ToBool(e, pollHooks);
				} else if (e.key == "directinputhooks") {
					directInputHooks = ToBool(e, directInputHooks);
				} else if (e.key == "diagnostics") {
					diagnostics = ToBool(e, diagnostics);
				} else if (e.key == "logkeys") {
					logKeys = ToBool(e, logKeys);
				} else if (e.key == "syntheticpressms") {
					ReadNumber(e, syntheticPressMs, kPressMsMin, kPressMsMax);
				}
			} else if (e.section == "killswitch") {
				if (e.key == "enabled") {
					killSwitchEnabled = ToBool(e, killSwitchEnabled);
				} else if (e.key == "key") {
					if (!e.value.empty()) {
						killSwitchKey = e.value;
					}
				} else if (e.key == "holdseconds") {
					ReadNumber(e, killSwitchHoldSeconds, kKillMin, kKillMax);
				}
			} else if (e.section == "keyboardmap") {
				if (e.key == "enabled") {
					mapEnabled = ToBool(e, mapEnabled);
				} else if (e.key == "key") {
					if (!e.value.empty()) {
						mapKey = e.value;
					}
				} else if (e.key == "holdseconds") {
					ReadNumber(e, mapHoldSeconds, kMapMin, kMapMax);
				} else if (e.key == "layout") {
					const auto v = Lower(e.value);
					if (v == "us" || v == "ansi") {
						mapLayout = kLayoutUS;
					} else if (v == "uk" || v == "iso") {
						mapLayout = kLayoutUK;
					} else {
						logger::warn("Bad value for [KeyboardMap] Layout: '{}' (US or UK), keeping {}", e.value, mapLayout == kLayoutUK ? "UK" : "US");
					}
				}
			} else if (e.section == "passivemods") {
				if (ToBool(e, false)) {
					std::scoped_lock lock(listLock);
					passiveMods.push_back(e.key);
					passiveNames.push_back(e.rawKey);
				}
			} else if (e.section == "dllkeys") {
				dllKeyOverrides.emplace_back(StripDll(e.key), e.value);
				dllKeysToSave.emplace_back(e.rawKey, e.value);
			}
		}

		logger::info("Settings: Enabled={} IncludeGameControls={} IncludeMouseButtons={} CheckInMenus={}",
			enabled.load(), includeGameControls.load(), includeMouseButtons.load(), checkInMenus.load());
		logger::info("Settings: ScanDllConfigs={} PollHooks={} DirectInputHooks={} SyntheticPressMs={} LogKeys={} Diagnostics={}",
			scanDllConfigs.load(), pollHooks.load(), directInputHooks.load(), syntheticPressMs.load(), logKeys.load(), diagnostics.load());
		logger::info("Settings: KillSwitch Enabled={} Key={} HoldSeconds={}", killSwitchEnabled.load(), killSwitchKey, killSwitchHoldSeconds.load());
		logger::info("Settings: KeyboardMap Enabled={} Key={} HoldSeconds={} Layout={}", mapEnabled.load(), mapKey, mapHoldSeconds.load(),
			mapLayout == kLayoutUK ? "UK" : "US");
		{
			std::scoped_lock lock(listLock);
			for (const auto& name : passiveMods) {
				logger::info("Passive (never a competitor): {}", name);
			}
		}
		for (const auto& [stem, value] : dllKeyOverrides) {
			logger::info("[DllKeys] {} = {}", stem, value);
		}
	}

	bool Save()
	{
		// Current values
		auto scalars = Scalars();
		std::vector<bool> scalarDone(scalars.size(), false);

		std::vector<std::pair<std::string, std::string>> passive;  // lowercase, as written
		{
			std::scoped_lock lock(listLock);
			for (std::size_t i = 0; i < passiveMods.size(); ++i) {
				passive.emplace_back(passiveMods[i], passiveNames[i]);
			}
		}
		std::vector<bool> passiveDone(passive.size(), false);
		std::vector<bool> dllDone(dllKeysToSave.size(), false);

		// Old file
		std::vector<std::string> in;
		{
			std::ifstream file(kIniPath);
			std::string   line;
			while (file && std::getline(file, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				in.push_back(line);
			}
		}

		std::vector<std::string> out;
		std::set<std::string>    seenSections;
		std::string              section;

		// Lines still missing from a section go after its last non-blank line
		auto flushSection = [&]() {
			if (section.empty()) {
				return;
			}
			std::vector<std::string> add;
			for (std::size_t i = 0; i < scalars.size(); ++i) {
				if (!scalarDone[i] && section == scalars[i].section) {
					add.push_back(std::format("{}={}", scalars[i].key, scalars[i].value));
					scalarDone[i] = true;
				}
			}
			if (section == "passivemods") {
				for (std::size_t i = 0; i < passive.size(); ++i) {
					if (!passiveDone[i]) {
						add.push_back(passive[i].second + " = 1");
						passiveDone[i] = true;
					}
				}
			} else if (section == "dllkeys") {
				for (std::size_t i = 0; i < dllKeysToSave.size(); ++i) {
					if (!dllDone[i]) {
						add.push_back(dllKeysToSave[i].first + " = " + dllKeysToSave[i].second);
						dllDone[i] = true;
					}
				}
			}
			if (add.empty()) {
				return;
			}
			auto at = out.end();
			while (at != out.begin() && Trim(*(at - 1)).empty()) {
				--at;
			}
			out.insert(at, add.begin(), add.end());
		};

		for (const auto& raw : in) {
			const auto t = Trim(raw);
			if (!t.empty() && t.front() == '[' && t.back() == ']') {
				flushSection();
				section = Lower(Trim(t.substr(1, t.size() - 2)));
				seenSections.insert(section);
				out.push_back(raw);
				continue;
			}
			const auto eq = t.find('=');
			if (t.empty() || t.front() == ';' || t.front() == '#' || eq == std::string::npos || eq == 0) {
				out.push_back(raw);
				continue;
			}
			const auto rawKey = Trim(t.substr(0, eq));
			const auto key = Lower(rawKey);

			if (section == "passivemods") {
				// keep the line where it is if the mod is still in the list, drop it otherwise
				auto it = std::find_if(passive.begin(), passive.end(), [&](const auto& p) { return p.first == key; });
				if (it != passive.end() && !passiveDone[it - passive.begin()]) {
					passiveDone[it - passive.begin()] = true;
					out.push_back(it->second + " = 1");
				}
				continue;
			}
			if (section == "dllkeys") {
				auto it = std::find_if(dllKeysToSave.begin(), dllKeysToSave.end(),
					[&](const auto& d) { return StripDll(d.first) == StripDll(rawKey); });
				if (it != dllKeysToSave.end() && !dllDone[it - dllKeysToSave.begin()]) {
					dllDone[it - dllKeysToSave.begin()] = true;
					out.push_back(it->first + " = " + it->second);
				}
				continue;
			}
			bool replaced = false;
			for (std::size_t i = 0; i < scalars.size(); ++i) {
				if (section == scalars[i].section && key == Lower(scalars[i].key)) {
					if (!scalarDone[i]) {
						out.push_back(std::format("{}={}", rawKey, scalars[i].value));
						scalarDone[i] = true;
					}
					replaced = true;  // a second copy of the same key is dropped
					break;
				}
			}
			if (!replaced) {
				out.push_back(raw);  // unknown key: leave it
			}
		}
		flushSection();

		// Sections the file did not have at all
		for (const char* s : { "general", "killswitch", "keyboardmap", "passivemods", "dllkeys" }) {
			if (seenSections.contains(s)) {
				continue;
			}
			const bool needed =
				std::any_of(scalars.begin(), scalars.end(), [&](const Scalar& x) { return std::string_view(x.section) == s; }) ||
				(std::string_view(s) == "passivemods" && !passive.empty()) || (std::string_view(s) == "dllkeys" && !dllKeysToSave.empty());
			if (!needed) {
				continue;
			}
			if (!out.empty() && !Trim(out.back()).empty()) {
				out.emplace_back();
			}
			out.push_back(std::format("[{}]", SectionTitle(s)));
			section = s;
			flushSection();
		}

		std::ofstream file(kIniPath, std::ios::trunc);
		if (!file) {
			logger::error("Could not write {}", kIniPath);
			return false;
		}
		for (const auto& line : out) {
			file << line << "\n";
		}
		file.close();
		if (!file) {
			logger::error("Error while writing {}", kIniPath);
			return false;
		}
		logger::info("Settings saved to {}", kIniPath);
		return true;
	}
}
