#include "Poll.h"

#include "Diag.h"
#include "Keys.h"
#include "Owners.h"
#include "Scanner.h"
#include "Settings.h"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

namespace Poll
{
	namespace
	{
		struct Entry
		{
			std::vector<std::string> blocked;
			std::string              forced;
		};

		enum class Verdict
		{
			kPass,
			kBlock,
			kForce
		};

		std::mutex                               g_lock;
		std::unordered_map<std::uint32_t, Entry> g_table;  // key code -> rule
		std::unordered_map<HMODULE, std::string> g_watched;  // DLL module -> lowercase name
		std::atomic<bool>                        g_active{ false };
		// Deferring a DLL's own hotkeys until the router has seen the press (see Seen in Poll.h)
		constexpr std::uint64_t                                              kDeferMs = 150;
		std::unordered_map<std::string, std::unordered_set<std::uint32_t>> g_dllKeys;  // g_lock; DLL -> its hotkeys
		std::atomic<bool>                                                    g_deferActive{ false };
		std::array<std::atomic<bool>, Keys::kMaxCode>                        g_seen{};
		std::array<std::atomic<std::uint64_t>, Keys::kMaxCode>              g_firstDown{};

		std::uint64_t NowMs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		bool Active()
		{
			return g_active.load(std::memory_order_relaxed) || g_deferActive.load(std::memory_order_relaxed);
		}

		// g_lock held. Should this DLL see its own hotkey as up for now, because the router has not
		// looked at this press yet?
		bool DeferLocked(const std::string& a_dll, std::uint32_t a_code, bool a_realDown)
		{
			if (a_code == 0 || a_code >= Keys::kMaxCode) {
				return false;
			}
			if (!a_realDown) {
				g_firstDown[a_code] = 0;
				return false;
			}
			if (g_seen[a_code].load()) {
				return false;
			}
			const auto it = g_dllKeys.find(a_dll);
			if (it == g_dllKeys.end() || !it->second.contains(a_code)) {
				return false;
			}
			const auto now = NowMs();
			auto       first = g_firstDown[a_code].load();
			if (first == 0) {
				g_firstDown[a_code] = now;
				first = now;
			}
			return now - first < kDeferMs;  // the router normally sees it within one or two frames
		}

		HMODULE                                  g_exe = nullptr;
		HMODULE                                  g_self = nullptr;

		HMODULE ModuleOf(const void* a_address)
		{
			HMODULE module = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCWSTR>(a_address), &module);
			return module;
		}

		// g_lock held
		const std::string* WatchedLocked(HMODULE a_module)
		{
			const auto it = g_watched.find(a_module);
			return it == g_watched.end() ? nullptr : &it->second;
		}

		// g_lock held
		Verdict DecideLocked(const std::string& a_dll, std::uint32_t a_code)
		{
			const auto it = g_table.find(a_code);
			if (it == g_table.end()) {
				return Verdict::kPass;
			}
			if (!it->second.forced.empty() && it->second.forced == a_dll) {
				return Verdict::kForce;
			}
			const auto& blocked = it->second.blocked;
			if (std::find(blocked.begin(), blocked.end(), a_dll) != blocked.end()) {
				return Verdict::kBlock;
			}
			return Verdict::kPass;
		}

		Verdict Decide(const void* a_returnAddress, std::uint32_t a_code, bool a_realDown)
		{
			if (!Active() || a_code == 0) {
				return Verdict::kPass;
			}
			const auto       module = ModuleOf(a_returnAddress);
			std::scoped_lock lock(g_lock);
			const auto*      dll = WatchedLocked(module);
			if (!dll) {
				return Verdict::kPass;
			}
			const auto verdict = DecideLocked(*dll, a_code);
			if (verdict == Verdict::kPass && DeferLocked(*dll, a_code, a_realDown)) {
				return Verdict::kBlock;
			}
			return verdict;
		}

		// ---------------------------------------------------------------- user32 reads

		using GetAsyncKeyState_t = SHORT(WINAPI*)(int);
		using GetKeyState_t = SHORT(WINAPI*)(int);
		using GetKeyboardState_t = BOOL(WINAPI*)(PBYTE);

		GetAsyncKeyState_t g_realGetAsyncKeyState = nullptr;
		GetKeyState_t      g_realGetKeyState = nullptr;
		GetKeyboardState_t g_realGetKeyboardState = nullptr;

		constexpr std::uint16_t kDownBit = 0x8000;

