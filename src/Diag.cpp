#include "Diag.h"

#include "Keys.h"
#include "Owners.h"
#include "Settings.h"

#include <Windows.h>
#include <Psapi.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

namespace Diag
{
	namespace
	{
		std::atomic<std::uint64_t> g_calls{ 0 };
		std::atomic<std::uint64_t> g_empty{ 0 };
		std::atomic<std::uint64_t> g_lastCallMs{ 0 };
		std::atomic<std::uint64_t> g_maxGapMs{ 0 };
		std::atomic<bool>          g_started{ false };

		std::atomic<std::uint64_t> g_diReads{ 0 };
		std::atomic<std::uint64_t> g_busySince{ 0 };  // 0 = input hook not running
		std::atomic<const char*>   g_stepWhat{ "" };
		std::atomic<const char*>   g_stepDetail{ "" };
		std::atomic<std::uint64_t> g_stepAt{ 0 };
		std::atomic<std::uint32_t> g_busyThread{ 0 };
		std::atomic<bool>          g_reportedStuck{ false };

		std::uint64_t NowMs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		std::string ModuleOfVtable(const void* a_object)
		{
			if (!a_object) {
				return "null";
			}
			const void* vtbl = *static_cast<const void* const*>(a_object);
			HMODULE     module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(vtbl), &module) ||
				!module) {
				return std::format("unknown (vtable 0x{:X})", reinterpret_cast<std::uintptr_t>(vtbl));
			}
			wchar_t      buffer[MAX_PATH]{};
			const DWORD  length = GetModuleFileNameW(module, buffer, MAX_PATH);
			std::wstring path(buffer, length);
			const auto   slash = path.find_last_of(L"\\/");
			std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
			std::string  out;
			for (const wchar_t c : file) {
				out.push_back(c < 128 ? static_cast<char>(c) : '?');
			}
			const auto base = reinterpret_cast<std::uintptr_t>(module);
			return std::format("{}+0x{:X}", out, reinterpret_cast<std::uintptr_t>(vtbl) - base);
		}

		const char* ContextName(std::uint32_t a_context)
		{
			switch (a_context) {
			case 0:
				return "Gameplay";
			case 1:
				return "MenuMode";
			case 2:
				return "Console";
			case 3:
				return "ItemMenu";
			case 4:
				return "Inventory";
			case 5:
				return "DebugText";
			case 6:
				return "Favorites";
			case 7:
				return "Map";
			case 8:
				return "Stats";
			case 9:
				return "Cursor";
			case 10:
				return "Book";
			case 11:
				return "DebugOverlay";
			case 12:
				return "Journal";
			case 13:
				return "TFCMode";
			case 14:
				return "MapDebug";
			case 15:
				return "Lockpicking";
			case 16:
				return "Favor";
			default:
				return "?";
			}
		}
	}

	void Start()
	{
		if (!Settings::diagnostics) {
			return;
		}
		bool expected = false;
		if (!g_started.compare_exchange_strong(expected, true)) {
			return;
		}
		logger::info("[diag] diagnostics on");
		std::thread([] {
			std::uint64_t lastCalls = 0;
			std::uint64_t lastEmpty = 0;
			std::uint64_t lastDi = 0;
			RECT          lastRect{ 1, 1, 1, 1 };
			for (;;) {
				std::this_thread::sleep_for(1s);
				if (const auto since = g_busySince.load(); since && NowMs() - since > 2000) {
					if (!g_reportedStuck.exchange(true)) {
						logger::critical("[diag] INPUT HOOK STUCK for {} ms on thread {}: at step '{} {}' (entered {} ms ago)",
							NowMs() - since, g_busyThread.load(), g_stepWhat.load(), g_stepDetail.load(), NowMs() - g_stepAt.load());
					}
				}
				const auto calls = g_calls.load();
				const auto empty = g_empty.load();
				const auto last = g_lastCallMs.load();
				const auto sinceLast = last ? NowMs() - last : 0;
				const auto di = g_diReads.load();
				logger::info("[diag] input hook last second: {} calls, {} with no input events, longest gap {} ms, {} ms since last call; DirectInput reads: {}",
					calls - lastCalls, empty - lastEmpty, g_maxGapMs.exchange(0), sinceLast, di - lastDi);
				lastDi = di;

				// Skyrim's main window position, logged whenever it changes, to see when it moves
				struct Find
				{
					static BOOL CALLBACK Each(HWND a_window, LPARAM a_out)
					{
						DWORD pid = 0;
						GetWindowThreadProcessId(a_window, &pid);
						char title[64]{};
						GetWindowTextA(a_window, title, sizeof(title));
						// SSE Display Tweaks logs the window as created with the title "Skyrim Special Edition"
						if (pid == GetCurrentProcessId() && std::string_view(title) == "Skyrim Special Edition") {
							*reinterpret_cast<HWND*>(a_out) = a_window;
							return FALSE;
						}
						return TRUE;
					}
				};
				HWND game = nullptr;
				EnumWindows(&Find::Each, reinterpret_cast<LPARAM>(&game));
				RECT rect{};
				if (game && GetWindowRect(game, &rect) &&
					(rect.left != lastRect.left || rect.top != lastRect.top || rect.right != lastRect.right || rect.bottom != lastRect.bottom)) {
					MONITORINFO mi{ sizeof(MONITORINFO) };
					GetMonitorInfoW(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &mi);
					logger::info("[diag] Skyrim window now at ({},{})-({},{}), on the monitor at ({},{})-({},{}){}",
						rect.left, rect.top, rect.right, rect.bottom, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
						(mi.dwFlags & MONITORINFOF_PRIMARY) ? " (main display)" : "");
					lastRect = rect;
				}
				lastCalls = calls;
				lastEmpty = empty;
			}
		}).detach();
	}

	void CountHookCall(bool a_empty)
	{
		if (!Settings::diagnostics) {
			return;
		}
		const auto now = NowMs();
		const auto previous = g_lastCallMs.exchange(now);
		if (previous) {
			const auto gap = now - previous;
			auto       currentMax = g_maxGapMs.load();
			while (gap > currentMax && !g_maxGapMs.compare_exchange_weak(currentMax, gap)) {
			}
		}
		++g_calls;
		if (a_empty) {
			++g_empty;
		}
	}

	void TraceState(std::string_view a_what, std::uint32_t a_code, std::string_view a_where)
	{
		if (!Settings::diagnostics) {
			return;
		}
		logger::info("[diag] ---- {} {} ({}) on thread {}", a_what, Keys::Name(a_code), a_where, GetCurrentThreadId());

		if (auto* ui = RE::UI::GetSingleton()) {
			// IMenu has no name field in SE/AE, so names come from the UI's menu map
			std::string menus;
			for (const auto& [name, entry] : ui->menuMap) {
				if (entry.menu && entry.menu->OnStack()) {
					menus += (menus.empty() ? "" : ", ") + std::string(name.c_str() ? name.c_str() : "?");
				}
			}
			logger::info("[diag] UI: numPausesGame={} menus on stack ({}): {}", ui->numPausesGame, ui->menuStack.size(), menus.empty() ? "(none)" : menus);
		}

		if (auto* controlMap = RE::ControlMap::GetSingleton()) {
			std::string stack;
			for (const auto context : controlMap->GetRuntimeData().contextPriorityStack) {
				stack += (stack.empty() ? "" : " > ") + std::string(ContextName(static_cast<std::uint32_t>(context)));
			}
			logger::info("[diag] input context stack (bottom > top): {}", stack.empty() ? "(empty)" : stack);
		}

		if (auto* menuControls = RE::MenuControls::GetSingleton()) {
			const auto* raw = reinterpret_cast<const std::uint8_t*>(menuControls);
			logger::info("[diag] MenuControls: {} handlers, isProcessing={} remapMode={} byte83={}",
				menuControls->handlers.size(), raw[0x80], raw[0x82], raw[0x83]);
			std::uint32_t i = 0;
			for (auto* handler : menuControls->handlers) {
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(handler);
				logger::info("[diag]   menu handler {}: {} registered(+0C)={} +0D={}", i++, ModuleOfVtable(handler),
					handler ? bytes[0x0C] : 0, handler ? bytes[0x0D] : 0);
			}
		}

		if (auto* playerControls = RE::PlayerControls::GetSingleton()) {
			logger::info("[diag] PlayerControls: {} handlers", playerControls->handlers.size());
			std::uint32_t i = 0;
			for (auto* handler : playerControls->handlers) {
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(handler);
				logger::info("[diag]   player handler {}: {} enabled(+08)={}", i++, ModuleOfVtable(handler), handler ? bytes[0x08] : 0);
			}
		}
	}

	void CountDirectInputRead()
	{
		if (Settings::diagnostics) {
			++g_diReads;
		}
	}

	namespace
	{
		using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
		using MoveWindow_t = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
		using SetWindowPlacement_t = BOOL(WINAPI*)(HWND, const WINDOWPLACEMENT*);

		SetWindowPos_t       g_realSetWindowPos = nullptr;
		MoveWindow_t         g_realMoveWindow = nullptr;
		SetWindowPlacement_t g_realSetWindowPlacement = nullptr;

		bool IsOurTopWindow(HWND a_window)
		{
			if (!a_window || GetAncestor(a_window, GA_ROOT) != a_window) {
				return false;
			}
			DWORD pid = 0;
			GetWindowThreadProcessId(a_window, &pid);
			return pid == GetCurrentProcessId();
		}

		std::string ModuleName(const void* a_address)
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					static_cast<LPCWSTR>(a_address), &module) ||
				!module) {
				return std::format("unknown 0x{:X}", reinterpret_cast<std::uintptr_t>(a_address));
			}
			wchar_t     buffer[MAX_PATH]{};
			const DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
			std::string out;
			for (DWORD i = 0; i < length; ++i) {
				out.push_back(buffer[i] < 128 ? static_cast<char>(buffer[i]) : '?');
			}
			const auto slash = out.find_last_of("\\/");
			return std::format("{}+0x{:X}", slash == std::string::npos ? out : out.substr(slash + 1),
				reinterpret_cast<std::uintptr_t>(a_address) - reinterpret_cast<std::uintptr_t>(module));
		}

		std::string WindowTitle(HWND a_window)
		{
			char title[128]{};
			GetWindowTextA(a_window, title, sizeof(title));
			return title;
		}

		BOOL WINAPI Hook_SetWindowPos(HWND a_window, HWND a_after, int a_x, int a_y, int a_cx, int a_cy, UINT a_flags)
		{
			if (IsOurTopWindow(a_window) && !(a_flags & SWP_NOMOVE)) {
				logger::info("[diag] window '{}' SetWindowPos to ({},{}) size {}x{} flags 0x{:X}, called from {}",
					WindowTitle(a_window), a_x, a_y, a_cx, a_cy, a_flags, ModuleName(_ReturnAddress()));
			}
			return g_realSetWindowPos(a_window, a_after, a_x, a_y, a_cx, a_cy, a_flags);
		}

		BOOL WINAPI Hook_MoveWindow(HWND a_window, int a_x, int a_y, int a_w, int a_h, BOOL a_repaint)
		{
			if (IsOurTopWindow(a_window)) {
				logger::info("[diag] window '{}' MoveWindow to ({},{}) size {}x{}, called from {}",
					WindowTitle(a_window), a_x, a_y, a_w, a_h, ModuleName(_ReturnAddress()));
			}
			return g_realMoveWindow(a_window, a_x, a_y, a_w, a_h, a_repaint);
		}

		BOOL WINAPI Hook_SetWindowPlacement(HWND a_window, const WINDOWPLACEMENT* a_placement)
		{
			if (IsOurTopWindow(a_window) && a_placement) {
				const auto& r = a_placement->rcNormalPosition;
				logger::info("[diag] window '{}' SetWindowPlacement to ({},{})-({},{}) show {}, called from {}",
					WindowTitle(a_window), r.left, r.top, r.right, r.bottom, a_placement->showCmd, ModuleName(_ReturnAddress()));
			}
			return g_realSetWindowPlacement(a_window, a_placement);
		}

		int PatchUser32Import(HMODULE a_module, const char* a_function, void* a_hook)
		{
			auto*      base = reinterpret_cast<std::uint8_t*>(a_module);
			const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
				return 0;
			}
			const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
				return 0;
			}
			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (dir.VirtualAddress == 0) {
				return 0;
			}
			int patched = 0;
			for (auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
				const auto* dllName = reinterpret_cast<const char*>(base + imp->Name);
				if (_stricmp(dllName, "user32.dll") != 0 || imp->OriginalFirstThunk == 0) {
					continue;
				}
				auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + imp->OriginalFirstThunk);
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imp->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++slots) {
					if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
						continue;
					}
					const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
					if (std::strcmp(reinterpret_cast<const char*>(byName->Name), a_function) != 0 ||
						slots->u1.Function == reinterpret_cast<ULONGLONG>(a_hook)) {
						continue;
					}
					DWORD old = 0;
					if (VirtualProtect(&slots->u1.Function, sizeof(ULONGLONG), PAGE_READWRITE, &old)) {
						slots->u1.Function = reinterpret_cast<ULONGLONG>(a_hook);
						VirtualProtect(&slots->u1.Function, sizeof(ULONGLONG), old, &old);
						++patched;
					}
				}
			}
			return patched;
		}
	}

	void InstallWindowWatch()
	{
		if (!Settings::diagnostics) {
			return;
		}
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32) {
			return;
		}
		g_realSetWindowPos = reinterpret_cast<SetWindowPos_t>(GetProcAddress(user32, "SetWindowPos"));
		g_realMoveWindow = reinterpret_cast<MoveWindow_t>(GetProcAddress(user32, "MoveWindow"));
		g_realSetWindowPlacement = reinterpret_cast<SetWindowPlacement_t>(GetProcAddress(user32, "SetWindowPlacement"));
		if (!g_realSetWindowPos || !g_realMoveWindow || !g_realSetWindowPlacement) {
			return;
		}

		HMODULE self = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&InstallWindowWatch), &self);

		std::vector<HMODULE> modules(2048);
		DWORD                needed = 0;
		if (!EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) {
			return;
		}
		modules.resize(std::min<std::size_t>(modules.size(), needed / sizeof(HMODULE)));

		int total = 0;
		for (const auto module : modules) {
			if (module == self || module == user32) {
				continue;
			}
			total += PatchUser32Import(module, "SetWindowPos", reinterpret_cast<void*>(&Hook_SetWindowPos));
			total += PatchUser32Import(module, "MoveWindow", reinterpret_cast<void*>(&Hook_MoveWindow));
			total += PatchUser32Import(module, "SetWindowPlacement", reinterpret_cast<void*>(&Hook_SetWindowPlacement));
		}
		logger::info("[diag] window watch: {} window-move imports redirected in {} modules", total, modules.size());
	}

	void HookEnter()
	{
		if (!Settings::diagnostics) {
			return;
		}
		g_busyThread = GetCurrentThreadId();
		g_stepWhat = "start";
		g_stepDetail = "";
		g_stepAt = NowMs();
		g_busySince = NowMs();
	}

	void HookStep(const char* a_what, const char* a_detail)
	{
		if (!Settings::diagnostics) {
			return;
		}
		g_stepWhat = a_what;
		g_stepDetail = a_detail ? a_detail : "";
		g_stepAt = NowMs();
	}

	void HookLeave()
	{
		if (!Settings::diagnostics) {
			return;
		}
		g_busySince = 0;
	}

	void TraceListener(std::string_view a_owner, bool a_sawIt, int a_result)
	{
		if (!Settings::diagnostics) {
			return;
		}
		logger::info("[diag]   listener {}: {}, returned {}", a_owner, a_sawIt ? "got the replayed event" : "filtered out", a_result);
	}
}
