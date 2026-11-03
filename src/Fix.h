#pragma once

#include "Scanner.h"

// Changes a DLL mod's hotkey in its own config file (the keyboard map's "Move" button).
// Only the main key is changed; Shift/Ctrl/Alt written in the same setting stay. The mod reads
// its file when the game starts, so the new key works after a restart.
namespace Fix
{
	struct Result
	{
		bool        ok{ false };
		std::string message;  // shown to the player
		std::string newToken;  // the value now written, for a later move of the same setting
	};

	Result MoveKey(const Scanner::Origin& a_origin, std::uint32_t a_newCode);
}
