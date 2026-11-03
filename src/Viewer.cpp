#include "Viewer.h"

#include "Fix.h"
#include "Keys.h"
#include "Router.h"
#include "Settings.h"

#include <Windows.h>

#include "SKSEMenuFramework.h"

namespace Viewer
{
	namespace
	{
		using namespace ImGuiMCP;

		struct KeyRect
		{
			std::uint32_t code;
			float         x, y, w, h;  // in key units
			const char*   label;
		};

		// 104-key US (ANSI) layout plus a mouse panel; codes are DirectX scan codes, mouse 256+
		const std::vector<KeyRect>& LayoutUS()
		{
			static std::vector<KeyRect> keys = [] {
				std::vector<KeyRect> k;
				auto row = [&](float y, float x, std::initializer_list<std::pair<std::uint32_t, const char*>> items, float w = 1.0F) {
					for (const auto& [code, label] : items) {
						k.push_back({ code, x, y, w, 1.0F, label });
						x += w;
					}
				};
				// function row
				k.push_back({ 0x01, 0, 0, 1, 1, "Esc" });
				row(0, 2, { { 0x3B, "F1" }, { 0x3C, "F2" }, { 0x3D, "F3" }, { 0x3E, "F4" } });
				row(0, 6.5F, { { 0x3F, "F5" }, { 0x40, "F6" }, { 0x41, "F7" }, { 0x42, "F8" } });
				row(0, 11, { { 0x43, "F9" }, { 0x44, "F10" }, { 0x57, "F11" }, { 0x58, "F12" } });
				row(0, 15.25F, { { 0xB7, "PrtSc" }, { 0x46, "ScrLk" }, { 0xC5, "Pause" } });
				// number row
				row(1.5F, 0, { { 0x29, "`" }, { 0x02, "1" }, { 0x03, "2" }, { 0x04, "3" }, { 0x05, "4" }, { 0x06, "5" }, { 0x07, "6" },
								  { 0x08, "7" }, { 0x09, "8" }, { 0x0A, "9" }, { 0x0B, "0" }, { 0x0C, "-" }, { 0x0D, "=" } });
				k.push_back({ 0x0E, 13, 1.5F, 2, 1, "Backspace" });
				row(1.5F, 15.25F, { { 0xD2, "Ins" }, { 0xC7, "Home" }, { 0xC9, "PgUp" } });
				row(1.5F, 18.5F, { { 0x45, "NumLk" }, { 0xB5, "/" }, { 0x37, "*" }, { 0x4A, "-" } });
				// QWERTY row
				k.push_back({ 0x0F, 0, 2.5F, 1.5F, 1, "Tab" });
				row(2.5F, 1.5F, { { 0x10, "Q" }, { 0x11, "W" }, { 0x12, "E" }, { 0x13, "R" }, { 0x14, "T" }, { 0x15, "Y" }, { 0x16, "U" },
									{ 0x17, "I" }, { 0x18, "O" }, { 0x19, "P" }, { 0x1A, "[" }, { 0x1B, "]" } });
				k.push_back({ 0x2B, 13.5F, 2.5F, 1.5F, 1, "\\" });
				row(2.5F, 15.25F, { { 0xD3, "Del" }, { 0xCF, "End" }, { 0xD1, "PgDn" } });
				row(2.5F, 18.5F, { { 0x47, "7" }, { 0x48, "8" }, { 0x49, "9" } });
				k.push_back({ 0x4E, 21.5F, 2.5F, 1, 2, "+" });
				// home row
				k.push_back({ 0x3A, 0, 3.5F, 1.75F, 1, "Caps" });
				row(3.5F, 1.75F, { { 0x1E, "A" }, { 0x1F, "S" }, { 0x20, "D" }, { 0x21, "F" }, { 0x22, "G" }, { 0x23, "H" }, { 0x24, "J" },
									 { 0x25, "K" }, { 0x26, "L" }, { 0x27, ";" }, { 0x28, "'" } });
				k.push_back({ 0x1C, 12.75F, 3.5F, 2.25F, 1, "Enter" });
				row(3.5F, 18.5F, { { 0x4B, "4" }, { 0x4C, "5" }, { 0x4D, "6" } });
				// shift row
				k.push_back({ 0x2A, 0, 4.5F, 2.25F, 1, "Shift" });
				row(4.5F, 2.25F, { { 0x2C, "Z" }, { 0x2D, "X" }, { 0x2E, "C" }, { 0x2F, "V" }, { 0x30, "B" }, { 0x31, "N" }, { 0x32, "M" },
									 { 0x33, "," }, { 0x34, "." }, { 0x35, "/" } });
				k.push_back({ 0x36, 12.25F, 4.5F, 2.75F, 1, "Shift" });
				k.push_back({ 0xC8, 16.25F, 4.5F, 1, 1, "Up" });
				row(4.5F, 18.5F, { { 0x4F, "1" }, { 0x50, "2" }, { 0x51, "3" } });
				k.push_back({ 0x9C, 21.5F, 4.5F, 1, 2, "Enter" });
				// bottom row
				row(5.5F, 0, { { 0x1D, "Ctrl" }, { 0xDB, "Win" }, { 0x38, "Alt" } }, 1.25F);
				k.push_back({ 0x39, 3.75F, 5.5F, 6.25F, 1, "Space" });
				row(5.5F, 10, { { 0xB8, "Alt" }, { 0xDC, "Win" }, { 0xDD, "Menu" }, { 0x9D, "Ctrl" } }, 1.25F);
				row(5.5F, 15.25F, { { 0xCB, "Left" }, { 0xD0, "Down" }, { 0xCD, "Right" } });
				k.push_back({ 0x52, 18.5F, 5.5F, 2, 1, "0" });
				k.push_back({ 0x53, 20.5F, 5.5F, 1, 1, "." });
				// mouse
				k.push_back({ 256, 23.5F, 1.5F, 1.5F, 1.5F, "Left btn" });
				k.push_back({ 258, 25.0F, 1.5F, 1.0F, 1.5F, "Mid" });
				k.push_back({ 257, 26.0F, 1.5F, 1.5F, 1.5F, "Right btn" });
				k.push_back({ 264, 23.5F, 3.25F, 2.0F, 1, "Wheel up" });
				k.push_back({ 265, 25.5F, 3.25F, 2.0F, 1, "Wheel down" });
				row(4.5F, 23.5F, { { 259, "M4" }, { 260, "M5" }, { 261, "M6" }, { 262, "M7" } });
				k.push_back({ 263, 23.5F, 5.5F, 1, 1, "M8" });
				return k;
			}();
			return keys;
		}