		SHORT WINAPI Hook_GetAsyncKeyState(int a_vk)
		{
			const SHORT result = g_realGetAsyncKeyState(a_vk);
			if (!Active()) {
				return result;
			}
			const bool down = (static_cast<std::uint16_t>(result) & kDownBit) != 0;
			switch (Decide(_ReturnAddress(), Keys::VkToDx(static_cast<std::uint32_t>(a_vk) & 0xFF), down)) {
			case Verdict::kBlock:
				return 0;
			case Verdict::kForce:
				return static_cast<SHORT>(kDownBit);
			default:
				return result;
			}
		}

		SHORT WINAPI Hook_GetKeyState(int a_vk)
		{
			const SHORT result = g_realGetKeyState(a_vk);
			if (!Active()) {
				return result;
			}
			const auto bits = static_cast<std::uint16_t>(result);
			switch (Decide(_ReturnAddress(), Keys::VkToDx(static_cast<std::uint32_t>(a_vk) & 0xFF), (bits & kDownBit) != 0)) {
			case Verdict::kBlock:
				return static_cast<SHORT>(bits & 0x0001);  // keep only the toggle bit
			case Verdict::kForce:
				return static_cast<SHORT>(bits | kDownBit);
			default:
				return result;
			}
		}

		BOOL WINAPI Hook_GetKeyboardState(PBYTE a_state)
		{
			const BOOL ok = g_realGetKeyboardState(a_state);
			if (!ok || !a_state || !Active()) {
				return ok;
			}
			const auto       module = ModuleOf(_ReturnAddress());
			std::scoped_lock lock(g_lock);
			const auto*      dll = WatchedLocked(module);
			if (!dll) {
				return ok;
			}
			for (std::uint32_t vk = 1; vk < 256; ++vk) {
				const auto code = Keys::VkToDx(vk);
				if (!code) {
					continue;
				}
				auto verdict = DecideLocked(*dll, code);
				if (verdict == Verdict::kPass && DeferLocked(*dll, code, (a_state[vk] & 0x80) != 0)) {
					verdict = Verdict::kBlock;
				}
				switch (verdict) {
				case Verdict::kBlock:
					a_state[vk] &= 0x01;
					break;
				case Verdict::kForce:
					a_state[vk] |= 0x80;
					break;
				default:
					break;
				}
			}
			return ok;
		}

