#include "Keys.h"

#include <Windows.h>

namespace Keys
{
	namespace
	{
		struct NameEntry
		{
			std::uint32_t code;
			const char*   name;
		};

		constexpr NameEntry kNames[] = {
			{ 0x01, "Esc" }, { 0x02, "1" }, { 0x03, "2" }, { 0x04, "3" }, { 0x05, "4" }, { 0x06, "5" },
			{ 0x07, "6" }, { 0x08, "7" }, { 0x09, "8" }, { 0x0A, "9" }, { 0x0B, "0" }, { 0x0C, "-" },
			{ 0x0D, "=" }, { 0x0E, "Backspace" }, { 0x0F, "Tab" }, { 0x10, "Q" }, { 0x11, "W" },
			{ 0x12, "E" }, { 0x13, "R" }, { 0x14, "T" }, { 0x15, "Y" }, { 0x16, "U" }, { 0x17, "I" },
			{ 0x18, "O" }, { 0x19, "P" }, { 0x1A, "[" }, { 0x1B, "]" }, { 0x1C, "Enter" },
			{ 0x1D, "Left Ctrl" }, { 0x1E, "A" }, { 0x1F, "S" }, { 0x20, "D" }, { 0x21, "F" },
			{ 0x22, "G" }, { 0x23, "H" }, { 0x24, "J" }, { 0x25, "K" }, { 0x26, "L" }, { 0x27, ";" },
			{ 0x28, "'" }, { 0x29, "~" }, { 0x2A, "Left Shift" }, { 0x2B, "\\" }, { 0x2C, "Z" },
			{ 0x2D, "X" }, { 0x2E, "C" }, { 0x2F, "V" }, { 0x30, "B" }, { 0x31, "N" }, { 0x32, "M" },
			{ 0x33, "," }, { 0x34, "." }, { 0x35, "/" }, { 0x36, "Right Shift" }, { 0x37, "Num *" },
			{ 0x38, "Left Alt" }, { 0x39, "Space" }, { 0x3A, "Caps Lock" }, { 0x3B, "F1" },
			{ 0x3C, "F2" }, { 0x3D, "F3" }, { 0x3E, "F4" }, { 0x3F, "F5" }, { 0x40, "F6" },
			{ 0x41, "F7" }, { 0x42, "F8" }, { 0x43, "F9" }, { 0x44, "F10" }, { 0x45, "Num Lock" },
			{ 0x46, "Scroll Lock" }, { 0x47, "Num 7" }, { 0x48, "Num 8" }, { 0x49, "Num 9" },
			{ 0x4A, "Num -" }, { 0x4B, "Num 4" }, { 0x4C, "Num 5" }, { 0x4D, "Num 6" },
			{ 0x4E, "Num +" }, { 0x4F, "Num 1" }, { 0x50, "Num 2" }, { 0x51, "Num 3" },
			{ 0x52, "Num 0" }, { 0x53, "Num ." }, { 0x57, "F11" }, { 0x58, "F12" },
			{ 0x9C, "Num Enter" }, { 0x9D, "Right Ctrl" }, { 0xB5, "Num /" }, { 0xB7, "Print Screen" },
			{ 0xB8, "Right Alt" }, { 0xC5, "Pause" }, { 0xC7, "Home" }, { 0xC8, "Up" },
			{ 0xC9, "Page Up" }, { 0xCB, "Left" }, { 0xCD, "Right" }, { 0xCF, "End" },
			{ 0xD0, "Down" }, { 0xD1, "Page Down" }, { 0xD2, "Insert" }, { 0xD3, "Delete" },
			{ 0xDB, "Left Win" }, { 0xDC, "Right Win" }, { 0xDD, "Menu" },
			{ 256, "Left Mouse" }, { 257, "Right Mouse" }, { 258, "Middle Mouse" }, { 259, "Mouse 4" },
			{ 260, "Mouse 5" }, { 261, "Mouse 6" }, { 262, "Mouse 7" }, { 263, "Mouse 8" },
			{ 264, "Wheel Up" }, { 265, "Wheel Down" }
		};