		// UK ISO 105 keys: the US layout with a tall Enter, # next to it, a short Left Shift with the
		// extra ISO key (scan code 0x56) beside it, and UK labels
		const std::vector<KeyRect>& LayoutUK()
		{
			static std::vector<KeyRect> keys = [] {
				auto k = LayoutUS();
				std::erase_if(k, [](const KeyRect& r) { return r.code == 0x2B || r.code == 0x1C || r.code == 0x2A; });
				k.push_back({ 0x1C, 13.75F, 2.5F, 1.25F, 2.0F, "Enter" });
				k.push_back({ 0x2B, 12.75F, 3.5F, 1.0F, 1.0F, "#" });
				k.push_back({ 0x2A, 0.0F, 4.5F, 1.25F, 1.0F, "Shift" });
				k.push_back({ 0x56, 1.25F, 4.5F, 1.0F, 1.0F, "\\" });
				for (auto& r : k) {
					if (r.code == 0xB8) {
						r.label = "AltGr";
					}
				}
				return k;
			}();
			return keys;
		}

		bool UK() { return Settings::mapLayout == Settings::kLayoutUK; }

		const std::vector<KeyRect>& Layout() { return UK() ? LayoutUK() : LayoutUS(); }

		// Key name as printed on the player's keyboard
		std::string DisplayName(std::uint32_t a_code)
		{
			if (UK()) {
				switch (a_code) {
				case 0x2B:
					return "#";
				case 0x56:
					return "\\ (ISO key next to Left Shift)";
				case 0xB8:
					return "AltGr";
				case 0x28:
					return "' (@ with Shift)";
				case 0x29:
					return "`";
				default:
					break;
				}
			}
			return Keys::Name(a_code);
		}

		MENU_WINDOW                g_window = nullptr;
		std::atomic<std::uint32_t> g_triggerCode{ 0 };  // 0 = no usable key
		std::atomic<bool>          g_threadStarted{ false };

		std::mutex                                          g_mapLock;
		std::unordered_map<std::uint32_t, Router::KeyEntry> g_map;
		float                                               g_zoom = 0.0F;  // 0 = fit on next frame
		ImVec2                                              g_pan{ 0, 0 };
		std::uint32_t                                       g_selected = 0;  // 0 = none
		ImVec2                                              g_pressPos{ 0, 0 };

		enum class Color
		{
			kNone,
			kOne,
			kSafe,
			kConflict
		};

		Color ColorOf(const Router::KeyEntry& a_entry)
		{
			const auto total = a_entry.plain.size();
			const auto active = std::count_if(a_entry.plain.begin(), a_entry.plain.end(), [](const Router::KeyBind& b) { return !b.passive; });
			if (total == 0) {
				return Color::kNone;
			}
			if (total == 1) {
				return Color::kOne;
			}
			return active <= 1 ? Color::kSafe : Color::kConflict;
		}

		struct Rgb
		{
			int r, g, b;
		};

		Rgb StatusRgb(Color a_color)
		{
			switch (a_color) {
			case Color::kNone:
				return { 70, 190, 100 };
			case Color::kOne:
				return { 240, 140, 40 };
			case Color::kSafe:
				return { 235, 210, 60 };
			default:
				return { 230, 55, 55 };
			}
		}

		ImU32 Col(Rgb a_c, int a_alpha = 255) { return IM_COL32(a_c.r, a_c.g, a_c.b, a_alpha); }