		int PatchImport(HMODULE a_module, const char* a_function, void* a_hook)
		{
			auto*      base = reinterpret_cast<std::uint8_t*>(a_module);
			const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
				return 0;
			}
			const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE) {
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
					if (std::strcmp(reinterpret_cast<const char*>(byName->Name), a_function) != 0) {
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

		// ---------------------------------------------------------------- DirectInput reads

		using GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
		using GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
		using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);

		struct DeviceVtable
		{
			void** vtbl{ nullptr };
			void*  getDeviceState{ nullptr };
			void*  getDeviceData{ nullptr };
		};

		struct FactoryVtable
		{
			void** vtbl{ nullptr };
			void*  createDevice{ nullptr };
		};

		std::array<DeviceVtable, 2>  g_devices{};    // wide and ANSI interfaces
		std::array<FactoryVtable, 2> g_factories{};
		std::mutex                   g_keyboardLock;
		std::unordered_set<void*>    g_keyboards;    // keyboard devices made after our hook

		const DeviceVtable* FindDevice(void* a_self)
		{
			const auto vtbl = *static_cast<void***>(a_self);
			for (const auto& d : g_devices) {
				if (d.vtbl == vtbl) {
					return &d;
				}
			}
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(void* a_self, DWORD a_size, LPVOID a_data)
		{
			Diag::CountDirectInputRead();
			const auto* d = FindDevice(a_self);
			if (!d) {
				return DIERR_GENERIC;
			}
			const HRESULT hr = reinterpret_cast<GetDeviceState_t>(d->getDeviceState)(a_self, a_size, a_data);
			if (FAILED(hr) || !a_data || a_size != 256 || !Active()) {
				return hr;
			}
			const auto module = ModuleOf(_ReturnAddress());
			if (module == g_exe || module == g_self) {
				return hr;
			}
			auto*            keys = static_cast<BYTE*>(a_data);
			std::scoped_lock lock(g_lock);
			const auto*      dll = WatchedLocked(module);
			if (!dll) {
				return hr;
			}
			if (const auto own = g_dllKeys.find(*dll); own != g_dllKeys.end()) {
				for (const auto code : own->second) {
					if (code < 256 && DecideLocked(*dll, code) == Verdict::kPass && DeferLocked(*dll, code, (keys[code] & 0x80) != 0)) {
						keys[code] = 0;
					}
				}
			}
			for (const auto& [code, entry] : g_table) {
				if (code >= 256) {
					continue;
				}
				switch (DecideLocked(*dll, code)) {
				case Verdict::kBlock:
					keys[code] = 0;
					break;
				case Verdict::kForce:
					keys[code] = 0x80;
					break;
				default:
					break;
				}
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE Hook_GetDeviceData(void* a_self, DWORD a_objectSize, LPDIDEVICEOBJECTDATA a_data, LPDWORD a_count, DWORD a_flags)
		{
			Diag::CountDirectInputRead();
			const auto* d = FindDevice(a_self);
			if (!d) {
				return DIERR_GENERIC;
			}
			const HRESULT hr = reinterpret_cast<GetDeviceData_t>(d->getDeviceData)(a_self, a_objectSize, a_data, a_count, a_flags);
			if (FAILED(hr) || !a_data || !a_count || a_objectSize < sizeof(DIDEVICEOBJECTDATA) || !g_active.load(std::memory_order_relaxed)) {
				// buffered DirectInput reads are not deferred: they are events, not a polled state
				return hr;
			}
			{
				std::scoped_lock lock(g_keyboardLock);
				if (!g_keyboards.contains(a_self)) {
					return hr;
				}
			}
			const auto module = ModuleOf(_ReturnAddress());
			if (module == g_exe || module == g_self) {
				return hr;
			}
			std::scoped_lock lock(g_lock);
			const auto*      dll = WatchedLocked(module);
			if (!dll) {
				return hr;
			}
			auto* bytes = reinterpret_cast<std::uint8_t*>(a_data);
			DWORD kept = 0;
			for (DWORD i = 0; i < *a_count; ++i) {
				auto* item = reinterpret_cast<DIDEVICEOBJECTDATA*>(bytes + static_cast<std::size_t>(i) * a_objectSize);
				if (item->dwOfs < 256 && DecideLocked(*dll, item->dwOfs) == Verdict::kBlock) {
					continue;
				}
				if (kept != i) {
					std::memmove(bytes + static_cast<std::size_t>(kept) * a_objectSize, item, a_objectSize);
				}
				++kept;
			}
			*a_count = kept;
			return hr;
		}

		HRESULT STDMETHODCALLTYPE Hook_CreateDevice(void* a_self, REFGUID a_guid, void** a_device, LPUNKNOWN a_outer)
		{
			const auto     vtbl = *static_cast<void***>(a_self);
			CreateDevice_t original = nullptr;
			for (const auto& f : g_factories) {
				if (f.vtbl == vtbl) {
					original = reinterpret_cast<CreateDevice_t>(f.createDevice);
				}
			}
			if (!original) {
				return DIERR_GENERIC;
			}
			const HRESULT hr = original(a_self, a_guid, a_device, a_outer);
			if (SUCCEEDED(hr) && a_device && *a_device &&
				(IsEqualGUID(a_guid, GUID_SysKeyboard) || IsEqualGUID(a_guid, GUID_SysKeyboardEm) || IsEqualGUID(a_guid, GUID_SysKeyboardEm2))) {
				std::scoped_lock lock(g_keyboardLock);
				g_keyboards.insert(*a_device);
			}
			return hr;
		}

		// Reads the old slot first so a hook call never sees an empty original
		void PatchSlot(void** a_vtbl, std::size_t a_index, void* a_hook, void** a_original)
		{
			*a_original = a_vtbl[a_index];
			DWORD old = 0;
			if (VirtualProtect(&a_vtbl[a_index], sizeof(void*), PAGE_READWRITE, &old)) {
				a_vtbl[a_index] = a_hook;
				VirtualProtect(&a_vtbl[a_index], sizeof(void*), old, &old);
			}
		}
	}

	void InstallDirectInput()
	{
		g_exe = GetModuleHandleW(nullptr);
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&InstallDirectInput), &g_self);

		if (!Settings::directInputHooks) {
			logger::info("DirectInput hooks are off (DirectInputHooks=0)");
			return;
		}

		const std::array<const IID*, 2> interfaces{ &IID_IDirectInput8W, &IID_IDirectInput8A };
		for (std::size_t i = 0; i < interfaces.size(); ++i) {
			void* factory = nullptr;
			if (FAILED(DirectInput8Create(g_self, DIRECTINPUT_VERSION, *interfaces[i], &factory, nullptr)) || !factory) {
				logger::warn("DirectInput8Create failed for interface {}", i);
				continue;
			}

			void** factoryVtbl = *static_cast<void***>(factory);
			void*  device = nullptr;
			const auto create = reinterpret_cast<CreateDevice_t>(factoryVtbl[3]);
			if (SUCCEEDED(create(factory, GUID_SysKeyboard, &device, nullptr)) && device) {
				void**     deviceVtbl = *static_cast<void***>(device);
				const bool known = std::any_of(g_devices.begin(), g_devices.end(), [&](const DeviceVtable& d) { return d.vtbl == deviceVtbl; });
				if (!known) {
					g_devices[i].vtbl = deviceVtbl;
					PatchSlot(deviceVtbl, 9, reinterpret_cast<void*>(&Hook_GetDeviceState), &g_devices[i].getDeviceState);
					PatchSlot(deviceVtbl, 10, reinterpret_cast<void*>(&Hook_GetDeviceData), &g_devices[i].getDeviceData);
				}
				static_cast<IUnknown*>(device)->Release();
			}

			const bool knownFactory = std::any_of(g_factories.begin(), g_factories.end(), [&](const FactoryVtable& f) { return f.vtbl == factoryVtbl; });
			if (!knownFactory) {
				g_factories[i].vtbl = factoryVtbl;
				PatchSlot(factoryVtbl, 3, reinterpret_cast<void*>(&Hook_CreateDevice), &g_factories[i].createDevice);
			}
			static_cast<IUnknown*>(factory)->Release();
		}
		logger::info("DirectInput keyboard hooks installed");
	}

	void InstallImportHooks(const std::vector<std::string>& a_dllStems)
	{
		if (a_dllStems.empty()) {
			return;
		}

		if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
			g_realGetAsyncKeyState = reinterpret_cast<GetAsyncKeyState_t>(GetProcAddress(user32, "GetAsyncKeyState"));
			g_realGetKeyState = reinterpret_cast<GetKeyState_t>(GetProcAddress(user32, "GetKeyState"));
			g_realGetKeyboardState = reinterpret_cast<GetKeyboardState_t>(GetProcAddress(user32, "GetKeyboardState"));
		}
		const bool canHook = Settings::pollHooks && g_realGetAsyncKeyState && g_realGetKeyState && g_realGetKeyboardState;
		if (Settings::pollHooks && !canHook) {
			logger::error("Could not find the user32 keyboard functions; key-polling hooks not installed");
		}

		for (const auto& module : Owners::SksePluginModules()) {
			if (std::find(a_dllStems.begin(), a_dllStems.end(), module.stem) == a_dllStems.end()) {
				continue;
			}
			const auto handle = static_cast<HMODULE>(module.handle);
			{
				std::scoped_lock lock(g_lock);
				g_watched[handle] = module.stem;
				auto& own = g_dllKeys[module.stem];
				for (const auto& dll : Scanner::Dlls()) {
					if (dll.stem == module.stem) {
						for (const auto& b : dll.keys) {
							own.insert(b.code);
						}
					}
				}
				if (!own.empty()) {
					g_deferActive = true;
				}
			}
			if (!canHook) {
				continue;
			}
			int count = 0;
			count += PatchImport(handle, "GetAsyncKeyState", reinterpret_cast<void*>(&Hook_GetAsyncKeyState));
			count += PatchImport(handle, "GetKeyState", reinterpret_cast<void*>(&Hook_GetKeyState));
			count += PatchImport(handle, "GetKeyboardState", reinterpret_cast<void*>(&Hook_GetKeyboardState));
			logger::info("Watching {}: {} direct keyboard read(s) hooked", module.fileName, count);
		}
	}

	void Set(std::uint32_t a_code, std::vector<std::string> a_blocked, std::string a_forced)
	{
		std::scoped_lock lock(g_lock);
		g_table[a_code] = Entry{ std::move(a_blocked), std::move(a_forced) };
		g_active = true;
	}

	void Clear(std::uint32_t a_code)
	{
		std::scoped_lock lock(g_lock);
		g_table.erase(a_code);
		g_active = !g_table.empty();
	}

	void ClearAll()
	{
		std::scoped_lock lock(g_lock);
		g_table.clear();
		g_active = false;
		for (auto& s : g_seen) {
			s = false;
		}
	}

	void Seen(std::uint32_t a_code, bool a_seen)
	{
		if (a_code < Keys::kMaxCode) {
			g_seen[a_code] = a_seen;
		}
	}
}
