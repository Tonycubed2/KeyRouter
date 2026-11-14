#include "Gate.h"

#include "Keys.h"
#include "Router.h"

#include <Windows.h>

namespace Gate
{
	namespace
	{
		constexpr std::uint64_t kUndecidedMs = 150;  // a press the router has not seen yet

		struct Held
		{
			UINT   msg;
			WPARAM wParam;
			LPARAM lParam;
		};

		enum class State
		{
			kNone,
			kPassed,
			kDropped,
			kHeld
		};

		struct KeyGate
		{
			State             state{ State::kNone };
			std::uint64_t     since{ 0 };
			std::vector<Held> queue;
		};

		HWND                                   g_hwnd = nullptr;
		WNDPROC                                g_next = nullptr;
		bool                                   g_unicode = true;
		UINT                                   g_kickMsg = 0;
		std::array<KeyGate, Keys::kMaxCode>    g_keys{};  // window thread only
		std::atomic<int>                       g_heldCount{ 0 };

		std::uint64_t NowMs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		LRESULT Next(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			return g_unicode ? CallWindowProcW(g_next, a_hwnd, a_msg, a_wParam, a_lParam) :
			                   CallWindowProcA(g_next, a_hwnd, a_msg, a_wParam, a_lParam);
		}

		bool IsKeyMessage(UINT a_msg)
		{
			return a_msg == WM_KEYDOWN || a_msg == WM_SYSKEYDOWN || a_msg == WM_KEYUP || a_msg == WM_SYSKEYUP;
		}

		bool IsDown(UINT a_msg) { return a_msg == WM_KEYDOWN || a_msg == WM_SYSKEYDOWN; }

		// DirectInput code from the message: scan code in bits 16-23, extended keys + 0x80
		std::uint32_t CodeOf(LPARAM a_lParam)
		{
			const auto scan = static_cast<std::uint32_t>((a_lParam >> 16) & 0xFF);
			const bool extended = ((a_lParam >> 24) & 1) != 0;
			return scan == 0 ? 0 : (extended ? scan | 0x80 : scan);
		}

		void Flush(HWND a_hwnd, KeyGate& a_key, bool a_pass)
		{
			bool sawUp = false;
			for (const auto& h : a_key.queue) {
				if (a_pass) {
					Next(a_hwnd, h.msg, h.wParam, h.lParam);
				}
				sawUp = sawUp || !IsDown(h.msg);
			}
			a_key.queue.clear();
			--g_heldCount;
			a_key.state = sawUp ? State::kNone : (a_pass ? State::kPassed : State::kDropped);
		}

		// Look at every held key again
		void Recheck(HWND a_hwnd)
		{
			if (g_heldCount.load() <= 0) {
				return;
			}
			const auto now = NowMs();
			for (std::uint32_t code = 0; code < Keys::kMaxCode; ++code) {
				auto& key = g_keys[code];
				if (key.state != State::kHeld) {
					continue;
				}
				switch (Router::WindowKey(code)) {
				case Router::WindowVerdict::kPass:
					Flush(a_hwnd, key, true);
					break;
				case Router::WindowVerdict::kDrop:
					Flush(a_hwnd, key, false);
					break;
				case Router::WindowVerdict::kUndecided:
					// (1.6.3) the press may already have ended: then the messages go where it went
					switch (Router::WindowAfterPress(code, key.since)) {
					case Router::WindowVerdict::kPass:
						Flush(a_hwnd, key, true);
						break;
					case Router::WindowVerdict::kDrop:
						Flush(a_hwnd, key, false);
						break;
					default:
						if (now - key.since > kUndecidedMs) {
							Flush(a_hwnd, key, true);  // the router never saw this press: let it through
						}
						break;
					}
					break;
				case Router::WindowVerdict::kHold:
					break;
				}
			}
		}

