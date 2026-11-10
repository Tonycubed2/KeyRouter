#include "Quick.h"

#include "Poll.h"
#include "Router.h"

#include <Windows.h>

#include "SKSEMenuFramework.h"

namespace Quick
{
	namespace
	{
		using namespace ImGuiMCP;

		MENU_WINDOW g_window = nullptr;

		std::mutex               g_lock;
		std::uint32_t            g_id{ 0 };  // 0 = closed
		std::string              g_title;
		std::vector<std::string> g_names;
		int                      g_highlight{ 0 };
		bool                     g_placeAtMouse{ false };
		ImVec2                   g_pos{ 0, 0 };
		ImVec2                   g_lastMouse{ -1, -1 };
		std::atomic<std::uint64_t> g_closedAt{ 0 };
		std::atomic<bool>          g_openFlag{ false };  // g_id != 0, readable without g_lock
		std::atomic<std::uint64_t> g_openedAt{ 0 };
		std::atomic<std::uint64_t> g_drawnAt{ 0 };  // last frame the list was drawn (0: not yet since it opened)
		constexpr std::uint64_t    kAppearMs = 2000;  // a list not drawn by then is given up

		std::uint64_t NowMs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		// g_lock held: take the answer out; the caller sends it without the lock
		std::optional<std::pair<std::uint32_t, int>> TakeAnswer(int a_index)
		{
			if (g_id == 0 || g_names.empty()) {
				return std::nullopt;
			}
			const auto id = g_id;
			g_id = 0;
			g_openFlag = false;
			g_closedAt = NowMs();
			const int index = std::clamp(a_index, 0, static_cast<int>(g_names.size()) - 1);
			return std::make_pair(id, index);
		}

		void Send(const std::optional<std::pair<std::uint32_t, int>>& a_answer)
		{
			if (!a_answer) {
				return;
			}
			if (g_window) {
				g_window->IsOpen = false;
			}
			Router::AnswerPopupIndex(a_answer->first, a_answer->second);
		}