		// Mixes the dark keycap color with a_c; a_t 0..1
		ImU32 Tint(Rgb a_c, float a_t, int a_add = 0)
		{
			auto mix = [&](int base, int c) { return std::clamp(static_cast<int>(base + (c - base) * a_t) + a_add, 0, 255); };
			return IM_COL32(mix(34, a_c.r), mix(34, a_c.g), mix(37, a_c.b), 255);
		}

		const Rgb   kGold{ 200, 165, 100 };
		const ImU32 kText = IM_COL32(236, 228, 210, 255);
		const ImU32 kTextDim = IM_COL32(170, 160, 140, 255);
		const ImU32 kRemembered = IM_COL32(90, 170, 255, 255);

		float TextWidth(const std::string& a_text, float a_size)
		{
			return CalcTextSize(a_text.c_str()).x * a_size / GetFontSize();
		}

		void PaintText(ImDrawList* a_dl, float a_size, ImVec2 a_pos, ImU32 a_col, const std::string& a_text)
		{
			ImDrawListManager::AddText(a_dl, GetFont(), a_size, a_pos, a_col, a_text.c_str());
		}

		void DrawCentered(ImDrawList* a_dl, float a_size, float a_centerX, float a_y, ImU32 a_col, const std::string& a_text)
		{
			PaintText(a_dl, a_size, ImVec2{ a_centerX - TextWidth(a_text, a_size) * 0.5F, a_y }, a_col, a_text);
		}

		// Cuts a_text so it fits a_width pixels at a_size, ending with ".." when shortened
		std::string FitText(const std::string& a_text, float a_width, float a_size)
		{
			if (TextWidth(a_text, a_size) <= a_width) {
				return a_text;
			}
			std::string cut = a_text;
			while (!cut.empty() && TextWidth(cut + "..", a_size) > a_width) {
				cut.pop_back();
			}
			return cut.empty() ? std::string() : cut + "..";
		}

		// Thin gold line with a small diamond at each end
		void GoldRule(ImDrawList* a_dl, float a_x1, float a_x2, float a_y)
		{
			ImDrawListManager::AddLine(a_dl, ImVec2{ a_x1, a_y }, ImVec2{ a_x2, a_y }, Col(kGold, 150), 1.0F);
			for (const float x : { a_x1, a_x2 }) {
				ImDrawListManager::AddTriangleFilled(a_dl, ImVec2{ x - 4, a_y }, ImVec2{ x, a_y - 4 }, ImVec2{ x + 4, a_y }, Col(kGold, 200));
				ImDrawListManager::AddTriangleFilled(a_dl, ImVec2{ x - 4, a_y }, ImVec2{ x + 4, a_y }, ImVec2{ x, a_y + 4 }, Col(kGold, 200));
			}
		}

		// Gold frame with L-shaped corner marks
		void GoldFrame(ImDrawList* a_dl, ImVec2 a_a, ImVec2 a_b)
		{
			ImDrawListManager::AddRect(a_dl, a_a, a_b, Col(kGold, 110), 0.0F, 0, 1.0F);
			const float l = 18.0F;
			const auto  c = Col(kGold, 230);
			auto        corner = [&](ImVec2 p, float dx, float dy) {
                ImDrawListManager::AddLine(a_dl, ImVec2{ p.x, p.y }, ImVec2{ p.x + dx * l, p.y }, c, 2.5F);
                ImDrawListManager::AddLine(a_dl, ImVec2{ p.x, p.y }, ImVec2{ p.x, p.y + dy * l }, c, 2.5F);
			};
			corner(ImVec2{ a_a.x - 3, a_a.y - 3 }, 1, 1);
			corner(ImVec2{ a_b.x + 3, a_a.y - 3 }, -1, 1);
			corner(ImVec2{ a_a.x - 3, a_b.y + 3 }, 1, -1);
			corner(ImVec2{ a_b.x + 3, a_b.y + 3 }, -1, -1);
		}

		std::vector<std::string> Lines(const Router::KeyEntry& a_entry)
		{
			std::vector<std::string> lines;
			for (const auto& bind : a_entry.plain) {
				lines.push_back(bind.passive ? bind.name + " (passive)" : bind.name);
			}
			for (const auto& bind : a_entry.combos) {
				lines.push_back(bind.combo + ": " + bind.name);
			}
			return lines;
		}

