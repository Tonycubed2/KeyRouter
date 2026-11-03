#include "KillSwitch.h"

#include "Keys.h"
#include "Settings.h"

#include <Windows.h>

namespace KillSwitch
{
	namespace
	{
		std::atomic<std::uint32_t> g_code{ 0 };  // 0 = no usable key
		std::atomic<bool>          g_started{ false };

		// Skyrim, or a window it opened (an error box), is the active window
		bool SkyrimInFront()
		{
			const HWND window = GetForegroundWindow();
			if (!window) {
				return false;
			}
			DWORD processId = 0;
			GetWindowThreadProcessId(window, &processId);
			return processId == GetCurrentProcessId();
		}

		void Run()
		{
			using clock = std::chrono::steady_clock;
			std::optional<clock::time_point> heldSince;
			std::uint32_t                    heldCode = 0;
			bool                             warned = false;

			for (;;) {
				std::this_thread::sleep_for(100ms);

				const auto code = g_code.load();
				if (!Settings::killSwitchEnabled || code == 0 || code != heldCode || !Keys::IsDownNow(code) || !SkyrimInFront()) {
					heldSince.reset();
					heldCode = code;
					warned = false;
					continue;
				}
				const auto now = clock::now();
				if (!heldSince) {
					heldSince = now;
					continue;
				}
				const auto holdMs = Settings::killSwitchHoldSeconds.load() * 1000;
				const auto heldMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - *heldSince).count());

				// One beep 3 seconds before the end, so you know it is about to happen
				if (!warned && heldMs + 3000 >= holdMs) {
					warned = true;
					Beep(880, 150);
				}
				if (heldMs >= holdMs) {
					logger::critical("Kill switch: {} held for {} seconds, ending Skyrim", Keys::Name(code), holdMs / 1000);
					if (auto log = spdlog::default_logger()) {
						log->flush();
					}
					Beep(440, 300);
					TerminateProcess(GetCurrentProcess(), 1);
					return;
				}
			}
		}
	}

	bool Apply()
	{
		const auto code = Keys::ParseToken(Settings::killSwitchKey, false);
		if (!code || Keys::ToVirtualKey(*code) == 0) {
			logger::error("Kill switch: could not read key '{}'", Settings::killSwitchKey);
			return false;
		}
		g_code = *code;
		logger::info("Kill switch: {}, hold {} for {} seconds to end Skyrim", Settings::killSwitchEnabled ? "on" : "off",
			Keys::Name(*code), Settings::killSwitchHoldSeconds.load());
		return true;
	}

	void Start()
	{
		Apply();
		// The thread always runs (it only sleeps while the switch is off), so the settings page
		// can turn the switch on or change its key without a restart.
		bool expected = false;
		if (g_started.compare_exchange_strong(expected, true)) {
			std::thread(Run).detach();
		}
	}
}
