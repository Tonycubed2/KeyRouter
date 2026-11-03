#pragma once

#include "Scanner.h"

// The core: watches every key press, finds every mod that wants that key, and when two or more
// want the same press, holds it back and asks the player which one gets it (or uses a remembered
// choice).
namespace Router
{
	void InstallHooks();   // plugin load: input dispatch hook + Papyrus event hook
	void OnDataLoaded();
	void Reset();          // new game / save loaded
	void OnPopupAnswer(std::uint32_t a_popupId, std::uint8_t a_button);

	// ---- keyboard map

	// How one mod's use of a key can be changed
	struct HowTo
	{
		std::string                    text;    // one line for the player
		std::optional<Scanner::Origin> origin;  // DLL config setting Key Router can rewrite (Movable())
		std::uint32_t                  code{ 0 };
		std::vector<std::uint8_t>      modGroups;  // Shift/Ctrl/Alt that stay with the key
	};

	struct KeyBind
	{
		std::string        name;     // "SkyUI_SE.esp", "DragonsEyeMinimap.dll", "Skyrim controls (Favorites)"
		std::string        source;   // "Papyrus", "DragonsEyeMinimap.ini: iControlKey", "game control"...
		std::string        combo;    // "" for the plain key, else "Left Ctrl+T" etc.
		bool               passive{ false };
		std::vector<HowTo> howTo;    // one per place the key is set
		bool               moved{ false };  // changed this session; active after a restart
	};

	struct KeyEntry
	{
		std::vector<KeyBind> plain;   // want the key on its own: these decide the color
		std::vector<KeyBind> combos;  // want it only together with Shift/Ctrl/Alt
		std::string          remembered;  // remembered choice for this key, if any
	};

	// Every key code Key Router knows (0-255 keyboard, 256-265 mouse), as seen during normal
	// play. Papyrus registrations are probed live: SKSE's key listener is run once per key in
	// capture mode, so nothing is sent to any script. Call on the main thread.
	std::vector<std::pair<std::uint32_t, KeyEntry>> BuildKeyMap();

	// False when key routing is off (KeyRouter.ini Enabled=0): the map then cannot ask the game
	// which scripts registered which key, and shows only DLL and game controls.
	bool HooksInstalled();
}