		void DrawKey(ImDrawList* a_dl, const KeyRect& a_key, const Router::KeyEntry& a_entry, ImVec2 a_a, ImVec2 a_b, bool a_hover, bool a_selected)
		{
			const auto  status = ColorOf(a_entry);
			const auto  rgb = StatusRgb(status);
			const bool  quiet = status == Color::kNone;  // most keys: keep them calm
			const float h = a_b.y - a_a.y;
			const float round = std::min(h * 0.14F, 10.0F);

			// shadow
			ImDrawListManager::AddRectFilled(a_dl, ImVec2{ a_a.x + 1, a_a.y + h * 0.06F + 1 }, ImVec2{ a_b.x + 1, a_b.y + h * 0.06F + 1 },
				IM_COL32(0, 0, 0, 150), round, ImDrawFlags_RoundCornersAll);
			// glow
			if (!quiet || a_selected) {
				const auto glow = a_selected ? kRemembered : Col(rgb, 70);
				ImDrawListManager::AddRect(a_dl, ImVec2{ a_a.x - 2, a_a.y - 2 }, ImVec2{ a_b.x + 2, a_b.y + 2 }, glow, round + 2, ImDrawFlags_RoundCornersAll, 3.0F);
			}
			// cap, a little lighter at the top
			const int lift = a_hover ? 18 : 0;
			ImDrawListManager::AddRectFilled(a_dl, a_a, a_b, Tint(rgb, quiet ? 0.10F : 0.30F, lift), round, ImDrawFlags_RoundCornersAll);
			ImDrawListManager::AddRectFilled(a_dl, ImVec2{ a_a.x + 1, a_a.y + 1 }, ImVec2{ a_b.x - 1, a_a.y + h * 0.45F },
				IM_COL32(255, 255, 255, 12), round, ImDrawFlags_RoundCornersTop);
			// rim
			ImDrawListManager::AddRect(a_dl, a_a, a_b, quiet ? Col(rgb, 120) : Col(rgb, 235), round, ImDrawFlags_RoundCornersAll, quiet ? 1.0F : 1.8F);
			// remembered choice: blue corner
			if (!a_entry.remembered.empty()) {
				const float s = std::max(6.0F, h * 0.18F);
				ImDrawListManager::AddTriangleFilled(a_dl, ImVec2{ a_b.x - s - 2, a_a.y + 2 }, ImVec2{ a_b.x - 2, a_a.y + 2 }, ImVec2{ a_b.x - 2, a_a.y + 2 + s }, kRemembered);
			}

			// key name, centered; mod names under it when there is room
			ImDrawListManager::PushClipRect(a_dl, a_a, a_b, true);
			const float inner = a_b.x - a_a.x - 6.0F;
			const auto  lines = Lines(a_entry);
			const float labelSize = std::clamp(h * 0.30F, 9.0F, 30.0F);
			const float nameSize = std::clamp(h * 0.17F, 8.0F, 22.0F);
			const bool  showNames = !lines.empty() && nameSize >= 11.0F;
			const float cx = (a_a.x + a_b.x) * 0.5F;
			if (!showNames) {
				DrawCentered(a_dl, labelSize, cx, a_a.y + (h - labelSize) * 0.5F, kText, a_key.label);
			} else {
				float y = a_a.y + h * 0.10F;
				DrawCentered(a_dl, labelSize, cx, y, kText, a_key.label);
				y += labelSize * 1.05F;
				for (const auto& text : lines) {
					if (y + nameSize > a_b.y - 2) {
						break;
					}
					const auto fitted = FitText(text, inner, nameSize);
					if (!fitted.empty()) {
						DrawCentered(a_dl, nameSize, cx, y, kTextDim, fitted);
					}
					y += nameSize * 1.05F;
				}
			}
			ImDrawListManager::PopClipRect(a_dl);
		}

		void Swatch(ImDrawList* a_dl, float& a_x, float a_y, float a_size, Color a_color, const char* a_text)
		{
			const auto rgb = StatusRgb(a_color);
			ImDrawListManager::AddRectFilled(a_dl, ImVec2{ a_x, a_y }, ImVec2{ a_x + a_size, a_y + a_size }, Tint(rgb, 0.30F), 3.0F, ImDrawFlags_RoundCornersAll);
			ImDrawListManager::AddRect(a_dl, ImVec2{ a_x, a_y }, ImVec2{ a_x + a_size, a_y + a_size }, Col(rgb), 3.0F, ImDrawFlags_RoundCornersAll, 1.5F);
			a_x += a_size + 6;
			PaintText(a_dl, a_size, ImVec2{ a_x, a_y }, kTextDim, a_text);
			a_x += TextWidth(a_text, a_size) + 22;
		}

		// Hotkeys moved with the Move button this session. The mods still use the old key until
		// Skyrim restarts; the map shows them on the new key, marked "after restart".
		struct Moved
		{
			std::string               path;  // the setting's origin, to find it again after a rebuild
			std::size_t               line{ 0 };
			std::string               setting;
			std::uint32_t             from{ 0 };
			std::uint32_t             to{ 0 };
			std::vector<std::uint8_t> modGroups;
			Router::KeyBind           bind;  // as it will be on the new key
		};
		std::vector<Moved> g_moved;

		bool SameOrigin(const Router::HowTo& a_how, const std::string& a_path, std::size_t a_line, const std::string& a_setting)
		{
			return a_how.origin && a_how.origin->path == a_path && a_how.origin->line == a_line && a_how.origin->setting == a_setting;
		}