		constexpr NameEntry kAliases[] = {
			{ 0x01, "Escape" }, { 0x1C, "Return" }, { 0x2A, "LShift" }, { 0x2A, "Shift" },
			{ 0x36, "RShift" }, { 0x1D, "LCtrl" }, { 0x1D, "LControl" }, { 0x1D, "Ctrl" },
			{ 0x1D, "Control" }, { 0x9D, "RCtrl" }, { 0x9D, "RControl" }, { 0x38, "LAlt" },
			{ 0x38, "Alt" }, { 0x38, "LMenu" }, { 0xB8, "RAlt" }, { 0xB8, "RMenu" },
			{ 0x2A, "LeftShift" }, { 0x36, "RightShift" }, { 0x1D, "LeftControl" },
			{ 0x9D, "RightControl" }, { 0x38, "LeftMenu" }, { 0xB8, "RightMenu" },
			{ 0x39, "Spacebar" }, { 0x29, "Tilde" }, { 0x29, "Grave" }, { 0x29, "Console" },
			{ 0xD3, "Del" }, { 0xD2, "Ins" }, { 0xC9, "PgUp" }, { 0xD1, "PgDn" }, { 0xC9, "Prior" },
			{ 0xD1, "Next" }, { 0x3A, "Capital" }, { 0x0E, "Back" }, { 0x0C, "Minus" },
			{ 0x0D, "Equals" }, { 0x1A, "LBracket" }, { 0x1B, "RBracket" }, { 0x27, "Semicolon" },
			{ 0x28, "Apostrophe" }, { 0x2B, "Backslash" }, { 0x33, "Comma" }, { 0x34, "Period" },
			{ 0x35, "Slash" }, { 0x37, "Multiply" }, { 0x4A, "Subtract" }, { 0x4E, "Add" },
			{ 0x53, "Decimal" }, { 0xB5, "Divide" }, { 0x9C, "NumpadEnter" }, { 0xB7, "SysRq" },
			{ 0xB7, "Snapshot" }, { 0x46, "Scroll" }, { 0xDB, "LWin" }, { 0xDC, "RWin" },
			{ 0xDD, "Apps" }, { 0xC8, "UpArrow" }, { 0xD0, "DownArrow" }, { 0xCB, "LeftArrow" },
			{ 0xCD, "RightArrow" }, { 256, "LMB" }, { 257, "RMB" }, { 258, "MMB" }
		};

		std::unordered_map<std::string, std::uint32_t> g_byName;
		std::array<std::uint32_t, 256>                  g_vkToDx{};

