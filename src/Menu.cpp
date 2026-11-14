#include "Menu.h"

#include "Choices.h"
#include "Diag.h"
#include "Keys.h"
#include "KillSwitch.h"
#include "Settings.h"
#include "Viewer.h"

#include <Windows.h>

#include "SKSEMenuFramework.h"

namespace Menu
{
	namespace
	{
		using namespace ImGuiMCP;

		std::atomic<bool> g_installed{ false };

		const ImVec4 kGood{ 0.45F, 0.85F, 0.50F, 1.0F };
		const ImVec4 kBad{ 1.0F, 0.40F, 0.35F, 1.0F };
		const ImVec4 kWarn{ 1.0F, 0.80F, 0.30F, 1.0F };
		const ImVec4 kGold{ 0.85F, 0.72F, 0.45F, 1.0F };

		// Values that only take effect after a restart, as they were when the game started
		struct Startup
		{
			bool                                             enabled, scanDllConfigs, pollHooks, directInputHooks, includeMouseButtons;
			std::vector<std::pair<std::string, std::string>> dllKeys;
		};
		Startup g_startup{};

		std::string g_status;  // last save result
		bool        g_statusOk = true;

		void Save()
		{
			g_statusOk = Settings::Save();
			g_status = g_statusOk ? "Saved to KeyRouter.ini" : "Could not write KeyRouter.ini (see KeyRouter.log)";
		}

		void Help(const char* a_text)
		{
			TextDisabled("%s", a_text);
		}

		bool Check(const char* a_label, std::atomic<bool>& a_value)
		{
			bool v = a_value;
			if (Checkbox(a_label, &v)) {
				a_value = v;
				return true;
			}
			return false;
		}

		// Slider: the value applies while you drag; it is saved when you let go
		bool Number(const char* a_label, std::atomic<std::uint32_t>& a_value, std::uint32_t a_min, std::uint32_t a_max)
		{
			int v = static_cast<int>(a_value.load());
			SetNextItemWidth(GetFontSize() * 12);
			if (SliderInt(a_label, &v, static_cast<int>(a_min), static_cast<int>(a_max))) {
				a_value = static_cast<std::uint32_t>(std::clamp(v, static_cast<int>(a_min), static_cast<int>(a_max)));
			}
			return IsItemDeactivatedAfterEdit();
		}

		// ---- key fields (name box + "press a key") ----

		struct KeyField
		{
			const char*        id;
			std::string*       setting;
			bool             (*apply)();
			std::array<char, 64> buf{};
			bool               loaded{ false };
			bool               capturing{ false };
			std::array<bool, 256> downAtStart{};
			std::string        error;
		};

		KeyField g_killKey{ "kill", &Settings::killSwitchKey, &KillSwitch::Apply };
		KeyField g_mapKey{ "map", &Settings::mapKey, &Viewer::Apply };

		void SetBuf(KeyField& a_f, const std::string& a_text)
		{
			a_f.buf.fill(0);
			std::memcpy(a_f.buf.data(), a_text.data(), std::min(a_text.size(), a_f.buf.size() - 1));
		}

		bool Accept(KeyField& a_f, const std::string& a_text)
		{
			const auto code = Keys::ParseToken(a_text, false);
			if (!code || *code == 0 || Keys::ToVirtualKey(*code) == 0) {
				a_f.error = std::format("'{}' is not a key name Key Router knows", a_text);
				return false;
			}
			const auto old = *a_f.setting;
			*a_f.setting = Keys::Name(*code);
			if (!a_f.apply()) {
				*a_f.setting = old;
				a_f.error = "That key could not be used";
				return false;
			}
			a_f.error.clear();
			SetBuf(a_f, *a_f.setting);
			return true;
		}

		bool DownVk(int a_vk) { return (static_cast<std::uint16_t>(GetAsyncKeyState(a_vk)) & 0x8000) != 0; }

