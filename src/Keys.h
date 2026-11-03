#pragma once

// Key codes are the ones SKSE and Papyrus use:
//   0-255   keyboard, DirectInput scan codes (DIK_*)
//   256-265 mouse buttons (256 = left, 257 = right, 258 = middle, ... 264/265 = wheel)
namespace Keys
{
	constexpr std::uint32_t kMouseBase = 256;
	constexpr std::uint32_t kMouseLast = 265;
	constexpr std::uint32_t kMaxCode = 512;

	// Each modifier key is its own bit: Left Alt and Right Alt are different keys
	enum Modifier : std::uint8_t
	{
		kLShift = 1 << 0,
		kRShift = 1 << 1,
		kLCtrl = 1 << 2,
		kRCtrl = 1 << 3,
		kLAlt = 1 << 4,
		kRAlt = 1 << 5,

		kAnyShift = kLShift | kRShift,
		kAnyCtrl = kLCtrl | kRCtrl,
		kAnyAlt = kLAlt | kRAlt
	};

	void Init();

	// "G", "Left Shift", "Mouse 4"
	std::string Name(std::uint32_t a_code);

	// "Left Ctrl+G", "Alt+F5". Each entry of a_modGroups is one modifier the combo needs:
	// a single bit (Left Alt) or both sides (Alt = either Alt key)
	std::string ComboName(std::uint32_t a_code, const std::vector<std::uint8_t>& a_modGroups);

	// Bit of one modifier key (Left Shift, Right Alt...), 0 for every other key
	std::uint8_t ModifierBit(std::uint32_t a_code);

	// Words that mean "either side": "Alt", "Ctrl", "Control", "Shift" (and "Menu" style aliases).
	// Returns both bits, or 0 if the token names one particular key.
	std::uint8_t EitherSideMask(std::string_view a_token);

	// true for a real keyboard key or a mouse button code
	bool IsValidCode(std::uint32_t a_code);

	// "34", "0x22", "G", "Left Shift", "LShift", "F5", "DIK_G", "VK_F5"...
	// a_vk: plain numbers are Windows virtual-key codes, not scan codes
	std::optional<std::uint32_t> ParseToken(std::string_view a_token, bool a_vk);

	// Windows virtual-key code -> key code (0 if none)
	std::uint32_t VkToDx(std::uint32_t a_vk);

	// Key code -> Windows virtual-key code (0 if none)
	int ToVirtualKey(std::uint32_t a_code);

	// Is the key held down right now, as Windows sees it (works even when the game misses the release)
	bool IsDownNow(std::uint32_t a_code);
}
