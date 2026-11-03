#include "Router.h"

#include "Menu.h"

#include "Choices.h"
#include "Diag.h"
#include "Keys.h"
#include "Owners.h"
#include "Poll.h"
#include "Scanner.h"
#include "Settings.h"

// How a key press travels:
//   The game polls the keyboard and sends one chain of InputEvents per frame through
//   BSInputDeviceManager (a BSTEventSource), on whichever game thread runs the input job. Every
//   listener (the game's own controls, SKSE, each SKSE DLL) is a sink in that source. SKSE's sink
//   turns key presses into Papyrus OnKeyDown/OnKeyUp/OnControlDown/OnControlUp events.
//
// What Key Router does each frame, inside a hook on that send:
//   1. Runs SKSE's sink first in "capture" mode: the Papyrus key events it produces are caught in
//      a hook on the VM's SendEvent and held, so we know exactly which scripts want which key.
//   2. For each new key press, lists every mod that wants it: those scripts (named by plugin),
//      DLL mods whose config files use that key, and the game's own controls. A plugin and a DLL
//      with the same name are one mod. Two or more mods = conflict: the press is held back from
//      all of them and a window asks the player who gets it. Nothing is remembered.
//   3. Sends the frame through the game, each listener wrapped for that one send so it only sees
//      the events it is allowed to see; held Papyrus events are released only where allowed.
//   4. When the player picks, the press is put into the next real input frame (so it travels the
//      same way as a real press, on the input thread) and only the picked mod is allowed to see
//      it; the release follows SyntheticPressMs later, or the player's own release if the key is
//      still held. If no input frame comes along, it is sent straight to the picked mod instead.
//
// State is shared between the input thread, the main thread (window answer, ticker) and the
// Papyrus hook, so it is guarded by g_lock. The lock is never held while another mod's code
// runs (the frame send itself, or a direct delivery), so nothing can deadlock on it.

namespace Router
{
	namespace
	{
		using Sink = RE::BSTEventSink<RE::InputEvent*>;
		using Source = RE::BSTEventSource<RE::InputEvent*>;
		using Kind = Owners::Kind;
		using Notify = RE::BSEventNotifyControl;

		constexpr std::uint8_t  kButtonOffset = 4;  // message box reports button N as 4 + N
		constexpr std::uint32_t kPapyrusGamepadBase = 266;
		constexpr std::size_t   kMaxChain = 512;
		constexpr std::uint64_t kFallbackMs = 400;  // no input frame to carry a delivery: send it directly

		enum class PapKind
		{
			kKeyDown,
			kKeyUp,
			kControlDown,
			kControlUp
		};

		struct PapEvent
		{
			RE::VMHandle      handle{ 0 };
			RE::BSFixedString name;
			PapKind           kind{ PapKind::kKeyDown };
			std::int32_t      code{ 0 };
			float             hold{ 0.0F };
			RE::BSFixedString control;
		};

		// One competitor. A plugin and a DLL with the same name are merged into one, with both ids.
		struct Candidate
		{
			Kind                     kind{ Kind::kUnknown };
			std::vector<std::string> ids;  // "pap:foo.esp", "dll:foo", "game"
			std::string              display;
			std::string              stem;  // lowercase name without extension, for merging
		};

		bool HasId(const Candidate& a_candidate, const std::string& a_id)
		{
			return std::find(a_candidate.ids.begin(), a_candidate.ids.end(), a_id) != a_candidate.ids.end();
		}

		// Stable name of a competitor for the choices file: its ids, sorted, joined with "+"
		// ("dll:dragonseyeminimap", "dll:spellhotbar+pap:spellhotbar.esp", "game")
		std::string CandidateKey(const Candidate& a_candidate)
		{
			auto ids = a_candidate.ids;
			std::sort(ids.begin(), ids.end());
			std::string key;
			for (const auto& id : ids) {
				key += (key.empty() ? "" : "+") + id;
			}
			return key;
		}

		std::vector<std::string> CandidateKeys(const std::vector<Candidate>& a_candidates)
		{
			std::vector<std::string> keys;
			for (const auto& c : a_candidates) {
				keys.push_back(CandidateKey(c));
			}
			return keys;
		}

		enum class Mode
		{
			kIdle,      // not pressed
			kFree,      // pressed, no conflict: everyone gets it
			kPending,   // conflict, window open: no competitor gets it
			kAnswered,  // player picked; waiting to deliver
			kRouted     // only the picked mod (and non-competitors) gets this key
		};

		struct KeyState
		{
			Mode                     mode{ Mode::kIdle };
			std::vector<Candidate>   candidates;
			std::vector<std::string> owners;  // ids of the picked mod; empty = nobody
			bool                     synthetic{ false };  // press made by Key Router, release comes from Key Router
		};

		struct Popup
		{
			std::uint32_t          id{ 0 };
			std::uint32_t          code{ 0 };
			std::vector<Candidate> candidates;
			std::vector<PapEvent>  papDowns;
			RE::BSFixedString      userEvent;
			bool                   fromGameplay{ true };
			std::uint64_t          createdAt{ 0 };
			std::uint64_t          lastSeenOpen{ 0 };
			bool                   seenOpen{ false };
			bool                   remember{ false };  // "Remember my choice: ON"
		};

		enum class Phase
		{
			kWaitClose,  // window still closing
			kSendDown,   // next input frame carries the press
			kHold,       // press sent, release follows after SyntheticPressMs
			kSendUp,     // next input frame carries the release
			kDirect      // being sent straight to the picked mod (no frame came along)
		};

		struct Delivery
		{
			std::uint32_t         code{ 0 };
			Candidate             who;
			std::vector<PapEvent> papDowns;
			RE::BSFixedString     userEvent;
			bool                  fromGameplay{ true };
			Phase                 phase{ Phase::kWaitClose };
			std::uint64_t         answeredAt{ 0 };
			std::uint64_t         phaseAt{ 0 };
		};

		std::recursive_mutex g_lock;

		// ---- state (g_lock)
		std::array<KeyState, Keys::kMaxCode> g_keys;
		std::array<bool, Keys::kMaxCode>     g_physical{};
		std::optional<Popup>                 g_popup;
		std::optional<Delivery>              g_delivery;
		std::uint32_t                        g_popupSeq{ 0 };

		// ---- the frame being sent (g_lock)
		bool                      g_inDispatch{ false };
		bool                      g_frameBusy{ false };  // window up when the frame began
		bool                      g_papReleased{ false };
		std::vector<PapEvent>     g_captured;
		std::unordered_set<Sink*> g_removedSinks;
		bool                      g_loggedCapture{ false };
		bool                      g_traceFrame{ false };  // this frame carries a replayed press: log each listener
		std::uint32_t             g_traceCode{ 0 };

		// ---- Papyrus capture, read inside the VM hook (which any thread can call)
		std::atomic<bool>            g_capturing{ false };
		std::atomic<std::thread::id> g_captureThread{};

		// ---- ticker: window timeout and delivery timing, run on the main thread
		std::atomic<bool> g_workPending{ false };
		std::atomic<bool> g_taskQueued{ false };
		std::atomic<bool> g_tickerStarted{ false };

		using InputFn = void (*)(Source*, RE::InputEvent* const*);
		using SendEventFn = void (*)(RE::BSScript::IVirtualMachine*, RE::VMHandle, const RE::BSFixedString&, RE::BSScript::IFunctionArguments*);

		InputFn     g_originalInput{ nullptr };
		SendEventFn g_originalSendEvent{ nullptr };

		void Tick();

		std::uint64_t NowMs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		// g_lock held
		void UpdateWork()
		{
			bool stale = false;
			for (const auto& state : g_keys) {
				if (state.mode == Mode::kFree || state.mode == Mode::kRouted) {
					stale = true;
					break;
				}
			}
			g_workPending = g_popup.has_value() || g_delivery.has_value() || stale;
		}

		void StartTicker()
		{
			bool expected = false;
			if (!g_tickerStarted.compare_exchange_strong(expected, true)) {
				return;
			}
			std::thread([] {
				for (;;) {
					std::this_thread::sleep_for(16ms);
					if (!g_workPending.load()) {
						continue;
					}
					bool queued = false;
					if (!g_taskQueued.compare_exchange_strong(queued, true)) {
						continue;
					}
					if (auto* task = SKSE::GetTaskInterface()) {
						task->AddTask([] {
							g_taskQueued = false;
							Tick();
						});
					} else {
						g_taskQueued = false;
					}
				}
			}).detach();
		}

		// ------------------------------------------------------------------ proxies

		class ProxySink;
		Notify ProxyCall(ProxySink* a_proxy, RE::InputEvent* const* a_event, Source* a_source);