		// true when the key was changed
		bool KeyEdit(KeyField& a_f)
		{
			if (!a_f.loaded) {
				SetBuf(a_f, *a_f.setting);
				a_f.loaded = true;
			}
			bool changed = false;
			PushID(a_f.id);
			SetNextItemWidth(GetFontSize() * 9);
			InputText("Key", a_f.buf.data(), a_f.buf.size());
			if (IsItemDeactivatedAfterEdit()) {
				changed = Accept(a_f, a_f.buf.data());
			}
			SameLine();
			if (!a_f.capturing) {
				if (Button("Press a key...")) {
					a_f.capturing = true;
					for (int vk = 0; vk < 256; ++vk) {
						a_f.downAtStart[vk] = DownVk(vk);
					}
				}
			} else {
				TextColored(kWarn, "Press the key now (click here to cancel)");
				if (IsItemClicked()) {
					a_f.capturing = false;
				}
				// keyboard only: mouse buttons, and the either-side Shift/Ctrl/Alt codes, are skipped
				for (int vk = 0x08; vk < 0xFF && a_f.capturing; ++vk) {
					if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
						continue;
					}
					const bool down = DownVk(vk);
					if (!down) {
						a_f.downAtStart[vk] = false;
						continue;
					}
					if (a_f.downAtStart[vk]) {
						continue;  // was already held when capture began
					}
					if (const auto dx = Keys::VkToDx(static_cast<std::uint32_t>(vk)); dx != 0) {
						a_f.capturing = false;
						changed = Accept(a_f, Keys::Name(dx));
					}
				}
			}
			if (!a_f.error.empty()) {
				TextColored(kBad, "%s", a_f.error.c_str());
			}
			PopID();
			return changed;
		}

		// ---- Settings page ----

		std::array<char, 128> g_newPassive{};
		std::array<char, 128> g_newDllName{};
		std::array<char, 256> g_newDllKeys{};

		std::vector<std::string> RestartNeeded()
		{
			std::vector<std::string> out;
			if (Settings::enabled != g_startup.enabled) {
				out.emplace_back("Key routing on/off");
			}
			if (Settings::scanDllConfigs != g_startup.scanDllConfigs) {
				out.emplace_back("Read DLL config files");
			}
			if (Settings::pollHooks != g_startup.pollHooks) {
				out.emplace_back("Watch DLLs that read the keyboard themselves");
			}
			if (Settings::directInputHooks != g_startup.directInputHooks) {
				out.emplace_back("Watch DLLs with their own DirectInput device");
			}
			if (Settings::includeMouseButtons != g_startup.includeMouseButtons) {
				out.emplace_back("Mouse buttons (the DLL config part)");
			}
			if (Settings::dllKeysToSave != g_startup.dllKeys) {
				out.emplace_back("DLL hotkey corrections");
			}
			return out;
		}

