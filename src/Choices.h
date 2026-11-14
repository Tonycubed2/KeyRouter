#pragma once

// Remembered answers to the Key Router window ("Remember my choice: ON").
// Stored in Data\SKSE\Plugins\KeyRouter_Choices.ini, one line per key and set of competing mods:
//
//   [Choices]
//   ; L: DragonsEyeMinimap.dll, UnsearchedCorpsesIndicator.dll -> DragonsEyeMinimap.dll
//   0x26 = dll:dragonseyeminimap | dll:dragonseyeminimap, dll:unsearchedcorpsesindicator
//
// Left of "=": the key code. Right: the picked mod's id, or "nobody", then "|", then the ids of all
// mods that competed for the key. A choice applies only when exactly that set of mods competes
// again; if a mod is added or removed, the window asks again. Delete a line to be asked again.
namespace Choices
{
	struct Choice
	{
		std::string picked;  // a candidate key (see Router), or "nobody"
	};

	void Load();

	// a_set: the competing mods' keys, any order
	std::optional<Choice> Find(std::uint32_t a_code, const std::vector<std::string>& a_set);

	// Saves and writes the file at once. a_comment: readable line written above the entry.
	void Remember(std::uint32_t a_code, const std::vector<std::string>& a_set, const std::string& a_picked, const std::string& a_comment);

	// For the settings page
	struct Listed
	{
		std::uint32_t code{ 0 };
		std::string   set;      // ids, ", " joined
		std::string   picked;   // id or "nobody"
		std::string   comment;  // readable line ("L: A.dll, B.dll -> A.dll"), may be empty
	};
	std::vector<Listed> List();

	// Removes one entry (or all with a_code == 0 and an empty a_set) and writes the file.
	void Forget(std::uint32_t a_code, const std::string& a_set);

	// (1.6.4) Removes the entry for this key and set of competing mods (any order), if there is one.
	void ForgetSet(std::uint32_t a_code, const std::vector<std::string>& a_set);
}