		// g_mapLock held. Takes the setting off a_move.from and puts its bind on a_move.to.
		void ApplyMove(const Moved& a_move)
		{
			if (auto it = g_map.find(a_move.from); it != g_map.end()) {
				for (auto* list : { &it->second.plain, &it->second.combos }) {
					for (auto b = list->begin(); b != list->end();) {
						std::erase_if(b->howTo, [&](const Router::HowTo& h) { return SameOrigin(h, a_move.path, a_move.line, a_move.setting); });
						const bool onlyThat = b->howTo.empty() && b->name == a_move.bind.name;
						b = onlyThat ? list->erase(b) : b + 1;
					}
				}
			}
			auto& target = g_map[a_move.to];
			auto& list = a_move.modGroups.empty() ? target.plain : target.combos;
			for (const auto& b : list) {
				for (const auto& h : b.howTo) {
					if (SameOrigin(h, a_move.path, a_move.line, a_move.setting)) {
						return;  // already there
					}
				}
			}
			list.push_back(a_move.bind);
		}

		// ---- Move button state ----

		enum class MoveStep
		{
			kNone,
			kWaitKey,
			kConfirm
		};

		struct MoveState
		{
			MoveStep              step{ MoveStep::kNone };
			std::uint32_t         from{ 0 };
			Router::KeyBind       bind;  // the bind being moved, with only the setting being moved
			Router::HowTo         how;
			std::uint32_t         to{ 0 };
			std::array<bool, 256> downAtStart{};
		};
		MoveState   g_move;
		std::string g_moveStatus;
		bool        g_moveOk = true;

		bool DownVk(int a_vk) { return (static_cast<std::uint16_t>(GetAsyncKeyState(a_vk)) & 0x8000) != 0; }

		// The first keyboard key pressed since the capture began, 0 if none yet
		std::uint32_t CapturedKey()
		{
			for (int vk = 0x08; vk < 0xFF; ++vk) {
				if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_ESCAPE) {
					continue;
				}
				const bool down = DownVk(vk);
				if (!down) {
					g_move.downAtStart[vk] = false;
					continue;
				}
				if (g_move.downAtStart[vk]) {
					continue;
				}
				if (const auto dx = Keys::VkToDx(static_cast<std::uint32_t>(vk)); dx != 0 && dx < Keys::kMouseBase) {
					return dx;
				}
			}
			return 0;
		}

		// g_mapLock held
		void DoMove()
		{
			const auto result = Fix::MoveKey(*g_move.how.origin, g_move.to);
			g_moveOk = result.ok;
			g_moveStatus = result.message;
			if (result.ok) {
				Moved m;
				m.path = g_move.how.origin->path;
				m.line = g_move.how.origin->line;
				m.setting = g_move.how.origin->setting;
				m.from = g_move.from;
				m.to = g_move.to;
				m.modGroups = g_move.how.modGroups;
				m.bind = g_move.bind;
				m.bind.moved = true;
				m.bind.combo = m.modGroups.empty() ? std::string() : Keys::ComboName(g_move.to, m.modGroups);
				m.bind.howTo = { g_move.how };
				m.bind.howTo[0].code = g_move.to;
				m.bind.howTo[0].origin->token = result.newToken;
				// a later move of the same setting replaces this one
				std::erase_if(g_moved, [&](const Moved& x) { return x.path == m.path && x.line == m.line && x.setting == m.setting; });
				g_moved.push_back(m);
				ApplyMove(m);
				g_selected = g_move.to;
			}
			g_move = MoveState{};
		}

		std::string UsersOf(std::uint32_t a_code)
		{
			std::string out;
			if (const auto it = g_map.find(a_code); it != g_map.end()) {
				for (const auto& b : it->second.plain) {
					out += (out.empty() ? "" : ", ") + b.name;
				}
			}
			return out;
		}