		void __stdcall RenderSettings()
		{
			bool save = false;

			if (const auto restart = RestartNeeded(); !restart.empty()) {
				std::string list;
				for (const auto& r : restart) {
					list += (list.empty() ? "" : ", ") + r;
				}
				TextColored(kWarn, "Saved. Restart Skyrim for these to take effect: %s", list.c_str());
			}
			if (!g_status.empty()) {
				TextColored(g_statusOk ? kGood : kBad, "%s", g_status.c_str());
			}

			SeparatorText("Key routing");
			save |= Check("Key routing on (restart)", Settings::enabled);
			Help("Off: Key Router never asks; the kill switch and keyboard map still work.");
			save |= Check("Count Skyrim's own controls as a competitor", Settings::includeGameControls);
			Help("Activate, Sprint, Ready Weapon and so on count as one of the mods that want the key.");
			save |= Check("Also check mouse buttons", Settings::includeMouseButtons);
			Help("Applies at once to scripts and Skyrim's controls; to DLL config files after a restart.");
			save |= Check("Also check while menus are open", Settings::checkInMenus);
			Help("Off: only during normal play. Skyrim's own controls are never counted in menus.");

			SeparatorText("Conflict window");
			{
				int mode = Settings::quickConflict ? 1 : 0;
				bool changed = RadioButton("Regular (window with buttons, Remember my choice)", &mode, 0);
				changed |= RadioButton("Quick (list at the mouse pointer: wheel to move, click or Enter to pick, Esc = nobody)", &mode, 1);
				if (changed && (mode == 1) != Settings::quickConflict) {
					Settings::quickConflict = mode == 1;
					save = true;
				}
				Help("Quick needs SKSE Menu Framework and has no Remember my choice. Remembered choices still apply in both modes.");
			}

			SeparatorText("Finding hotkeys");
			save |= Check("Read the config files of SKSE DLL mods (restart)", Settings::scanDllConfigs);
			save |= Check("Watch DLLs that read the keyboard themselves (restart)", Settings::pollHooks);
			Help("GetAsyncKeyState / GetKeyState / GetKeyboardState.");
			save |= Check("Watch DLLs that read the keyboard through their own DirectInput device (restart)", Settings::directInputHooks);

			SeparatorText("Delivering a pick");
			save |= Number("Tap length (ms)", Settings::syntheticPressMs, Settings::kPressMsMin, Settings::kPressMsMax);
			Help("When you have let go of the key before you pick, the picked mod gets a tap this long (30 to 2000).\n"
				 "Keep it short: many mods treat 0.15 s or longer as a hold. To give a mod a hold, keep holding the key while you click.");

			SeparatorText("Log");
			save |= Check("Log every key press (LogKeys)", Settings::logKeys);
			Help("One line per press in KeyRouter.log: which mods wanted it and what Key Router did. Turn off for normal play.");
			if (Check("Diagnostics", Settings::diagnostics)) {
				save = true;
				if (Settings::diagnostics) {
					Diag::Start();
				}
			}
			Help("[diag] lines in KeyRouter.log, for finding faults only. Turn off for normal play.");

			SeparatorText("Kill switch");
			save |= Check("Kill switch on", Settings::killSwitchEnabled);
			Help("Hold the key while Skyrim is the active window and Skyrim is ended at once. Nothing is saved.\n"
				 "A beep 3 seconds before the end. Works even when the game is frozen.");
			save |= KeyEdit(g_killKey);
			save |= Number("Hold seconds##kill", Settings::killSwitchHoldSeconds, Settings::kKillMin, Settings::kKillMax);

			SeparatorText("Keyboard map");
			save |= Check("Keyboard map on", Settings::mapEnabled);
			Help("Hold the key and the keyboard map opens (it can also be opened from the Keyboard map page).");
			save |= KeyEdit(g_mapKey);
			save |= Number("Hold seconds##map", Settings::mapHoldSeconds, Settings::kMapMin, Settings::kMapMax);
			{
				int layout = Settings::mapLayout;
				Text("Layout:");
				SameLine();
				bool changed = RadioButton("US (ANSI, 104 keys)", &layout, Settings::kLayoutUS);
				SameLine();
				changed |= RadioButton("UK (ISO, 105 keys)", &layout, Settings::kLayoutUK);
				if (changed && layout != Settings::mapLayout) {
					Settings::mapLayout = layout;
					save = true;
				}
			}

			SeparatorText("Passive mods");
			Help("These mods still get every key, but never count as a competitor. Only for a mod that just\n"
				 "watches a key without doing anything of its own with it. Applies at once.");
			{
				std::vector<std::string> names;
				{
					std::scoped_lock lock(Settings::listLock);
					names = Settings::passiveNames;
				}
				int remove = -1;
				for (int i = 0; i < static_cast<int>(names.size()); ++i) {
					PushID(i);
					if (SmallButton("Remove")) {
						remove = i;
					}
					SameLine();
					Text("%s", names[i].c_str());
					PopID();
				}
				if (remove >= 0) {
					std::scoped_lock lock(Settings::listLock);
					if (remove < static_cast<int>(Settings::passiveMods.size())) {
						Settings::passiveMods.erase(Settings::passiveMods.begin() + remove);
						Settings::passiveNames.erase(Settings::passiveNames.begin() + remove);
					}
					save = true;
				}
				SetNextItemWidth(GetFontSize() * 14);
				InputText("##newpassive", g_newPassive.data(), g_newPassive.size());
				SameLine();
				if (Button("Add passive mod")) {
					std::string name = g_newPassive.data();
					name.erase(0, name.find_first_not_of(" \t"));
					name.erase(name.find_last_not_of(" \t") + 1);
					if (!name.empty()) {
						std::string lower = name;
						for (auto& c : lower) {
							c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
						}
						std::scoped_lock lock(Settings::listLock);
						if (std::find(Settings::passiveMods.begin(), Settings::passiveMods.end(), lower) == Settings::passiveMods.end()) {
							Settings::passiveMods.push_back(lower);
							Settings::passiveNames.push_back(name);
							save = true;
						}
					}
					g_newPassive.fill(0);
				}
				Help("Plugin or DLL file name, for example TKDodge.esp or SomeHelper.dll. Skyrim = the game's own controls.");
			}

			SeparatorText("DLL hotkey corrections");
			Help("Corrects or adds an SKSE DLL mod's hotkeys when its config file could not be read or was read wrong.\n"
				 "Keys comma separated: G, F5, Left Shift+G, 0x22. Left/Right Alt/Ctrl/Shift are separate keys;\n"
				 "plain Alt/Ctrl/Shift means either side. none = that DLL has no hotkeys. Applies after a restart.");
			{
				int remove = -1;
				for (int i = 0; i < static_cast<int>(Settings::dllKeysToSave.size()); ++i) {
					auto& [name, keys] = Settings::dllKeysToSave[i];
					PushID(1000 + i);
					if (SmallButton("Remove")) {
						remove = i;
					}
					SameLine();
					Text("%s =", name.c_str());
					SameLine();
					std::array<char, 256> buf{};
					std::memcpy(buf.data(), keys.data(), std::min(keys.size(), buf.size() - 1));
					SetNextItemWidth(GetFontSize() * 16);
					InputText("##keys", buf.data(), buf.size());
					if (IsItemDeactivatedAfterEdit()) {
						keys = buf.data();
						save = true;
					}
					PopID();
				}
				if (remove >= 0) {
					Settings::dllKeysToSave.erase(Settings::dllKeysToSave.begin() + remove);
					save = true;
				}
				SetNextItemWidth(GetFontSize() * 9);
				InputText("DLL name##newdll", g_newDllName.data(), g_newDllName.size());
				SameLine();
				SetNextItemWidth(GetFontSize() * 12);
				InputText("Keys##newdll", g_newDllKeys.data(), g_newDllKeys.size());
				SameLine();
				if (Button("Add correction")) {
					const std::string name = g_newDllName.data();
					const std::string keys = g_newDllKeys.data();
					const auto        stem = Settings::StripDll(name);
					if (!stem.empty() && !keys.empty()) {
						auto it = std::find_if(Settings::dllKeysToSave.begin(), Settings::dllKeysToSave.end(),
							[&](const auto& d) { return Settings::StripDll(d.first) == stem; });
						if (it != Settings::dllKeysToSave.end()) {
							it->second = keys;
						} else {
							Settings::dllKeysToSave.emplace_back(name, keys);
						}
						save = true;
						g_newDllName.fill(0);
						g_newDllKeys.fill(0);
					}
				}
			}

			if (save) {
				Save();
			}
		}