		// Stands in for a real listener while one frame is sent, so it can be given a filtered chain
		class ProxySink final : public Sink
		{
		public:
			explicit ProxySink(Sink* a_original) :
				original(a_original)
			{}

			Notify ProcessEvent(RE::InputEvent* const* a_event, Source* a_source) override
			{
				return ProxyCall(this, a_event, a_source);
			}

			Sink* original;
		};

		std::unordered_map<Sink*, ProxySink*> g_proxyOf;  // g_lock
		std::unordered_set<Sink*>             g_proxies;  // g_lock

		Sink* Unwrap(Sink* a_sink)
		{
			if (a_sink && g_proxies.contains(a_sink)) {
				return static_cast<ProxySink*>(a_sink)->original;
			}
			return a_sink;
		}

		// g_lock held
		std::vector<Sink*> SnapshotSinks(Source* a_source)
		{
			std::vector<Sink*>  out;
			RE::BSSpinLockGuard lock(a_source->lock);
			for (auto* sink : a_source->sinks) {
				if (auto* real = Unwrap(sink)) {
					out.push_back(real);
				}
			}
			return out;
		}

		template <class T>
		bool ArrayContains(const RE::BSTArray<T>& a_array, const T& a_value)
		{
			return std::find(a_array.begin(), a_array.end(), a_value) != a_array.end();
		}

		// ------------------------------------------------------------------ keys

		// Tracked code of a button event, or nothing for events Key Router leaves alone
		std::optional<std::uint32_t> CodeOf(RE::InputEvent* a_event)
		{
			if (!a_event || a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
				return std::nullopt;
			}
			auto* button = a_event->AsButtonEvent();
			if (!button) {
				return std::nullopt;
			}
			const auto id = button->GetIDCode();
			switch (a_event->GetDevice()) {
			case RE::INPUT_DEVICE::kKeyboard:
				if (id < Keys::kMouseBase) {
					return id;
				}
				break;
			case RE::INPUT_DEVICE::kMouse:
				if (Settings::includeMouseButtons && id <= Keys::kMouseLast - Keys::kMouseBase) {
					return Keys::kMouseBase + id;
				}
				break;
			default:
				break;
			}
			return std::nullopt;
		}

		// Papyrus key code of a keyboard or mouse button event (tracked or not)
		std::optional<std::uint32_t> PapyrusCodeOf(RE::InputEvent* a_event)
		{
			if (!a_event || a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
				return std::nullopt;
			}
			auto* button = a_event->AsButtonEvent();
			if (!button) {
				return std::nullopt;
			}
			const auto id = button->GetIDCode();
			switch (a_event->GetDevice()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return id < Keys::kMouseBase ? std::optional<std::uint32_t>(id) : std::nullopt;
			case RE::INPUT_DEVICE::kMouse:
				return Keys::kMouseBase + id;
			default:
				return std::nullopt;
			}
		}

		bool InGameplay()
		{
			auto* controlMap = RE::ControlMap::GetSingleton();
			if (!controlMap) {
				return false;
			}
			const auto& stack = controlMap->GetRuntimeData().contextPriorityStack;
			return !stack.empty() && stack[stack.size() - 1] == RE::ControlMap::InputContextID::kGameplay;
		}

		std::uint8_t HeldModifiers(std::uint32_t a_except)
		{
			std::uint8_t mods = 0;
			for (const std::uint32_t code : { 0x2Au, 0x36u, 0x1Du, 0x9Du, 0x38u, 0xB8u }) {
				if (code != a_except && g_physical[code]) {
					mods |= Keys::ModifierBit(code);
				}
			}
			return mods;
		}

		std::string GameUserEvent(std::uint32_t a_code)
		{
			auto* controlMap = RE::ControlMap::GetSingleton();
			if (!controlMap) {
				return {};
			}
			const bool mouse = a_code >= Keys::kMouseBase;
			const auto name = controlMap->GetUserEventName(
				mouse ? a_code - Keys::kMouseBase : a_code,
				mouse ? RE::INPUT_DEVICE::kMouse : RE::INPUT_DEVICE::kKeyboard,
				RE::ControlMap::InputContextID::kGameplay);
			return std::string(name);
		}

		bool IsPassive(const Candidate& a_candidate)
		{
			std::scoped_lock lock(Settings::listLock);  // the settings page can change the list
			if (Settings::passiveMods.empty()) {
				return false;
			}
			const auto display = Owners::Lower(a_candidate.display);
			for (const auto& p : Settings::passiveMods) {
				if (p == display || (!a_candidate.stem.empty() && p == a_candidate.stem)) {
					return true;
				}
				for (const auto& id : a_candidate.ids) {
					const auto colon = id.find(':');
					if (p == (colon == std::string::npos ? id : id.substr(colon + 1))) {
						return true;
					}
				}
				if (a_candidate.kind == Kind::kGame && (p == "game" || p == "skyrim")) {
					return true;
				}
			}
			return false;
		}

		bool IsCandidate(const KeyState& a_state, const std::string& a_id)
		{
			return std::any_of(a_state.candidates.begin(), a_state.candidates.end(),
				[&](const Candidate& c) { return HasId(c, a_id); });
		}

		bool IsOwner(const KeyState& a_state, const std::string& a_id)
		{
			return std::find(a_state.owners.begin(), a_state.owners.end(), a_id) != a_state.owners.end();
		}

		// g_lock held. May a listener see an event for this key right now?
		bool AllowFor(Kind a_kind, const std::string& a_id, std::uint32_t a_code)
		{
			if (a_code < Keys::kMaxCode) {
				const auto& state = g_keys[a_code];
				switch (state.mode) {
				case Mode::kPending:
				case Mode::kAnswered:
					return !IsCandidate(state, a_id);
				case Mode::kRouted:
					return IsOwner(state, a_id) || !IsCandidate(state, a_id);
				default:
					break;
				}
			}
			// While the window is up, only the game (which runs the window) gets keys
			if (g_frameBusy && a_kind != Kind::kGame) {
				return false;
			}
			return true;
		}

		// g_lock held
		void SyncPoll(std::uint32_t a_code)
		{
			const auto&              state = g_keys[a_code];
			std::vector<std::string> blocked;
			std::string              forced;
			auto                     dllIds = [](const Candidate& a_candidate, auto&& a_fn) {
                for (const auto& id : a_candidate.ids) {
                    if (id.starts_with("dll:")) {
                        a_fn(id.substr(4));
                    }
                }
			};

			switch (state.mode) {
			case Mode::kPending:
			case Mode::kAnswered:
				for (const auto& c : state.candidates) {
					dllIds(c, [&](const std::string& a_name) { blocked.push_back(a_name); });
				}
				break;
			case Mode::kRouted:
				for (const auto& c : state.candidates) {
					const bool picked = std::any_of(c.ids.begin(), c.ids.end(), [&](const std::string& id) { return IsOwner(state, id); });
					dllIds(c, [&](const std::string& a_name) {
						if (!picked) {
							blocked.push_back(a_name);
						} else if (state.synthetic && forced.empty()) {
							forced = a_name;
						}
					});
				}
				break;
			default:
				Poll::Clear(a_code);
				return;
			}

			if (blocked.empty() && forced.empty()) {
				Poll::Clear(a_code);
			} else {
				Poll::Set(a_code, std::move(blocked), std::move(forced));
			}
		}

		// g_lock held
		void ResetKey(std::uint32_t a_code)
		{
			g_keys[a_code] = KeyState{};
			g_physical[a_code] = false;
			Poll::Clear(a_code);
		}

		// g_lock held. A key Key Router thinks is still held, but Windows says is up: its release
		// was swallowed somewhere (another mod blocking input, a menu). End the press.
		void ReconcileReleases(const std::unordered_set<std::uint32_t>& a_inFrame)
		{
			const std::uint32_t deliveryCode = g_delivery ? g_delivery->code : Keys::kMaxCode;
			for (std::uint32_t code = 0; code < Keys::kMaxCode; ++code) {
				auto& state = g_keys[code];
				const bool waitingForRelease = state.mode == Mode::kFree || (state.mode == Mode::kRouted && !state.synthetic);
				if (!waitingForRelease || code == deliveryCode || a_inFrame.contains(code)) {
					continue;
				}
				if (!Keys::IsDownNow(code)) {
					ResetKey(code);
				}
			}
		}

		// ------------------------------------------------------------------ Papyrus

		bool EqualsNoCase(std::string_view a_left, std::string_view a_right)
		{
			return a_left.size() == a_right.size() &&
			       std::equal(a_left.begin(), a_left.end(), a_right.begin(), [](char a, char b) {
					   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
				   });
		}