		// g_mapLock held. The panel under the keyboard: the selected key, its mods, how to change them.
		void DetailPanel(ImVec2 a_size)
		{
			const ImVec4 gold{ 0.86F, 0.74F, 0.50F, 1.0F };
			const ImVec4 good{ 0.45F, 0.85F, 0.50F, 1.0F };
			const ImVec4 bad{ 1.0F, 0.42F, 0.36F, 1.0F };
			const ImVec4 warn{ 1.0F, 0.80F, 0.30F, 1.0F };

			if (!BeginChild("##detail", a_size, ImGuiChildFlags_Border)) {
				EndChild();
				return;
			}

			if (g_move.step == MoveStep::kWaitKey) {
				TextColored(warn, "Press the new key for %s (%s)", g_move.bind.name.c_str(), g_move.how.text.c_str());
				SameLine();
				if (SmallButton("Cancel##move")) {
					g_move = MoveState{};
				} else if (const auto key = CapturedKey(); key != 0) {
					g_move.to = key;
					if (key == g_move.from) {
						g_move = MoveState{};
					} else if (const auto users = UsersOf(key); !users.empty() && g_move.how.modGroups.empty()) {
						g_move.step = MoveStep::kConfirm;
					} else {
						DoMove();
					}
				}
			} else if (g_move.step == MoveStep::kConfirm) {
				const auto name = DisplayName(g_move.to);
				TextColored(warn, "%s is also used by: %s", name.c_str(), UsersOf(g_move.to).c_str());
				if (SmallButton(std::format("Use {} anyway##move", name).c_str())) {
					DoMove();
				}
				SameLine();
				if (SmallButton("Pick another key##move")) {
					g_move.step = MoveStep::kWaitKey;
					for (int vk = 0; vk < 256; ++vk) {
						g_move.downAtStart[vk] = DownVk(vk);
					}
				}
				SameLine();
				if (SmallButton("Cancel##move")) {
					g_move = MoveState{};
				}
			}
			if (!g_moveStatus.empty()) {
				TextColored(g_moveOk ? good : bad, "%s", g_moveStatus.c_str());
			}

			if (g_selected == 0) {
				TextDisabled("Click a key to see every mod that uses it and how to change it.");
				EndChild();
				return;
			}
			const auto it = g_map.find(g_selected);
			TextColored(gold, "SELECTED KEY:");
			SameLine();
			Text("%s", DisplayName(g_selected).c_str());
			if (it == g_map.end() || (it->second.plain.empty() && it->second.combos.empty())) {
				TextDisabled("No mod uses this key.");
				EndChild();
				return;
			}
			auto& entry = it->second;
			if (!entry.remembered.empty()) {
				SameLine();
				TextDisabled("   remembered choice: %s", entry.remembered.c_str());
			}
			int id = 0;
			for (auto* list : { &entry.plain, &entry.combos }) {
				for (const auto& b : *list) {
					const std::string head = (b.combo.empty() ? "" : b.combo + ": ") + b.name + (b.passive ? "   (passive)" : "") +
					                         (b.moved ? "   (moved here, after restart)" : "");
					Text("%s", head.c_str());
					Indent();
					if (b.howTo.empty()) {
						TextDisabled("No setting found for this key");
					}
					for (const auto& h : b.howTo) {
						PushID(id++);
						TextDisabled("%s", h.text.c_str());
						if (h.origin && h.origin->Movable() && g_move.step == MoveStep::kNone) {
							SameLine();
							if (SmallButton("Move...")) {
								g_move = MoveState{};
								g_move.step = MoveStep::kWaitKey;
								g_move.from = g_selected;
								g_move.bind = b;
								g_move.bind.howTo = { h };
								g_move.how = h;
								g_moveStatus.clear();
								for (int vk = 0; vk < 256; ++vk) {
									g_move.downAtStart[vk] = DownVk(vk);
								}
							}
						}
						PopID();
					}
					Unindent();
				}
			}
			EndChild();
		}

		// One line per used key, for KeyRouter.log
		void LogMap()
		{
			for (const auto& [code, entry] : g_map) {
				if (entry.plain.empty() && entry.combos.empty()) {
					continue;
				}
				std::string line;
				for (const auto& b : entry.plain) {
					line += std::format("{}{} [{}]{}{}", line.empty() ? "" : " | ", b.name, b.source, b.passive ? " passive" : "", b.moved ? " moved here" : "");
				}
				for (const auto& b : entry.combos) {
					line += std::format("{}{}: {} [{}]{}", line.empty() ? "" : " | ", b.combo, b.name, b.source, b.moved ? " moved here" : "");
				}
				const auto status = ColorOf(entry);
				logger::info("[map] {} ({}): {}", DisplayName(code),
					status == Color::kConflict ? "CONFLICT" : status == Color::kSafe ? "shared" : status == Color::kOne ? "one mod" : "combos only", line);
			}
		}