		// ---- Remembered choices page ----

		void __stdcall RenderChoices()
		{
			Help("Answers saved with \"Remember my choice: ON\". One is used only when exactly the same mods want that key again;\n"
				 "if a mod is added or removed, Key Router asks again. Forget an answer to be asked again next time.");
			const auto list = Choices::List();
			if (list.empty()) {
				Spacing();
				Text("No remembered choices.");
				return;
			}
			if (Button("Forget all")) {
				Choices::Forget(0, {});
				return;
			}
			if (BeginTable("choices", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, GetFontSize() * 6);
				TableSetupColumn("Goes to", ImGuiTableColumnFlags_WidthStretch, 1.0F);
				TableSetupColumn("When these mods want it", ImGuiTableColumnFlags_WidthStretch, 2.0F);
				TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, GetFontSize() * 4);
				TableHeadersRow();
				for (int i = 0; i < static_cast<int>(list.size()); ++i) {
					const auto& c = list[i];
					PushID(i);
					TableNextRow();
					TableNextColumn();
					Text("%s", Keys::Name(c.code).c_str());
					TableNextColumn();
					TextColored(kGold, "%s", c.picked.c_str());
					TableNextColumn();
					TextWrapped("%s", c.comment.empty() ? c.set.c_str() : c.comment.c_str());
					TableNextColumn();
					if (SmallButton("Forget")) {
						Choices::Forget(c.code, c.set);
					}
					PopID();
				}
				EndTable();
			}
		}

		// ---- Keyboard map page ----

		void __stdcall RenderMap()
		{
			Help("A map of the keyboard and mouse: green = no mod, orange = one mod, yellow = shared but no conflict,\n"
				 "red = conflict, blue corner = remembered choice. Mouse wheel zooms, drag moves, click a key for details,\n"
				 "double-click a key to pick which mod gets it during play (or nobody).");
			Spacing();
			if (Button("Open keyboard map")) {
				if (!Viewer::Open()) {
					g_status = "The keyboard map window is not available (see KeyRouter.log)";
					g_statusOk = false;
				}
			}
			if (Settings::mapEnabled) {
				Text("Or hold %s for %u seconds during play.", Settings::mapKey.c_str(), Settings::mapHoldSeconds.load());
			}
		}
	}

	void Register()
	{
		g_startup = { Settings::enabled, Settings::scanDllConfigs, Settings::pollHooks, Settings::directInputHooks,
			Settings::includeMouseButtons, Settings::dllKeysToSave };

		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed: no in-game settings page");
			return;
		}
		SKSEMenuFramework::SetSection("Key Router");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		SKSEMenuFramework::AddSectionItem("Remembered choices", RenderChoices);
		SKSEMenuFramework::AddSectionItem("Keyboard map", RenderMap);
		g_installed = true;
		logger::info("Settings page added to the SKSE Menu Framework menu");
	}

	bool BlockingWindowOpen()
	{
		return g_installed && SKSEMenuFramework::IsAnyBlockingWindowOpened();
	}
}