		std::string Normalize(std::string_view a_text)
		{
			std::string s;
			s.reserve(a_text.size());
			for (const char c : a_text) {
				s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			}
			for (const auto prefix : { "dik_"sv, "key_"sv, "vk_"sv, "kc_"sv }) {
				if (s.size() > prefix.size() && s.starts_with(prefix)) {
					s.erase(0, prefix.size());
					break;
				}
			}
			if (s.size() > 1) {
				std::erase_if(s, [](char c) { return c == ' ' || c == '_'; });
			}
			if (s.size() > 6 && s.starts_with("numpad")) {
				s.replace(0, 6, "num");
			}
			return s;
		}

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n\"'");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n\"'");
			return std::string(a_text.substr(first, last - first + 1));
		}
	}

	void Init()
	{
		for (const auto& e : kNames) {
			g_byName.emplace(Normalize(e.name), e.code);
		}
		for (const auto& e : kAliases) {
			g_byName.emplace(Normalize(e.name), e.code);
		}

		for (std::uint32_t vk = 1; vk < 256; ++vk) {
			const UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
			if (sc == 0) {
				continue;
			}
			const UINT prefix = (sc >> 8) & 0xFF;
			g_vkToDx[vk] = (sc & 0x7F) | ((prefix == 0xE0 || prefix == 0xE1) ? 0x80 : 0);
		}

		// Keys MapVirtualKey gets wrong or leaves out
		const std::pair<std::uint32_t, std::uint32_t> fixed[] = {
			{ VK_LBUTTON, 256 }, { VK_RBUTTON, 257 }, { VK_MBUTTON, 258 }, { VK_XBUTTON1, 259 },
			{ VK_XBUTTON2, 260 }, { VK_SHIFT, 0x2A }, { VK_CONTROL, 0x1D }, { VK_MENU, 0x38 },
			{ VK_LSHIFT, 0x2A }, { VK_RSHIFT, 0x36 }, { VK_LCONTROL, 0x1D }, { VK_RCONTROL, 0x9D },
			{ VK_LMENU, 0x38 }, { VK_RMENU, 0xB8 }, { VK_PAUSE, 0xC5 }, { VK_NUMLOCK, 0x45 },
			{ VK_SNAPSHOT, 0xB7 }, { VK_LEFT, 0xCB }, { VK_RIGHT, 0xCD }, { VK_UP, 0xC8 },
			{ VK_DOWN, 0xD0 }, { VK_HOME, 0xC7 }, { VK_END, 0xCF }, { VK_PRIOR, 0xC9 },
			{ VK_NEXT, 0xD1 }, { VK_INSERT, 0xD2 }, { VK_DELETE, 0xD3 }, { VK_DIVIDE, 0xB5 },
			{ VK_LWIN, 0xDB }, { VK_RWIN, 0xDC }, { VK_APPS, 0xDD }
		};
		for (const auto& [vk, dx] : fixed) {
			g_vkToDx[vk] = dx;
		}
	}

	std::string Name(std::uint32_t a_code)
	{
		for (const auto& e : kNames) {
			if (e.code == a_code) {
				return e.name;
			}
		}
		return std::format("Key 0x{:02X}", a_code);
	}

	std::string ComboName(std::uint32_t a_code, const std::vector<std::uint8_t>& a_modGroups)
	{
		constexpr std::pair<std::uint8_t, const char*> names[] = {
			{ kAnyCtrl, "Ctrl" }, { kLCtrl, "Left Ctrl" }, { kRCtrl, "Right Ctrl" },
			{ kAnyShift, "Shift" }, { kLShift, "Left Shift" }, { kRShift, "Right Shift" },
			{ kAnyAlt, "Alt" }, { kLAlt, "Left Alt" }, { kRAlt, "Right Alt" }
		};
		std::string s;
		for (const auto& [mask, name] : names) {
			if (std::find(a_modGroups.begin(), a_modGroups.end(), mask) != a_modGroups.end()) {
				s += name;
				s += '+';
			}
		}
		return s + Name(a_code);
	}

	std::uint8_t ModifierBit(std::uint32_t a_code)
	{
		switch (a_code) {
		case 0x2A:
			return kLShift;
		case 0x36:
			return kRShift;
		case 0x1D:
			return kLCtrl;
		case 0x9D:
			return kRCtrl;
		case 0x38:
			return kLAlt;
		case 0xB8:
			return kRAlt;
		default:
			return 0;
		}
	}

	std::uint8_t EitherSideMask(std::string_view a_token)
	{
		const auto n = Normalize(Trim(a_token));
		if (n == "alt") {
			return kAnyAlt;
		}
		if (n == "ctrl" || n == "control") {
			return kAnyCtrl;
		}
		if (n == "shift") {
			return kAnyShift;
		}
		return 0;
	}

	bool IsValidCode(std::uint32_t a_code)
	{
		if (a_code >= kMouseBase) {
			return a_code <= kMouseLast;
		}
		for (const auto& e : kNames) {
			if (e.code == a_code) {
				return true;
			}
		}
		return false;
	}

	std::optional<std::uint32_t> ParseToken(std::string_view a_token, bool a_vk)
	{
		const auto text = Trim(a_token);
		if (text.empty()) {
			return std::nullopt;
		}

		std::uint32_t value = 0;
		bool          numeric = false;
		if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
			const auto res = std::from_chars(text.data() + 2, text.data() + text.size(), value, 16);
			numeric = res.ec == std::errc{} && res.ptr == text.data() + text.size();
			if (!numeric) {
				return std::nullopt;
			}
		} else if (std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
			const auto res = std::from_chars(text.data(), text.data() + text.size(), value, 10);
			numeric = res.ec == std::errc{};
			if (!numeric) {
				return std::nullopt;
			}
		}

		if (numeric) {
			if (a_vk) {
				const auto dx = VkToDx(value);
				return dx ? std::optional<std::uint32_t>(dx) : std::nullopt;
			}
			return value;
		}

		if (const auto it = g_byName.find(Normalize(text)); it != g_byName.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	std::uint32_t VkToDx(std::uint32_t a_vk)
	{
		return a_vk < g_vkToDx.size() ? g_vkToDx[a_vk] : 0;
	}

	int ToVirtualKey(std::uint32_t a_code)
	{
		switch (a_code) {
		case 256:
			return VK_LBUTTON;
		case 257:
			return VK_RBUTTON;
		case 258:
			return VK_MBUTTON;
		case 259:
			return VK_XBUTTON1;
		case 260:
			return VK_XBUTTON2;
		case 0x2A:
			return VK_LSHIFT;
		case 0x36:
			return VK_RSHIFT;
		case 0x1D:
			return VK_LCONTROL;
		case 0x9D:
			return VK_RCONTROL;
		case 0x38:
			return VK_LMENU;
		case 0xB8:
			return VK_RMENU;
		case 0x45:
			return VK_NUMLOCK;
		case 0xC5:
			return VK_PAUSE;
		default:
			break;
		}
		if (a_code >= 256) {
			return 0;  // mouse wheel has no held state
		}
		const UINT scan = (a_code & 0x7F) | ((a_code & 0x80) ? 0xE000 : 0);
		return static_cast<int>(MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX));
	}

	bool IsDownNow(std::uint32_t a_code)
	{
		const int vk = ToVirtualKey(a_code);
		if (vk == 0) {
			return false;
		}
		return (static_cast<std::uint16_t>(GetAsyncKeyState(vk)) & 0x8000) != 0;
	}
}