		void __stdcall Render()
		{
			std::unique_lock lock(g_lock);
			if (g_id == 0) {
				lock.unlock();
				if (g_window) {
					g_window->IsOpen = false;
				}
				return;
			}
			const auto   mouse = GetMousePos();
			const auto*  io = GetIO();
			const ImVec2 screen = io ? io->DisplaySize : ImVec2{ 0, 0 };
			if (g_placeAtMouse) {
				// At the pointer. Right after a load the pointer position is not known yet (ImGui keeps
				// it at -FLT_MAX until the mouse moves): the list then went far off screen and could not
				// be seen. Use the middle of the screen then.
				const bool onScreen = IsMousePosValid(&mouse) && mouse.x >= 0 && mouse.y >= 0 &&
				                      (screen.x <= 0 || (mouse.x < screen.x && mouse.y < screen.y));
				g_pos = onScreen ? mouse : ImVec2{ std::max(0.0F, screen.x * 0.5F - 160.0F), std::max(0.0F, screen.y * 0.4F) };
				logger::info("Quick list placed at {:.0f},{:.0f} ({}; screen {:.0f}x{:.0f})", g_pos.x, g_pos.y,
					onScreen ? "at the mouse pointer" : "mouse position unknown, middle of the screen", screen.x, screen.y);
				g_lastMouse = mouse;
				g_placeAtMouse = false;
			}
			const bool mouseMoved = mouse.x != g_lastMouse.x || mouse.y != g_lastMouse.y;
			g_lastMouse = mouse;
			SetNextWindowPos(g_pos, ImGuiCond_Always, ImVec2{ 0.0F, 0.0F });
			SetNextWindowBgAlpha(0.96F);
			if (!Begin("##KeyRouterQuick", nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
						ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
						ImGuiWindowFlags_NoNav)) {  // no keyboard navigation: with it, Enter "activated" the first line instead of the highlighted one
				End();
				return;
			}
			g_drawnAt = NowMs();
			// keep the whole list on screen (a pointer near the right or bottom edge)
			if (screen.x > 0 && screen.y > 0) {
				const auto size = GetWindowSize();
				const ImVec2 fitted{ std::clamp(g_pos.x, 0.0F, std::max(0.0F, screen.x - size.x)),
					std::clamp(g_pos.y, 0.0F, std::max(0.0F, screen.y - size.y)) };
				g_pos = fitted;
			}
			TextColored(ImVec4{ 0.86F, 0.74F, 0.50F, 1.0F }, "%s", g_title.c_str());
			Separator();
			std::optional<std::pair<std::uint32_t, int>> answer;
			// The highlighted line: a strong gold bar with dark text, so it stands out from the rest
			PushStyleColor(ImGuiCol_Header, ImVec4{ 0.93F, 0.72F, 0.28F, 1.0F });
			PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{ 0.93F, 0.72F, 0.28F, 1.0F });
			PushStyleColor(ImGuiCol_HeaderActive, ImVec4{ 1.0F, 0.82F, 0.40F, 1.0F });
			for (int i = 0; i < static_cast<int>(g_names.size()); ++i) {
				const bool selected = i == g_highlight;
				const auto label = std::format("{}  {}", selected ? ">" : " ", g_names[i]);
				PushID(i);
				if (selected) {
					PushStyleColor(ImGuiCol_Text, ImVec4{ 0.08F, 0.06F, 0.03F, 1.0F });
				}
				if (Selectable(label.c_str(), selected)) {
					answer = TakeAnswer(i);
				}
				if (selected) {
					PopStyleColor();
				}
				// the pointer takes the highlight only when it moves, so it does not fight the wheel
				if (mouseMoved && IsItemHovered()) {
					g_highlight = i;
				}
				PopID();
				if (answer) {
					break;
				}
			}
			PopStyleColor(3);
			// Enter or a wheel click picks the highlighted line
			if (!answer && (IsKeyPressed(ImGuiKey_Enter, false) || IsKeyPressed(ImGuiKey_KeypadEnter, false) || IsMouseClicked(2))) {
				answer = TakeAnswer(g_highlight);
			}
			Separator();
			TextDisabled("Wheel or Up/Down: move   Click, wheel click or Enter: pick   Esc: nobody");
			End();
			lock.unlock();
			Send(answer);
		}
	}

	void Start()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			return;
		}
		g_window = SKSEMenuFramework::AddWindow(Render, true);  // true: the game pauses while open
		if (!g_window) {
			logger::warn("SKSE Menu Framework did not create the quick conflict list; the regular window is used");
		}
	}

	bool Available() { return g_window != nullptr; }

	void Open(std::uint32_t a_popupId, std::string a_title, std::vector<std::string> a_names)
	{
		if (!g_window) {
			return;
		}
		{
			std::scoped_lock lock(g_lock);
			g_id = a_popupId;
			g_openFlag = true;
			g_openedAt = NowMs();
			g_drawnAt = 0;
			g_title = std::move(a_title);
			g_names = std::move(a_names);
			g_highlight = 0;
			g_placeAtMouse = true;
		}
		Poll::ListOpened();
		g_window->IsOpen = true;
	}

	bool IsOpen()
	{
		return g_window && g_window->IsOpen;
	}

	std::uint32_t OpenId()
	{
		std::scoped_lock lock(g_lock);
		return g_id;
	}

	bool Stalled()
	{
		if (!g_openFlag.load()) {
			return false;
		}
		return g_drawnAt.load() == 0 && NowMs() - g_openedAt.load() > kAppearMs;
	}

	bool OpenNoLock()
	{
		return g_openFlag.load(std::memory_order_relaxed);
	}

	void Wheel(int a_steps)
	{
		std::scoped_lock lock(g_lock);
		if (g_id == 0 || g_names.empty() || a_steps == 0) {
			return;
		}
		const int count = static_cast<int>(g_names.size());
		g_highlight = ((g_highlight - a_steps) % count + count) % count;  // wheel up moves up the list
	}

	void PickHighlighted()
	{
		std::optional<std::pair<std::uint32_t, int>> answer;
		{
			std::scoped_lock lock(g_lock);
			answer = TakeAnswer(g_highlight);
		}
		Send(answer);
	}

	bool MouseExclusive()
	{
		if (OpenId() != 0) {
			return true;
		}
		const auto closed = g_closedAt.load();
		return closed != 0 && NowMs() - closed < 500;
	}

	void Close()
	{
		{
			std::scoped_lock lock(g_lock);
			if (g_id != 0) {
				g_closedAt = NowMs();
			}
			g_id = 0;
			g_openFlag = false;
		}
		if (g_window) {
			g_window->IsOpen = false;
		}
	}
}
