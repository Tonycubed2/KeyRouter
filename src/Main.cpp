// Key Router 1.6.1
// Built against CommonLibSSE-NG 6.7.0 for Skyrim AE 1.6.1170 (and 1.7.99).
//
// Finds key presses that more than one mod wants (Papyrus scripts, SKSE DLL mods, and the
// game's own controls) and asks the player which one gets it (or uses a remembered answer).
// With SKSE Menu Framework: an in-game settings page and a keyboard map.
//
// Log: Documents\My Games\Skyrim Special Edition\SKSE\KeyRouter.log
// INI: Data\SKSE\Plugins\KeyRouter.ini

#include "Choices.h"
#include "Diag.h"
#include "Keys.h"
#include "KillSwitch.h"
#include "Menu.h"
#include "Poll.h"
#include "Quick.h"
#include "Router.h"
#include "Scanner.h"
#include "Settings.h"
#include "Viewer.h"

SKSEPluginInfo(
	.Version = REL::Version{ 1, 6, 1, 0 },
	.Name = "KeyRouter"sv,
	.Author = "tonycubed2"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

namespace
{
	bool g_routing = false;  // key routing hooks installed at load

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			// every plugin is loaded: SKSE Menu Framework can be asked for windows and pages
			Viewer::Start();
			Quick::Start();
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			{
				// Reading DLL config files is harmless, and the keyboard map uses it even with routing off
				Scanner::Run();
				if (!g_routing) {
					break;
				}
				std::vector<std::string> watched;
				for (const auto& dll : Scanner::Dlls()) {
					watched.push_back(dll.stem);
				}
				Poll::InstallImportHooks(watched);
				Diag::InstallWindowWatch();  // modules loaded since plugin load
				Router::OnDataLoaded();
				break;
			}
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			if (g_routing) {
				Router::Reset();
				Router::AskModsForKeys();
			}
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	logger::info("Key Router 1.6.1 loading; game runtime {}", REL::Module::get().version().string());

	Settings::Load();
	Keys::Init();
	Choices::Load();

	// The kill switch works even when the rest of Key Router is turned off
	KillSwitch::Start();

	if (Settings::enabled) {
		g_routing = Router::InstallHooks();
		if (g_routing) {
			Diag::Start();
			Diag::InstallWindowWatch();
			Poll::InstallDirectInput();
		}
	} else {
		logger::info("Key routing is off in KeyRouter.ini (the settings page and keyboard map still work)");
	}

	// Always listen: the settings page must be there to turn routing back on
	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		logger::critical("Could not register SKSE messaging listener");
		return false;
	}

	logger::info("Loaded");
	return true;
}
