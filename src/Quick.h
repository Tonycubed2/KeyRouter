#pragma once

// Quick mode for the conflict window ([General] ConflictWindow=Quick): instead of the game's
// message box, a small list of the competing mods at the mouse pointer. The mouse wheel moves the
// highlight (the wheel belongs to Key Router while the list is open), a left click or Enter picks,
// Esc means Nobody. The game is paused while it is open. No "Remember my choice" here.
// Drawn with SKSE Menu Framework; without it the regular window is used.
namespace Quick
{
	void Start();  // kPostLoad
	bool Available();

	// a_names: the mods, then "Nobody" last. The answer goes to Router::AnswerPopupIndex.
	void Open(std::uint32_t a_popupId, std::string a_title, std::vector<std::string> a_names);
	bool IsOpen();
	std::uint32_t OpenId();  // popup id of the open list, 0 when closed
	bool          OpenNoLock();
	// The list was opened but SKSE Menu Framework has not drawn it for a while: it is not on screen
	bool          Stalled();  // the list is open; safe from any thread, even inside the list's drawing

	// From the input frame while the list is open
	void Wheel(int a_steps);  // + up, - down
	void PickHighlighted();   // Enter

	void Close();  // the popup was answered or dropped elsewhere

	// True while the list is open and for a moment after it closed: mouse buttons and Enter are
	// kept from the game then (no sword swing from the click that picked)
	bool MouseExclusive();
}