		std::optional<PapKind> PapKindOf(const RE::BSFixedString& a_name)
		{
			const char* text = a_name.c_str();
			if (!text) {
				return std::nullopt;
			}
			const std::string_view name{ text };
			if (EqualsNoCase(name, "OnKeyDown"sv)) {
				return PapKind::kKeyDown;
			}
			if (EqualsNoCase(name, "OnKeyUp"sv)) {
				return PapKind::kKeyUp;
			}
			if (EqualsNoCase(name, "OnControlDown"sv)) {
				return PapKind::kControlDown;
			}
			if (EqualsNoCase(name, "OnControlUp"sv)) {
				return PapKind::kControlUp;
			}
			return std::nullopt;
		}

		void Hook_SendEvent(RE::BSScript::IVirtualMachine* a_vm, RE::VMHandle a_handle, const RE::BSFixedString& a_name, RE::BSScript::IFunctionArguments* a_args)
		{
			if (g_capturing.load(std::memory_order_acquire) && a_args && std::this_thread::get_id() == g_captureThread.load()) {
				if (const auto kind = PapKindOf(a_name)) {
					RE::BSScrapArray<RE::BSScript::Variable> vars;
					if ((*a_args)(vars)) {
						PapEvent ev;
						ev.handle = a_handle;
						ev.name = a_name;
						ev.kind = *kind;
						if (*kind == PapKind::kKeyDown || *kind == PapKind::kKeyUp) {
							if (!vars.empty() && vars[0].IsInt()) {
								ev.code = vars[0].GetSInt();
							}
						} else if (!vars.empty() && vars[0].IsString()) {
							ev.control = RE::BSFixedString(vars[0].GetString());
						}
						if (vars.size() > 1 && vars[1].IsFloat()) {
							ev.hold = vars[1].GetFloat();
						}
						// the capturing thread holds g_lock
						g_captured.push_back(std::move(ev));
						return;
					}
				}
			}
			g_originalSendEvent(a_vm, a_handle, a_name, a_args);
		}