		void __stdcall Render()
		{
			auto* viewport = GetMainViewport();
			SetNextWindowPos(ImGuiViewportManager::GetCenter(viewport), ImGuiCond_Appearing, ImVec2{ 0.5F, 0.5F });
			SetNextWindowSize(ImVec2{ viewport->Size.x * 0.88F, viewport->Size.y * 0.78F }, ImGuiCond_Appearing);
			if (!Begin("Key Router keyboard map##KeyRouter", nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
				End();
				return;
			}

			auto*        dl = GetWindowDrawList();
			const ImVec2 wpos = GetWindowPos();
			const ImVec2 wsize = GetWindowSize();
			const float  font = GetFontSize();

			// backdrop and frame
			ImDrawListManager::AddRectFilledMultiColor(dl, wpos, ImVec2{ wpos.x + wsize.x, wpos.y + wsize.y },
				IM_COL32(22, 21, 20, 235), IM_COL32(22, 21, 20, 235), IM_COL32(10, 10, 10, 245), IM_COL32(10, 10, 10, 245));
			GoldFrame(dl, ImVec2{ wpos.x + 10, wpos.y + 10 }, ImVec2{ wpos.x + wsize.x - 10, wpos.y + wsize.y - 10 });

			// title
			const float titleSize = std::clamp(wsize.y * 0.055F, font, font * 2.0F);
			const float subSize = std::clamp(titleSize * 0.42F, 10.0F, font);
			const float cx = wpos.x + wsize.x * 0.5F;
			const float titleY = wpos.y + 22;
			const std::string title = "K E Y B O A R D   M A P";
			const std::string sub = "104 KEYS + MOUSE   \xC2\xB7   WHEEL ZOOMS   \xC2\xB7   DRAG MOVES   \xC2\xB7   CLICK A KEY";
			DrawCentered(dl, titleSize, cx, titleY, IM_COL32(225, 205, 160, 255), title);
			const float tw = TextWidth(title, titleSize);
			GoldRule(dl, cx - tw * 0.5F - wsize.x * 0.16F, cx - tw * 0.5F - 18, titleY + titleSize * 0.55F);
			GoldRule(dl, cx + tw * 0.5F + 18, cx + tw * 0.5F + wsize.x * 0.16F, titleY + titleSize * 0.55F);
			DrawCentered(dl, subSize, cx, titleY + titleSize * 1.15F, kTextDim, sub);
			if (!Router::HooksInstalled()) {
				PaintText(dl, subSize, ImVec2{ wpos.x + 28, titleY }, IM_COL32(255, 200, 80, 255), "Key routing is off: script hotkeys are not shown");
			}

			// layout: header, canvas, detail strip
			const float headerH = titleY - wpos.y + titleSize * 1.15F + subSize + 18;
			const float stripH = font * 6.5F;
			const ImVec2 origin{ wpos.x + 28, wpos.y + headerH };
			const ImVec2 avail{ wsize.x - 56, wsize.y - headerH - stripH - 30 };
			if (avail.x < 50 || avail.y < 50) {
				End();
				return;
			}

			SetCursorScreenPos(origin);
			InvisibleButton("##canvas", avail, 0);
			const bool hovered = IsItemHovered();
			const bool active = IsItemActive();
			const bool released = IsItemDeactivated();

			constexpr float kWidthUnits = 27.75F;
			constexpr float kHeightUnits = 6.6F;
			const float     fit = std::min(avail.x / kWidthUnits, avail.y / kHeightUnits);
			if (g_zoom <= 0.0F) {
				g_zoom = fit;
				g_pan = ImVec2{ (avail.x - kWidthUnits * fit) * 0.5F, (avail.y - kHeightUnits * fit) * 0.5F };
			}
			const auto mouse = GetMousePos();
			if (hovered) {
				auto* io = GetIO();
				if (io && io->MouseWheel != 0.0F) {
					const float before = g_zoom;
					g_zoom = std::clamp(g_zoom * (io->MouseWheel > 0 ? 1.15F : 1.0F / 1.15F), fit * 0.5F, fit * 12.0F);
					// keep the point under the mouse in place
					const float mx = mouse.x - origin.x - g_pan.x;
					const float my = mouse.y - origin.y - g_pan.y;
					g_pan.x -= mx * (g_zoom / before - 1.0F);
					g_pan.y -= my * (g_zoom / before - 1.0F);
				}
				if (IsMouseClicked(0)) {
					g_pressPos = mouse;
				}
			}
			if (active && IsMouseDragging(0)) {
				const auto d = GetMouseDragDelta(0);
				g_pan.x += d.x;
				g_pan.y += d.y;
				ResetMouseDragDelta(0);
			}
			const bool click = released && std::abs(mouse.x - g_pressPos.x) < 5 && std::abs(mouse.y - g_pressPos.y) < 5;

			std::scoped_lock       lock(g_mapLock);
			const Router::KeyEntry empty;
			const KeyRect*         hoverKey = nullptr;

			ImDrawListManager::PushClipRect(dl, origin, ImVec2{ origin.x + avail.x, origin.y + avail.y }, true);
			for (const auto& k : Layout()) {
				const float  pad = std::max(1.5F, g_zoom * 0.05F);
				const ImVec2 a{ origin.x + g_pan.x + k.x * g_zoom + pad, origin.y + g_pan.y + k.y * g_zoom + pad };
				const ImVec2 b{ origin.x + g_pan.x + (k.x + k.w) * g_zoom - pad, origin.y + g_pan.y + (k.y + k.h) * g_zoom - pad };
				const auto   it = g_map.find(k.code);
				const bool   over = hovered && mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
				if (over) {
					hoverKey = &k;
				}
				DrawKey(dl, k, it == g_map.end() ? empty : it->second, a, b, over, g_selected == k.code);
			}
			// "MOUSE" caption over the mouse panel
			PaintText(dl, std::clamp(g_zoom * 0.28F, 9.0F, 26.0F), ImVec2{ origin.x + g_pan.x + 23.5F * g_zoom, origin.y + g_pan.y + 0.55F * g_zoom }, kTextDim, "MOUSE");
			ImDrawListManager::PopClipRect(dl);

			if (click) {
				g_selected = hoverKey ? hoverKey->code : 0;
			}
			if (hoverKey) {
				const auto  it = g_map.find(hoverKey->code);
				const auto& entry = it == g_map.end() ? empty : it->second;
				std::string tip = DisplayName(hoverKey->code);
				if (entry.plain.empty() && entry.combos.empty()) {
					tip += "\nNo mod uses this key.";
				}
				for (const auto& bind : entry.plain) {
					tip += std::format("\n{}  [{}]{}", bind.name, bind.source, bind.passive ? "  passive" : "");
				}
				for (const auto& bind : entry.combos) {
					tip += std::format("\n{} with {}  [{}]", bind.name, bind.combo, bind.source);
				}
				if (!entry.remembered.empty()) {
					tip += std::format("\nRemembered choice: {}", entry.remembered);
				}
				SetTooltip("%s", tip.c_str());
			}

			// detail strip: the selected key, with Move buttons
			const ImVec2 sa{ wpos.x + 28, wpos.y + wsize.y - stripH - 22 };
			const ImVec2 sb{ wpos.x + wsize.x - 28, wpos.y + wsize.y - 22 };
			SetCursorScreenPos(sa);
			DetailPanel(ImVec2{ sb.x - sa.x - 400, stripH });

			// legend and buttons, right side of the strip
			float lgx = sb.x - 250;
			float lgy = sa.y + 8;
			for (const auto& [color, text] : std::initializer_list<std::pair<Color, const char*>>{
					 { Color::kNone, "no mod" }, { Color::kOne, "one mod" }, { Color::kSafe, "shared, no conflict" }, { Color::kConflict, "conflict" } }) {
				float x = lgx;
				Swatch(dl, x, lgy, font * 0.7F, color, text);
				lgy += font * 0.8F;
			}
			SetCursorScreenPos(ImVec2{ sb.x - 250 - 120, sa.y + 8 });
			if (Button("Fit##map")) {
				g_zoom = 0.0F;
			}
			SetCursorScreenPos(ImVec2{ sb.x - 250 - 120, sa.y + 8 + font * 1.6F });
			if (Button("Close##map")) {
				g_window->IsOpen = false;
			}
			SetCursorScreenPos(ImVec2{ sb.x - 250 - 120, sa.y + 8 + font * 3.2F });
			if (Button(UK() ? "Layout: UK##map" : "Layout: US##map")) {
				Settings::mapLayout = UK() ? Settings::kLayoutUS : Settings::kLayoutUK;
				Settings::Save();
			}
			End();
		}

		bool SkyrimInFront()
		{
			const HWND window = GetForegroundWindow();
			if (!window) {
				return false;
			}
			DWORD pid = 0;
			GetWindowThreadProcessId(window, &pid);
			return pid == GetCurrentProcessId();
		}

		void TriggerLoop()
		{
			using clock = std::chrono::steady_clock;
			std::optional<clock::time_point> since;
			std::uint32_t                    heldCode = 0;
			bool                             fired = false;
			for (;;) {
				std::this_thread::sleep_for(100ms);
				const auto code = g_triggerCode.load();
				if (!Settings::mapEnabled || code == 0 || code != heldCode || !Keys::IsDownNow(code) || !SkyrimInFront()) {
					since.reset();
					heldCode = code;
					fired = false;
					continue;
				}
				if (!since) {
					since = clock::now();
				}
				if (!fired && clock::now() - *since >= std::chrono::milliseconds(Settings::mapHoldSeconds.load() * 1000)) {
					fired = true;  // once per hold
					Open();
				}
			}
		}
	}

