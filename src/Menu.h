#pragma once

// Key Router's pages in the SKSE Menu Framework menu (default key F1):
//   Settings            every KeyRouter.ini option; changes apply at once and are saved
//   Remembered choices  the "Remember my choice" answers, with Forget buttons
//   Keyboard map        opens the keyboard map
namespace Menu
{
	void Register();  // kPostLoad

	// True while an SKSE Menu Framework window that blocks input is open (its menu, the map).
	// Key Router does not check key presses then, the same as in Skyrim's own menus.
	bool BlockingWindowOpen();
}