		void SendPap(const PapEvent& a_event)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !g_originalSendEvent) {
				return;
			}
			std::unique_ptr<RE::BSScript::IFunctionArguments> args;
			switch (a_event.kind) {
			case PapKind::kKeyDown:
				args.reset(RE::MakeFunctionArguments(std::int32_t{ a_event.code }));
				break;
			case PapKind::kKeyUp:
				args.reset(RE::MakeFunctionArguments(std::int32_t{ a_event.code }, float{ a_event.hold }));
				break;
			case PapKind::kControlDown:
				args.reset(RE::MakeFunctionArguments(RE::BSFixedString{ a_event.control }));
				break;
			case PapKind::kControlUp:
				args.reset(RE::MakeFunctionArguments(RE::BSFixedString{ a_event.control }, float{ a_event.hold }));
				break;
			}
			g_originalSendEvent(static_cast<RE::BSScript::IVirtualMachine*>(vm), a_event.handle, a_event.name, args.get());
		}

		// g_lock held. Runs SKSE's input listeners on a chain with capture on; their Papyrus events land in g_captured.
		void CaptureSkse(const std::vector<Sink*>& a_sinks, RE::InputEvent* const* a_events, Source* a_source)
		{
			g_captured.clear();
			g_captureThread = std::this_thread::get_id();
			g_capturing.store(true, std::memory_order_release);
			for (auto* sink : a_sinks) {
				if (Owners::ForSink(sink).kind == Kind::kSkse) {
					sink->ProcessEvent(a_events, a_source);
				}
			}
			g_capturing.store(false, std::memory_order_release);

			if (!g_loggedCapture && !g_captured.empty()) {
				g_loggedCapture = true;
				const auto& first = g_captured.front();
				if (first.kind == PapKind::kControlDown || first.kind == PapKind::kControlUp) {
					logger::info("Papyrus key events are being routed (first one: {} for control {})", first.name.c_str(), first.control.c_str());
				} else {
					logger::info("Papyrus key events are being routed (first one: {} for key {})", first.name.c_str(), Keys::Name(static_cast<std::uint32_t>(first.code)));
				}
			}
		}

		// g_lock held. Sends the held Papyrus events that are allowed. a_event is the chain SKSE's
		// slot was given in this send: an event is only released if its key is still in that chain
		// (another mod's input hook may have removed it).
		void ReleaseCaptured(RE::InputEvent* const* a_event)
		{
			std::unordered_set<std::uint32_t> present;
			std::vector<RE::InputEvent*>      buttons;
			for (auto* e = a_event ? *a_event : nullptr; e && buttons.size() < kMaxChain; e = e->next) {
				if (const auto code = PapyrusCodeOf(e)) {
					present.insert(*code);
				}
				if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
					buttons.push_back(e);
				}
			}

			for (const auto& ev : g_captured) {
				bool allowed = false;
				if (ev.kind == PapKind::kKeyDown || ev.kind == PapKind::kKeyUp) {
					const auto code = static_cast<std::uint32_t>(ev.code);
					if (code >= kPapyrusGamepadBase) {
						allowed = !g_frameBusy;
					} else if (present.contains(code)) {
						const auto& owner = Owners::ForHandle(ev.handle);
						allowed = AllowFor(owner.kind, owner.id, code);
					}
				} else {
					// OnControlDown/Up follow the game: only if the game is allowed the key behind it
					for (auto* e : buttons) {
						auto* button = e->AsButtonEvent();
						if (!button || button->QUserEvent() != ev.control) {
							continue;
						}
						const auto code = CodeOf(e);
						allowed = code ? AllowFor(Kind::kGame, "game", *code) : true;
						break;
					}
				}
				if (allowed) {
					SendPap(ev);
				}
			}
		}

		// ------------------------------------------------------------------ dispatch

		// The game's MenuControls and PlayerControls listeners each hand every event to a list of
		// handlers, and mods add their own handlers to those lists (Dragon's Eye Minimap adds one to
		// MenuControls). Verified in SkyrimSE.exe 1.6.1170:
		//   MenuControls::ProcessEvent (0x947CA0): for each event, for each handler in handlers
		//     (+0x18, count +0x28): skipped when its byte +0x0C ("registered") is 0, else
		//     CanProcess, then ProcessButton / ProcessMouseMove / ...
		//   PlayerControls::ProcessEvent (0x79A970): same over handlers (+0x58, count +0x68), byte +0x08
		// So a handler that must not see a key is held back by clearing that byte for exactly the
		// one event, then restoring it.
		struct HandlerList
		{
			RE::BSTArray<void*>* list{ nullptr };
			std::size_t          enabledOffset{ 0 };
		};

		std::optional<HandlerList> HandlerListOf(Sink* a_sink)
		{
			if (auto* menuControls = RE::MenuControls::GetSingleton();
				menuControls && a_sink == static_cast<Sink*>(menuControls)) {
				return HandlerList{ reinterpret_cast<RE::BSTArray<void*>*>(&menuControls->handlers), 0x0C };
			}
			if (auto* playerControls = RE::PlayerControls::GetSingleton();
				playerControls && a_sink == static_cast<Sink*>(playerControls)) {
				return HandlerList{ reinterpret_cast<RE::BSTArray<void*>*>(&playerControls->handlers), 0x08 };
			}
			return std::nullopt;
		}

		// g_lock held. Handlers of this list that must not see this event, by their owning DLL.
		std::vector<std::uint8_t*> BlockedHandlers(const HandlerList& a_list, std::uint32_t a_code)
		{
			std::vector<std::uint8_t*> blocked;
			for (auto* handler : *a_list.list) {
				if (!handler) {
					continue;
				}
				// the game's own handlers count as the game, mods' handlers as their DLL
				const auto& owner = Owners::ForSink(handler);
				if ((owner.kind == Kind::kDll || owner.kind == Kind::kGame) && !AllowFor(owner.kind, owner.id, a_code)) {
					blocked.push_back(static_cast<std::uint8_t*>(handler) + a_list.enabledOffset);
				}
			}
			return blocked;
		}

		// Sends the chain one event at a time, clearing the enabled byte of the handlers that must
		// not see that event. Used only when the chain has an event some handler must not see.
		Notify CallPerHandler(Sink* a_sink, const HandlerList& a_list, const std::vector<RE::InputEvent*>& a_events, Source* a_source)
		{
			Notify result = Notify::kContinue;
			for (std::size_t i = 0; i < a_events.size(); ++i) {
				RE::InputEvent* event = a_events[i];
				RE::InputEvent* savedNext = event->next;
				event->next = nullptr;

				std::vector<std::pair<std::uint8_t*, std::uint8_t>> cleared;
				if (const auto code = CodeOf(event)) {
					std::scoped_lock lock(g_lock);
					for (auto* flag : BlockedHandlers(a_list, *code)) {
						if (*flag) {
							cleared.emplace_back(flag, *flag);
							*flag = 0;
						}
					}
					if (Settings::diagnostics && !cleared.empty()) {
						logger::info("[diag] {}: held back from {} game-list handler(s) owned by other mods", Keys::Name(*code), cleared.size());
					}
				}

				RE::InputEvent* head = event;
				Diag::HookStep("per-handler send (some handlers held back) into", Owners::ForSink(a_sink).display.c_str());
				const auto      r = a_sink->ProcessEvent(&head, a_source);

				for (auto& [flag, value] : cleared) {
					*flag = value;
				}
				event->next = savedNext;
				if (r == Notify::kStop) {
					result = Notify::kStop;
				}
			}
			return result;
		}

		Notify CallFiltered(Sink* a_sink, const Owners::Owner& a_owner, RE::InputEvent* const* a_event, Source* a_source)
		{
			if (!a_event || !*a_event) {
				return a_sink->ProcessEvent(a_event, a_source);
			}

			// The game's own lists: filter per handler, not per listener
			if (a_owner.kind == Kind::kGame) {
				if (const auto list = HandlerListOf(a_sink)) {
					std::vector<RE::InputEvent*> events;
					std::vector<RE::InputEvent*> keep;
					bool                         needHandlerFilter = false;
					{
						std::scoped_lock lock(g_lock);
						for (auto* e = *a_event; e && events.size() < kMaxChain; e = e->next) {
							events.push_back(e);
							keep.push_back(e);
							const auto code = CodeOf(e);
							if (code && !BlockedHandlers(*list, *code).empty()) {
								needHandlerFilter = true;
							}
						}
					}
					if (!needHandlerFilter) {
						// fall through to the whole-listener filter below
					} else {
						std::vector<RE::InputEvent*> savedNext(events.size());
						for (std::size_t i = 0; i < events.size(); ++i) {
							savedNext[i] = events[i]->next;
						}
						const auto result = CallPerHandler(a_sink, *list, keep, a_source);
						for (std::size_t i = 0; i < events.size(); ++i) {
							events[i]->next = savedNext[i];
						}
						return result;
					}
				}
			}

			std::vector<RE::InputEvent*> all;
			std::vector<RE::InputEvent*> keep;
			{
				std::scoped_lock lock(g_lock);
				for (auto* e = *a_event; e && all.size() < kMaxChain; e = e->next) {
					all.push_back(e);
					const auto code = CodeOf(e);
					if (!code || AllowFor(a_owner.kind, a_owner.id, *code)) {
						keep.push_back(e);
					}
				}
			}
			if (keep.size() == all.size()) {
				return a_sink->ProcessEvent(a_event, a_source);
			}
			if (keep.empty()) {
				return Notify::kContinue;
			}

			// Relink the chain to the allowed events for this one listener, then put it back
			std::vector<RE::InputEvent*> savedNext(all.size());
			for (std::size_t i = 0; i < all.size(); ++i) {
				savedNext[i] = all[i]->next;
			}
			for (std::size_t i = 0; i < keep.size(); ++i) {
				keep[i]->next = i + 1 < keep.size() ? keep[i + 1] : nullptr;
			}
			RE::InputEvent* head = keep.front();
			const auto      result = a_sink->ProcessEvent(&head, a_source);
			for (std::size_t i = 0; i < all.size(); ++i) {
				all[i]->next = savedNext[i];
			}
			return result;
		}

		Notify ProxyCall(ProxySink* a_proxy, RE::InputEvent* const* a_event, Source* a_source)
		{
			Sink*        original = a_proxy->original;
			Owners::Owner owner;
			{
				std::scoped_lock lock(g_lock);
				if (ArrayContains<Sink*>(a_source->pendingUnregisters, original)) {
					g_removedSinks.insert(original);
					return Notify::kContinue;
				}
				owner = Owners::ForSink(original);
				if (owner.kind == Kind::kSkse) {
					// SKSE already ran in capture mode; release its events now, in its own slot
					if (!g_papReleased) {
						g_papReleased = true;
						Diag::HookStep("releasing Papyrus key events", "");
						ReleaseCaptured(a_event);
					}
				}
			}

			bool traceSaw = false;
			bool trace = false;
			{
				std::scoped_lock lock(g_lock);
				trace = g_traceFrame;
				if (trace) {
					traceSaw = AllowFor(owner.kind, owner.id, g_traceCode);
				}
			}

			Notify result = Notify::kContinue;
			if (owner.kind != Kind::kSkse) {
				Diag::HookStep("inside listener", Owners::ForSink(original).display.c_str());
				result = CallFiltered(original, owner, a_event, a_source);
				Diag::HookStep("back from listener", Owners::ForSink(original).display.c_str());
			}
			if (trace) {
				Diag::TraceListener(owner.display, traceSaw, static_cast<int>(result));
			}

			// The game drops removals it cannot match while the slots hold proxies; remember them
			std::scoped_lock lock(g_lock);
			for (auto* removed : a_source->pendingUnregisters) {
				if (removed && g_proxyOf.contains(removed)) {
					g_removedSinks.insert(removed);
				}
			}
			return result;
		}

		Candidate MakeCandidate(const Owners::Owner& a_owner)
		{
			Candidate c;
			c.kind = a_owner.kind;
			c.ids = { a_owner.id };
			c.display = a_owner.display;
			if (a_owner.kind == Kind::kPapyrus || a_owner.kind == Kind::kDll) {
				const auto dot = a_owner.display.find_last_of('.');
				c.stem = Owners::Lower(dot == std::string::npos ? a_owner.display : a_owner.display.substr(0, dot));
			}
			return c;
		}

		// g_lock held
		std::vector<Candidate> GatherCandidates(std::uint32_t a_code, bool a_gameplay)
		{
			std::vector<Candidate> raw;
			auto                   add = [&](Candidate a_candidate) {
                if (IsPassive(a_candidate)) {
                    return;
                }
                for (const auto& c : raw) {
                    if (c.ids == a_candidate.ids) {
                        return;
                    }
                }
                raw.push_back(std::move(a_candidate));
			};

			for (const auto& ev : g_captured) {
				if (ev.kind == PapKind::kKeyDown && static_cast<std::uint32_t>(ev.code) == a_code) {
					add(MakeCandidate(Owners::ForHandle(ev.handle)));
				}
			}

			const auto mods = HeldModifiers(a_code);
			for (const auto& dll : Scanner::Dlls()) {
				for (const auto& b : dll.keys) {
					const bool modsHeld = std::all_of(b.modGroups.begin(), b.modGroups.end(),
						[&](std::uint8_t a_group) { return (a_group & mods) != 0; });
					if (b.code == a_code && modsHeld) {
						add(MakeCandidate({ Kind::kDll, "dll:" + dll.stem, dll.display }));
						break;
					}
				}
			}

			if (Settings::includeGameControls && a_gameplay) {
				if (const auto userEvent = GameUserEvent(a_code); !userEvent.empty()) {
					Candidate game;
					game.kind = Kind::kGame;
					game.ids = { "game" };
					game.display = std::format("Skyrim controls ({})", userEvent);
					add(std::move(game));
				}
			}

			// A plugin and a DLL with the same name are the same mod: one competitor
			std::vector<Candidate> merged;
			for (auto& c : raw) {
				auto it = c.stem.empty() ? merged.end() :
				                           std::find_if(merged.begin(), merged.end(), [&](const Candidate& m) { return m.stem == c.stem; });
				if (it == merged.end()) {
					merged.push_back(std::move(c));
					continue;
				}
				for (auto& id : c.ids) {
					if (!HasId(*it, id)) {
						it->ids.push_back(std::move(id));
					}
				}
				const auto dot = it->display.find_last_of('.');
				const auto base = dot == std::string::npos ? it->display : it->display.substr(0, dot);
				const bool hasPlugin = std::any_of(it->ids.begin(), it->ids.end(), [](const std::string& id) { return id.starts_with("pap:"); });
				const bool hasDll = std::any_of(it->ids.begin(), it->ids.end(), [](const std::string& id) { return id.starts_with("dll:"); });
				if (hasPlugin && hasDll) {
					it->display = base + " (plugin + DLL)";
				}
			}
			return merged;
		}

		class PopupCallback final : public RE::IMessageBoxCallback
		{
		public:
			explicit PopupCallback(std::uint32_t a_id) :
				_id(a_id)
			{}

			~PopupCallback() override = default;

			void Run(std::uint8_t a_button) override
			{
				Router::OnPopupAnswer(_id, a_button);
			}

		private:
			std::uint32_t _id;
		};

		const char* ModeName(Mode a_mode)
		{
			switch (a_mode) {
			case Mode::kIdle:
				return "idle";
			case Mode::kFree:
				return "pressed (no conflict)";
			case Mode::kPending:
				return "waiting for the window";
			case Mode::kAnswered:
				return "picked, not yet delivered";
			case Mode::kRouted:
				return "routed to the picked mod";
			default:
				return "?";
			}
		}

		std::string Names(const std::vector<Candidate>& a_candidates)
		{
			std::string names;
			for (const auto& c : a_candidates) {
				names += (names.empty() ? "" : ", ") + c.display;
			}
			return names.empty() ? "nobody" : names;
		}

		std::string Shorten(const std::string& a_text, std::size_t a_max)
		{
			return a_text.size() <= a_max ? a_text : a_text.substr(0, a_max - 3) + "...";
		}

		void ShowPopup(const Popup& a_popup)
		{
			auto body = std::format(
				"Key Router\n\n{} is assigned to more than one mod.\nWhich one should get this key press?\n\n{}",
				Keys::Name(a_popup.code),
				a_popup.remember ?
					"Remember my choice is ON: the mod you pick now gets this key from now on, without asking.\n"
					"(To be asked again, delete its line in KeyRouter_Choices.ini.)" :
					"This is asked every time unless you turn on Remember my choice before picking.");

			// Buttons: one per mod, then Nobody, then the remember toggle (OnPopupAnswer relies on this order)
			std::vector<std::string> buttons;
			for (const auto& c : a_popup.candidates) {
				buttons.push_back(Shorten(c.display, 48));
			}
			buttons.emplace_back("Nobody");
			buttons.emplace_back(a_popup.remember ? "Remember my choice: ON" : "Remember my choice: OFF");

			const auto id = a_popup.id;
			auto*      task = SKSE::GetTaskInterface();
			if (!task) {
				logger::error("No SKSE task interface; cannot show the window");
				Router::OnPopupAnswer(id, 0);
				return;
			}
			task->AddTask([id, body = std::move(body), buttons = std::move(buttons)]() {
				// Built the way the game builds its own message boxes: a MessageBoxData from the UI
				// message factory, queued with MessageBoxMenu::QueueMessage. (MessageBoxMenu::Create
				// is not in the 1.6.1170 Address Library.)
				auto* factory = RE::MessageDataFactoryManager::GetSingleton();
				auto* creator = factory ? factory->GetCreator<RE::MessageBoxData>(RE::BSFixedString{ RE::MessageBoxData::CLASS_NAME }) : nullptr;
				auto* data = creator ? creator->Create() : nullptr;
				if (!data) {
					logger::error("Could not create the Key Router window; nobody gets this press");
					Router::OnPopupAnswer(id, 0);
					return;
				}
				data->bodyText = body.c_str();
				for (const auto& b : buttons) {
					data->buttonText.push_back(RE::BSString{ b.c_str() });
				}
				data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>{ new PopupCallback(id) };
				data->buttonPressOffset = kButtonOffset;
				data->warningType = 10;
				RE::MessageBoxMenu::QueueMessage(data);
			});
		}

		// g_lock held. A remembered answer for exactly this key and set of mods: route this press
		// right away, the same way as a key still held when the player picks (no window, no replay).
		bool ApplyRemembered(std::uint32_t a_code, const std::vector<Candidate>& a_candidates)
		{
			const auto choice = Choices::Find(a_code, CandidateKeys(a_candidates));
			if (!choice) {
				return false;
			}
			auto& state = g_keys[a_code];
			if (choice->picked == "nobody") {
				state.mode = Mode::kRouted;
				state.candidates = a_candidates;
				state.owners.clear();
				state.synthetic = false;
				SyncPoll(a_code);
				logger::info("{}: remembered choice, nobody gets it ({})", Keys::Name(a_code), Names(a_candidates));
				return true;
			}
			for (const auto& c : a_candidates) {
				if (CandidateKey(c) == choice->picked) {
					state.mode = Mode::kRouted;
					state.candidates = a_candidates;
					state.owners = c.ids;
					state.synthetic = false;
					SyncPoll(a_code);
					logger::info("{}: remembered choice, goes to {}", Keys::Name(a_code), c.display);
					return true;
				}
			}
			logger::warn("{}: remembered choice '{}' is not one of the mods that want it now; asking", Keys::Name(a_code), choice->picked);
			return false;
		}

		// g_lock held
		void OpenConflict(std::uint32_t a_code, RE::ButtonEvent* a_button, std::vector<Candidate> a_candidates, bool a_gameplay)
		{
			auto& state = g_keys[a_code];
			state.mode = Mode::kPending;
			state.candidates = a_candidates;
			state.owners.clear();
			state.synthetic = false;

			Popup popup;
			popup.id = ++g_popupSeq;
			popup.code = a_code;
			popup.candidates = std::move(a_candidates);
			popup.userEvent = a_button->QUserEvent();
			popup.fromGameplay = a_gameplay;
			popup.createdAt = NowMs();
			for (const auto& ev : g_captured) {
				if (ev.kind == PapKind::kKeyDown && static_cast<std::uint32_t>(ev.code) == a_code) {
					popup.papDowns.push_back(ev);
				}
			}

			std::string names;
			for (const auto& c : popup.candidates) {
				names += (names.empty() ? "" : ", ") + c.display;
			}
			logger::info("Conflict on {}: {}", Keys::Name(a_code), names);

			g_popup = std::move(popup);
			SyncPoll(a_code);
			UpdateWork();
			StartTicker();
			ShowPopup(*g_popup);
		}

		RE::ButtonEvent* Synthetic(std::uint32_t a_code, bool a_down, const RE::BSFixedString& a_userEvent)
		{
			static RE::ButtonEvent* event = nullptr;
			if (!event) {
				event = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, RE::BSFixedString{}, 0, 0.0F, 0.0F);
				if (!event) {
					return nullptr;
				}
			}
			const bool mouse = a_code >= Keys::kMouseBase;
			event->device = mouse ? RE::INPUT_DEVICE::kMouse : RE::INPUT_DEVICE::kKeyboard;
			event->SetIDCode(mouse ? a_code - Keys::kMouseBase : a_code);
			event->SetUserEvent(a_userEvent);
			event->GetRuntimeData().value = a_down ? 1.0F : 0.0F;
			event->GetRuntimeData().heldDownSecs = a_down ? 0.0F : std::max(0.05F, static_cast<float>(Settings::syntheticPressMs) / 1000.0F);
			event->next = nullptr;
			return event;
		}

		// g_lock held. The picked mod now owns the key.
		void StartRouting(const Delivery& a_delivery, bool a_synthetic)
		{
			auto& state = g_keys[a_delivery.code];
			state.mode = Mode::kRouted;
			state.owners = a_delivery.who.ids;
			state.synthetic = a_synthetic;
			SyncPoll(a_delivery.code);
		}

		// g_lock held. After the press (down) went out, in a frame or directly.
		void AfterDown(std::uint64_t a_now)
		{
			auto& d = *g_delivery;
			if (Keys::IsDownNow(d.code)) {
				// still held: the player's own release goes to the picked mod
				logger::info("{}: press delivered to {}; your release goes to it too", Keys::Name(d.code), d.who.display);
				g_keys[d.code].synthetic = false;
				g_physical[d.code] = true;
				SyncPoll(d.code);
				g_delivery.reset();
			} else {
				logger::info("{}: press delivered to {}", Keys::Name(d.code), d.who.display);
				d.phase = Phase::kHold;
				d.phaseAt = a_now;
			}
		}

		// Straight to the picked mod, outside an input frame. Used only when no frame came along.
		// Called without g_lock: the targets are worked out under the lock, then called without it.
		void DeliverDirect(const Delivery& a_delivery, bool a_down)
		{
			auto* manager = RE::BSInputDeviceManager::GetSingleton();
			if (!manager) {
				return;
			}
			Source* source = manager;

			std::vector<PapEvent> papyrus;
			std::vector<Sink*>    targets;
			std::vector<Sink*>    skse;
			{
				std::scoped_lock lock(g_lock);
				static const RE::BSFixedString onKeyUp{ "OnKeyUp" };
				for (const auto& ev : a_delivery.papDowns) {
					if (!HasId(a_delivery.who, Owners::ForHandle(ev.handle).id)) {
						continue;
					}
					PapEvent copy = ev;
					if (!a_down) {
						copy.kind = PapKind::kKeyUp;
						copy.name = onKeyUp;
						copy.hold = static_cast<float>(Settings::syntheticPressMs) / 1000.0F;
					}
					papyrus.push_back(std::move(copy));
				}
				for (auto* sink : SnapshotSinks(source)) {
					const auto& owner = Owners::ForSink(sink);
					if (owner.kind == Kind::kSkse) {
						skse.push_back(sink);
					} else if (HasId(a_delivery.who, owner.id)) {
						targets.push_back(sink);
					}
				}
			}

			Diag::TraceState(a_down ? "replayed press" : "replayed release", a_delivery.code, "direct");
			logger::info("[diag] replay goes to {}", a_delivery.who.display);
			for (const auto& ev : papyrus) {
				SendPap(ev);
			}
			for (auto* sink : targets) {
				RE::InputEvent* head = Synthetic(a_delivery.code, a_down, a_delivery.userEvent);
				const auto      result = head ? sink->ProcessEvent(&head, source) : Notify::kContinue;
				Diag::TraceListener(Owners::ForSink(sink).display, head != nullptr, static_cast<int>(result));
				if (result == Notify::kStop) {
					break;
				}
			}
			if (HasId(a_delivery.who, "game") && !skse.empty()) {
				// Scripts listening to the game control (OnControlDown/Up) follow the game
				// (g_captured may belong to a frame another thread is sending: keep it aside)
				std::scoped_lock lock(g_lock);
				auto             frameCaptured = std::move(g_captured);
				RE::InputEvent*  head = Synthetic(a_delivery.code, a_down, a_delivery.userEvent);
				CaptureSkse(skse, &head, source);
				for (const auto& ev : g_captured) {
					if (ev.kind == PapKind::kControlDown || ev.kind == PapKind::kControlUp) {
						SendPap(ev);
					}
				}
				g_captured = std::move(frameCaptured);
			}
		}

		void Hook_Input(Source* a_source, RE::InputEvent* const* a_events)
		{
			// The send function is only called from the input poll, but only BSInputDeviceManager's own
			// frames are Key Router's business
			if (a_source != static_cast<Source*>(RE::BSInputDeviceManager::GetSingleton())) {
				g_originalInput(a_source, a_events);
				return;
			}
			Diag::CountHookCall(!a_events || !*a_events);
			Diag::HookEnter();
			struct LeaveGuard
			{
				~LeaveGuard() { Diag::HookLeave(); }
			} leaveGuard;
			Diag::HookStep("waiting for Key Router lock", "");
			std::unique_lock lock(g_lock);
			Diag::HookStep("got Key Router lock", "");
			if (g_inDispatch || !a_source || !a_events) {
				lock.unlock();
				g_originalInput(a_source, a_events);
				return;
			}

			// A picked press (or its release) waiting for a frame rides along in this one
			RE::InputEvent* head = *a_events;
			bool            injectedDown = false;
			bool            injectedUp = false;
			if (g_delivery && (g_delivery->phase == Phase::kSendDown || g_delivery->phase == Phase::kSendUp)) {
				const bool down = g_delivery->phase == Phase::kSendDown;
				if (auto* ev = Synthetic(g_delivery->code, down, g_delivery->userEvent)) {
					ev->next = head;
					head = ev;
					injectedDown = down;
					injectedUp = !down;
					if (down) {
						StartRouting(*g_delivery, true);
					}
					Diag::TraceState(down ? "replayed press" : "replayed release", g_delivery->code, "in frame");
					logger::info("[diag] replay goes to {}", g_delivery->who.display);
				}
			}

			if (!head) {
				lock.unlock();
				g_originalInput(a_source, a_events);
				return;
			}
			g_inDispatch = true;
			RE::InputEvent* const* events = &head;

			std::vector<RE::InputEvent*>      nodes;
			std::unordered_set<std::uint32_t> inFrame;
			for (auto* e = head; e && nodes.size() < kMaxChain; e = e->next) {
				nodes.push_back(e);
			}
			const std::size_t firstReal = (injectedDown || injectedUp) ? 1 : 0;

			g_frameBusy = g_popup.has_value() || (g_delivery && g_delivery->phase == Phase::kWaitClose);

			for (std::size_t i = firstReal; i < nodes.size(); ++i) {
				if (const auto code = CodeOf(nodes[i])) {
					g_physical[*code] = nodes[i]->AsButtonEvent()->IsPressed();
					inFrame.insert(*code);
				}
			}
			ReconcileReleases(inFrame);

			// 1. Papyrus: learn which scripts want which key this frame
			Diag::HookStep("reading listener list", "");
			const auto sinks = SnapshotSinks(a_source);
			Diag::HookStep("SKSE capture pass", "");
			CaptureSkse(sinks, events, a_source);
			Diag::HookStep("checking new presses", "");

			// 2. New presses: who wants them?
			// An SKSE Menu Framework window (its menu, the keyboard map) counts as a menu
			const bool gameplay = InGameplay() && !Menu::BlockingWindowOpen();
			for (std::size_t i = firstReal; i < nodes.size(); ++i) {
				const auto code = CodeOf(nodes[i]);
				if (!code) {
					continue;
				}
				auto* button = nodes[i]->AsButtonEvent();
				if (!button->IsDown()) {
					continue;
				}
				auto& state = g_keys[*code];
				if (state.mode != Mode::kIdle) {
					if (Settings::logKeys) {
						logger::info("[keys] {} pressed while still marked {}: no check", Keys::Name(*code), ModeName(state.mode));
					}
					continue;
				}
				if (g_popup || g_delivery || (!gameplay && !Settings::checkInMenus)) {
					if (Settings::logKeys) {
						logger::info("[keys] {} pressed {}: not checked, everyone gets it", Keys::Name(*code),
							g_popup ? "while a Key Router window is open" : g_delivery ? "while a pick is being delivered" : "in a menu");
					}
					state.mode = Mode::kFree;
					continue;
				}
				auto candidates = GatherCandidates(*code, gameplay);
				if (Settings::logKeys) {
					std::string scripts;
					for (const auto& ev : g_captured) {
						if (ev.kind == PapKind::kKeyDown && static_cast<std::uint32_t>(ev.code) == *code) {
							scripts += (scripts.empty() ? "" : ", ") + Owners::ForHandle(ev.handle).display;
						}
					}
					const auto userEvent = GameUserEvent(*code);
					logger::info("[keys] {} pressed: wanted by {} (Papyrus: {}; game control: {})", Keys::Name(*code), Names(candidates),
						scripts.empty() ? "none" : scripts, userEvent.empty() ? "none" : userEvent);
				}
				if (candidates.size() < 2) {
					state.mode = Mode::kFree;
					continue;
				}
				if (ApplyRemembered(*code, candidates)) {
					continue;
				}
				OpenConflict(*code, button, std::move(candidates), gameplay);
			}

			// 3. Send the frame, each listener behind a filtering proxy
			g_papReleased = false;
			g_removedSinks.clear();
			g_traceFrame = injectedDown || injectedUp;
			g_traceCode = g_delivery ? g_delivery->code : 0;
			{
				RE::BSSpinLockGuard sinkLock(a_source->lock);
				for (auto& sink : a_source->sinks) {
					if (!sink || g_proxies.contains(sink)) {
						continue;
					}
					auto& proxy = g_proxyOf[sink];
					if (!proxy) {
						proxy = new ProxySink(sink);
						g_proxies.insert(proxy);
					}
					sink = proxy;
				}
			}

			Diag::HookStep("sending the frame", "");
			lock.unlock();
			g_originalInput(a_source, events);
			Diag::HookStep("frame sent, waiting for Key Router lock", "");
			lock.lock();
			Diag::HookStep("restoring listener list", "");

			{
				RE::BSSpinLockGuard       sinkLock(a_source->lock);
				auto&                     array = a_source->sinks;
				std::unordered_set<Sink*> seen;
				for (std::uint32_t i = 0; i < array.size();) {
					Sink* sink = Unwrap(array[i]);
					if (sink && (g_removedSinks.contains(sink) || !seen.insert(sink).second)) {
						array.erase(array.begin() + i);
						continue;
					}
					array[i] = sink;
					++i;
				}
			}
			if (head != *a_events) {
				head->next = nullptr;  // unhook our event from the game's chain
			}
			g_traceFrame = false;

			// 4. Released keys end their press
			for (std::size_t i = firstReal; i < nodes.size(); ++i) {
				const auto code = CodeOf(nodes[i]);
				if (!code || nodes[i]->AsButtonEvent()->IsPressed()) {
					continue;
				}
				const auto mode = g_keys[*code].mode;
				if (mode == Mode::kFree || (mode == Mode::kRouted && !g_keys[*code].synthetic)) {
					ResetKey(*code);
				}
			}

			// 5. The delivery this frame carried
			if (g_delivery && (injectedDown || injectedUp)) {
				const auto now = NowMs();
				if (injectedDown) {
					AfterDown(now);
				} else {
					logger::info("{}: release delivered to {}", Keys::Name(g_delivery->code), g_delivery->who.display);
					ResetKey(g_delivery->code);
					g_delivery.reset();
				}
			}

			g_captured.clear();
			UpdateWork();
			g_inDispatch = false;
		}

		void Tick()
		{
			std::unique_lock lock(g_lock);
			const auto       now = NowMs();
			auto*            ui = RE::UI::GetSingleton();
			const bool       boxOpen = ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);

			if (g_popup) {
				auto& popup = *g_popup;
				if (boxOpen) {
					popup.seenOpen = true;
					popup.lastSeenOpen = now;
				} else if ((popup.seenOpen && now - popup.lastSeenOpen > 1000) || (!popup.seenOpen && now - popup.createdAt > 15000)) {
					const auto id = popup.id;
					logger::warn("{}: the Key Router window closed without an answer; nobody gets this press", Keys::Name(popup.code));
					lock.unlock();
					OnPopupAnswer(id, 0);
					lock.lock();
				}
			}

			if (!g_inDispatch) {
				ReconcileReleases({});
			}

			std::optional<std::pair<Delivery, bool>> direct;  // delivery to send outside the lock, and down/up
			if (g_delivery) {
				auto& d = *g_delivery;
				switch (d.phase) {
				case Phase::kWaitClose:
					{
						const bool waitedLong = now - d.answeredAt > 2000;
						const bool ready = !boxOpen && (!d.fromGameplay || InGameplay());
						if (ready || waitedLong) {
							d.phase = Phase::kSendDown;
							d.phaseAt = now;
						}
						break;
					}
				case Phase::kHold:
					if (now - d.phaseAt >= Settings::syntheticPressMs) {
						d.phase = Phase::kSendUp;
						d.phaseAt = now;
					}
					break;
				case Phase::kSendDown:
				case Phase::kSendUp:
					if (now - d.phaseAt > kFallbackMs) {
						const bool down = d.phase == Phase::kSendDown;
						logger::info("{}: no input frame came along; sending the {} straight to {}",
							Keys::Name(d.code), down ? "press" : "release", d.who.display);
						if (down) {
							StartRouting(d, true);
						}
						direct = std::make_pair(d, down);
						d.phase = Phase::kDirect;  // the input frame must not send it too
						d.phaseAt = now;
					}
					break;
				case Phase::kDirect:
					break;
				}
			}

			if (direct) {
				lock.unlock();
				DeliverDirect(direct->first, direct->second);
				lock.lock();
				if (g_delivery && g_delivery->code == direct->first.code) {
					if (direct->second) {
						AfterDown(now);
					} else {
						logger::info("{}: release delivered to {}", Keys::Name(g_delivery->code), g_delivery->who.display);
						ResetKey(g_delivery->code);
						g_delivery.reset();
					}
				}
			}

			UpdateWork();
		}
	}

	void OnPopupAnswer(std::uint32_t a_popupId, std::uint8_t a_button)
	{
		std::scoped_lock lock(g_lock);
		if (!g_popup || g_popup->id != a_popupId) {
			logger::info("Answer for an old Key Router window ignored");
			return;
		}
		const int index = static_cast<int>(a_button) - kButtonOffset;
		const int count = static_cast<int>(g_popup->candidates.size());

		// The remember toggle: flip it and show the same window again
		if (index == count + 1) {
			g_popup->remember = !g_popup->remember;
			g_popup->id = ++g_popupSeq;
			g_popup->createdAt = NowMs();
			g_popup->seenOpen = false;
			logger::info("{}: Remember my choice turned {}", Keys::Name(g_popup->code), g_popup->remember ? "on" : "off");
			ShowPopup(*g_popup);
			UpdateWork();
			return;
		}

		Popup popup = std::move(*g_popup);
		g_popup.reset();

		auto& state = g_keys[popup.code];
		if (popup.remember && index >= 0 && index <= count) {
			const auto picked = index == count ? std::string("nobody") : CandidateKey(popup.candidates[static_cast<std::size_t>(index)]);
			const auto shown = index == count ? std::string("Nobody") : popup.candidates[static_cast<std::size_t>(index)].display;
			Choices::Remember(popup.code, CandidateKeys(popup.candidates), picked,
				std::format("{}: {} -> {}", Keys::Name(popup.code), Names(popup.candidates), shown));
		}
		if (index < 0 || index >= count) {
			logger::info("{}: player chose nobody", Keys::Name(popup.code));
			if (Keys::IsDownNow(popup.code)) {
				state.mode = Mode::kRouted;  // no owners: no competitor gets the rest of this press
				state.owners.clear();
				state.synthetic = false;
				SyncPoll(popup.code);
			} else {
				ResetKey(popup.code);
			}
			UpdateWork();
			return;
		}

		Delivery delivery;
		delivery.code = popup.code;
		delivery.who = popup.candidates[static_cast<std::size_t>(index)];
		delivery.papDowns = std::move(popup.papDowns);
		delivery.userEvent = popup.userEvent;
		delivery.fromGameplay = popup.fromGameplay;
		delivery.answeredAt = NowMs();
		logger::info("{}: player picked {}", Keys::Name(popup.code), delivery.who.display);

		state.mode = Mode::kAnswered;
		g_delivery = std::move(delivery);
		SyncPoll(popup.code);
		UpdateWork();
	}

	namespace
	{
		HowTo DllHowTo(const Scanner::Binding& a_binding)
		{
			HowTo how;
			how.code = a_binding.code;
			how.modGroups = a_binding.modGroups;
			if (!a_binding.origin) {
				how.text = "Set in KeyRouter.ini [DllKeys] (Key Router's own correction)";
				return how;
			}
			const auto& o = *a_binding.origin;
			how.origin = o;
			how.text = o.Movable() ? std::format("{} in {}", o.setting, o.FileName()) :
			                         std::format("{} in {} (written as a name: change it by hand)", o.setting, o.FileName());
			return how;
		}

		// True when the plugin has a quest with a script that extends SKI_ConfigBase (a SkyUI MCM
		// menu; MCM Helper menus extend it too). Main thread. Cached per plugin.
		//
		// Uses VirtualMachine::attachedScripts (+0x93A8, lock +0x93A0). Checked in the decrypted
		// 1.6.1170 image: the game takes the lock at +0x93A0, looks the handle up in the map at
		// +0x93A8 and walks a small array of pointers with flag bit 0 (and rcx, ~1).
		bool PluginHasMcm(const std::string& a_pluginLower)
		{
			static std::unordered_map<std::string, bool> cache;
			if (const auto it = cache.find(a_pluginLower); it != cache.end()) {
				return it->second;
			}
			bool  found = false;
			auto* data = RE::TESDataHandler::GetSingleton();
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (data && vm && policy) {
				for (auto* quest : data->GetFormArray<RE::TESQuest>()) {
					if (found) {
						break;
					}
					auto* file = quest ? quest->GetFile(0) : nullptr;
					if (!file || Owners::Lower(std::string(file->GetFilename())) != a_pluginLower) {
						continue;
					}
					const auto handle = policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
					if (handle == policy->EmptyHandle()) {
						continue;
					}
					RE::BSSpinLockGuard lock(vm->attachedScriptsLock);
					const auto          it = vm->attachedScripts.find(handle);
					if (it == vm->attachedScripts.end()) {
						continue;
					}
					for (const auto& script : it->second) {
						auto* object = script.get();
						for (auto* type = object ? object->GetTypeInfo() : nullptr; type; type = type->GetParent()) {
							if (const char* name = type->GetName(); name && _stricmp(name, "SKI_ConfigBase") == 0) {
								found = true;
								break;
							}
						}
						if (found) {
							break;
						}
					}
				}
			}
			cache.emplace(a_pluginLower, found);
			return found;
		}
	}

	std::vector<std::pair<std::uint32_t, KeyEntry>> BuildKeyMap()
	{
		std::vector<std::pair<std::uint32_t, KeyEntry>> out;

		auto* manager = RE::BSInputDeviceManager::GetSingleton();
		if (!manager) {
			return out;
		}
		Source* source = manager;

		static RE::ButtonEvent* probe = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, RE::BSFixedString{}, 0, 1.0F, 0.0F);

		std::scoped_lock lock(g_lock);
		// g_captured may belong to a frame being sent on the input thread: keep it aside
		auto       frameCaptured = std::move(g_captured);
		const auto sinks = SnapshotSinks(source);

		for (std::uint32_t code = 1; code <= Keys::kMouseLast; ++code) {
			if (!Keys::IsValidCode(code)) {
				continue;
			}
			KeyEntry entry;

			// Papyrus: which scripts would get OnKeyDown for this key
			std::vector<Candidate> plain;
			// Without the SendEvent hook the probe would really send OnKeyDown to the scripts
			if (probe && g_originalSendEvent) {
				const bool mouse = code >= Keys::kMouseBase;
				probe->device = mouse ? RE::INPUT_DEVICE::kMouse : RE::INPUT_DEVICE::kKeyboard;
				probe->SetIDCode(mouse ? code - Keys::kMouseBase : code);
				probe->SetUserEvent(RE::BSFixedString{});
				probe->GetRuntimeData().value = 1.0F;
				probe->GetRuntimeData().heldDownSecs = 0.0F;
				probe->next = nullptr;
				RE::InputEvent* head = probe;
				CaptureSkse(sinks, &head, source);
				for (const auto& ev : g_captured) {
					if (ev.kind == PapKind::kKeyDown && static_cast<std::uint32_t>(ev.code) == code) {
						auto c = MakeCandidate(Owners::ForHandle(ev.handle));
						if (std::none_of(plain.begin(), plain.end(), [&](const Candidate& x) { return x.ids == c.ids; })) {
							plain.push_back(std::move(c));
						}
					}
				}
				g_captured.clear();
			}

			// DLL mods, from their config files
			std::unordered_map<std::string, std::vector<HowTo>> dllHow;  // "dll:<stem>" -> plain settings
			for (const auto& dll : Scanner::Dlls()) {
				for (const auto& b : dll.keys) {
					if (b.code != code) {
						continue;
					}
					HowTo how = DllHowTo(b);
					if (b.modGroups.empty()) {
						auto c = MakeCandidate({ Kind::kDll, "dll:" + dll.stem, dll.display });
						dllHow["dll:" + dll.stem].push_back(std::move(how));
						if (std::none_of(plain.begin(), plain.end(), [&](const Candidate& x) { return x.ids == c.ids; })) {
							plain.push_back(std::move(c));
						}
					} else {
						KeyBind kb;
						kb.name = dll.display;
						kb.source = b.from;
						kb.combo = Keys::ComboName(b.code, b.modGroups);
						kb.howTo.push_back(std::move(how));
						entry.combos.push_back(std::move(kb));
					}
				}
			}

			// Skyrim's own controls during play
			std::string userEvent;
			if (Settings::includeGameControls) {
				userEvent = GameUserEvent(code);
			}

			// A plugin and a DLL with the same name are one mod (same merge as a real conflict)
			std::vector<Candidate> merged;
			for (auto& c : plain) {
				auto it = c.stem.empty() ? merged.end() :
				                           std::find_if(merged.begin(), merged.end(), [&](const Candidate& m) { return m.stem == c.stem; });
				if (it == merged.end()) {
					merged.push_back(std::move(c));
					continue;
				}
				for (auto& id : c.ids) {
					if (!HasId(*it, id)) {
						it->ids.push_back(std::move(id));
					}
				}
				const auto dot = it->display.find_last_of('.');
				it->display = (dot == std::string::npos ? it->display : it->display.substr(0, dot)) + " (plugin + DLL)";
			}
			if (!userEvent.empty()) {
				Candidate game;
				game.kind = Kind::kGame;
				game.ids = { "game" };
				game.display = std::format("Skyrim controls ({})", userEvent);
				merged.push_back(std::move(game));
			}

			std::vector<Candidate> active;
			for (const auto& c : merged) {
				KeyBind kb;
				kb.name = c.display;
				const bool pap = std::any_of(c.ids.begin(), c.ids.end(), [](const std::string& id) { return id.starts_with("pap:"); });
				const bool dll = std::any_of(c.ids.begin(), c.ids.end(), [](const std::string& id) { return id.starts_with("dll:"); });
				kb.passive = IsPassive(c);
				std::vector<std::string> sources;
				for (const auto& id : c.ids) {
					if (id == "game") {
						sources.emplace_back("game control");
						kb.howTo.push_back({ "Skyrim's own control: change it in the game's Controls settings", std::nullopt, code, {} });
					} else if (id.starts_with("pap:")) {
						sources.emplace_back("Papyrus");
						const auto plugin = id.substr(4);
						kb.howTo.push_back({ PluginHasMcm(plugin) ? "Script: change it in this mod's MCM menu" :
						                                            "Script: no MCM menu found, the key is set in the mod's scripts",
							std::nullopt, code, {} });
					} else if (id.starts_with("dll:")) {
						if (const auto it = dllHow.find(id); it != dllHow.end()) {
							for (const auto& h : it->second) {
								sources.push_back(h.origin ? std::format("{}: {}", h.origin->FileName(), h.origin->setting) : std::string("KeyRouter.ini [DllKeys]"));
								kb.howTo.push_back(h);
							}
						}
					}
				}
				for (const auto& src : sources) {
					kb.source += (kb.source.empty() ? "" : ", ") + src;
				}
				(void)pap;
				(void)dll;
				if (!kb.passive) {
					active.push_back(c);
				}
				entry.plain.push_back(std::move(kb));
			}
			if (active.size() >= 2) {
				if (const auto choice = Choices::Find(code, CandidateKeys(active))) {
					entry.remembered = choice->picked;
					for (const auto& c : active) {
						if (CandidateKey(c) == choice->picked) {
							entry.remembered = c.display;
						}
					}
				}
			}
			out.emplace_back(code, std::move(entry));
		}

		g_captured = std::move(frameCaptured);
		return out;
	}

	bool InstallHooks()
	{
		// The input send function is found by its Anniversary Edition Address Library ID (68655).
		// SE 1.5.x numbers its functions differently: that ID is some other function there, and its
		// first bytes could match by chance. Checked: the ID exists, next to PollInputDevices, in the
		// Address Library of every 1.6.x and 1.7.x version. Only 1.6.1170 is tested in game.
		if (!REL::Module::IsAE()) {
			logger::critical("Key routing needs Skyrim 1.6 (Anniversary Edition) or newer; this game is {}. "
							 "Key routing NOT installed.",
				REL::Module::get().version().string());
			return false;
		}

		SKSE::AllocTrampoline(128);

		// The frame's input events are sent by the game's input-send function (ID 68655, RVA
		// 0xCD9E00 in 1.6.1170), called from BSInputDeviceManager::PollInputDevices (ID 68617)
		// at +0x7B. Key Router used to hook that call site, but Typing Mode (zzzz_TypeMode.dll)
		// also hooks it and, while a menu such as the Key Router window is open, sends the frame
		// along a path that skips the hooks below it: the log showed the game's DirectInput reads
		// going on (about 110 per second) while Key Router's hook got 0 calls.
		// So Key Router now hooks the start of the send function itself. Every path that reaches
		// the game's listeners goes through it.
		//
		// Its first instructions (verified in the decrypted 1.6.1170 image):
		//   48 8B C4        mov rax, rsp
		//   48 89 50 10     mov [rax+10h], rdx
		//   55              push rbp
		// 8 bytes, no RIP-relative addressing, so they can run unchanged from the trampoline.
		REL::Relocation<std::uintptr_t> sendFn{ REL::ID(68655) };
		const auto                      entry = sendFn.address();
		constexpr std::uint8_t          expected[8] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x55 };

		if (std::memcmp(reinterpret_cast<const void*>(entry), expected, sizeof(expected)) != 0) {
			const auto* b = reinterpret_cast<const std::uint8_t*>(entry);
			logger::critical("Input send function does not start as expected ({:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}); "
							 "another mod may have hooked it. Key routing NOT installed.",
				b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
			return false;
		} else {
			// Trampoline: the 8 original bytes, then an absolute jump back to entry + 8
			auto* stub = static_cast<std::uint8_t*>(SKSE::GetTrampoline().allocate(8 + 14));
			std::memcpy(stub, expected, sizeof(expected));
			stub[8] = 0xFF;
			stub[9] = 0x25;
			std::memset(stub + 10, 0, 4);
			const std::uint64_t back = entry + sizeof(expected);
			std::memcpy(stub + 14, &back, sizeof(back));
			g_originalInput = reinterpret_cast<InputFn>(stub);

			SKSE::GetTrampoline().write_branch<5>(entry, Hook_Input);
			logger::info("Input send hook installed at the start of the send function (0x{:X})", entry - REL::Module::get().base());
		}

		// VirtualMachine::SendEvent (IVirtualMachine vfunc 0x24): SKSE sends OnKeyDown etc. through it
		REL::Relocation<std::uintptr_t> vmVtbl{ RE::VTABLE_BSScript__Internal__VirtualMachine[0] };
		g_originalSendEvent = reinterpret_cast<SendEventFn>(vmVtbl.write_vfunc(0x24, Hook_SendEvent));

		logger::info("Input and Papyrus event hooks installed");
		return true;
	}

	bool HooksInstalled()
	{
		return g_originalSendEvent != nullptr;
	}

	void OnDataLoaded()
	{
		StartTicker();
	}

	void Reset()
	{
		std::scoped_lock lock(g_lock);
		for (std::uint32_t code = 0; code < Keys::kMaxCode; ++code) {
			g_keys[code] = KeyState{};
		}
		g_physical.fill(false);
		g_popup.reset();
		g_delivery.reset();
		Poll::ClearAll();
		Owners::ClearHandleCache();
		UpdateWork();
	}
}