		LRESULT CALLBACK GateProc(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (a_msg == g_kickMsg && g_kickMsg != 0) {
				Recheck(a_hwnd);
				return 0;
			}
			if (!IsKeyMessage(a_msg)) {
				if (g_heldCount.load() > 0) {
					Recheck(a_hwnd);
				}
				return Next(a_hwnd, a_msg, a_wParam, a_lParam);
			}

			const auto code = CodeOf(a_lParam);
			if (code == 0 || code >= Keys::kMaxCode) {
				return Next(a_hwnd, a_msg, a_wParam, a_lParam);
			}
			auto& key = g_keys[code];
			const bool down = IsDown(a_msg);

			if (key.state == State::kNone && down) {
				switch (Router::WindowKey(code)) {
				case Router::WindowVerdict::kPass:
					key.state = State::kPassed;
					break;
				case Router::WindowVerdict::kDrop:
					key.state = State::kDropped;
					break;
				default:
					key.state = State::kHeld;
					key.since = NowMs();
					++g_heldCount;
					break;
				}
			}

			switch (key.state) {
			case State::kHeld:
				key.queue.push_back({ a_msg, a_wParam, a_lParam });
				Recheck(a_hwnd);
				return 0;
			case State::kDropped:
				if (!down) {
					key.state = State::kNone;
				}
				return 0;
			case State::kPassed:
				if (!down) {
					key.state = State::kNone;
				}
				return Next(a_hwnd, a_msg, a_wParam, a_lParam);
			default:
				return Next(a_hwnd, a_msg, a_wParam, a_lParam);  // a release without a press we saw
			}
		}

		BOOL CALLBACK FindGameWindow(HWND a_hwnd, LPARAM a_out)
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(a_hwnd, &pid);
			if (pid != GetCurrentProcessId()) {
				return TRUE;
			}
			wchar_t name[128]{};
			GetClassNameW(a_hwnd, name, 128);
			if (std::wstring_view(name) == L"Skyrim Special Edition") {
				*reinterpret_cast<HWND*>(a_out) = a_hwnd;
				return FALSE;
			}
			return TRUE;
		}

		std::string ModuleStemOf(const void* a_address)
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					static_cast<LPCWSTR>(a_address), &module) ||
				!module || module == GetModuleHandleW(nullptr)) {
				return {};
			}
			wchar_t path[MAX_PATH]{};
			GetModuleFileNameW(module, path, MAX_PATH);
			auto stem = std::filesystem::path(path).stem().string();
			for (auto& c : stem) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return stem;
		}

		std::string ModuleNameOf(const void* a_address)
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					static_cast<LPCWSTR>(a_address), &module) ||
				!module) {
				return "unknown";
			}
			wchar_t path[MAX_PATH]{};
			GetModuleFileNameW(module, path, MAX_PATH);
			return std::filesystem::path(path).filename().string();
		}
	}

	void Install()
	{
		HWND hwnd = nullptr;
		EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&hwnd));
		if (!hwnd) {
			logger::warn("Window gate not installed: the game window was not found");
			return;
		}
		g_unicode = IsWindowUnicode(hwnd) != FALSE;
		g_kickMsg = RegisterWindowMessageW(L"KeyRouter.Gate.Recheck");
		const auto before = reinterpret_cast<const void*>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
		const auto old = g_unicode ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GateProc)) :
		                             SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GateProc));
		if (!old) {
			logger::warn("Window gate not installed: SetWindowLongPtr failed ({})", GetLastError());
			return;
		}
		g_next = reinterpret_cast<WNDPROC>(old);
		g_hwnd = hwnd;
		Router::SetWindowReader(ModuleStemOf(before));
		logger::info("Window gate installed on the game window, in front of {}", ModuleNameOf(before));
	}

	void Kick()
	{
		if (g_hwnd && g_kickMsg && g_heldCount.load() > 0) {
			PostMessageW(g_hwnd, g_kickMsg, 0, 0);
		}
	}
}