	bool Open()
	{
		auto* task = SKSE::GetTaskInterface();
		if (!g_window || !task) {
			return false;
		}
		task->AddTask([] {
			auto        map = Router::BuildKeyMap();
			std::size_t used = 0;
			{
				std::scoped_lock lock(g_mapLock);
				g_map.clear();
				for (auto& [code, entry] : map) {
					used += entry.plain.empty() && entry.combos.empty() ? 0 : 1;
					g_map.emplace(code, std::move(entry));
				}
				for (const auto& m : g_moved) {
					ApplyMove(m);
				}
				g_zoom = 0.0F;
				g_selected = 0;
				g_move = MoveState{};
				g_moveStatus.clear();
				LogMap();
			}
			logger::info("Keyboard map opened: {} keys checked, {} used by at least one mod", map.size(), used);
			g_window->IsOpen = true;
		});
		return true;
	}

	bool Apply()
	{
		const auto code = Keys::ParseToken(Settings::mapKey, false);
		if (!code || Keys::ToVirtualKey(*code) == 0) {
			logger::error("Keyboard map: could not read key '{}'", Settings::mapKey);
			return false;
		}
		g_triggerCode = *code;
		logger::info("Keyboard map: {}, hold {} for {} seconds to open it", Settings::mapEnabled ? "on" : "off", Keys::Name(*code),
			Settings::mapHoldSeconds.load());
		return true;
	}

	void Start()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::warn("Keyboard map needs SKSE Menu Framework, which is not loaded; map not available");
			return;
		}
		g_window = SKSEMenuFramework::AddWindow(Render, true);
		if (!g_window) {
			logger::error("SKSE Menu Framework did not create the keyboard map window");
			return;
		}
		Apply();
		// The thread always runs (it only sleeps while the map is off), so the settings page can
		// turn the map on or change its key without a restart.
		bool expected = false;
		if (g_threadStarted.compare_exchange_strong(expected, true)) {
			std::thread(TriggerLoop).detach();
		}
	}
}
