#include "Owners.h"

#include <Windows.h>
#include <Psapi.h>

namespace Owners
{
	namespace
	{
		// Called from the input thread, the main thread and Papyrus; the caches need their own lock.
		// Returned references stay valid: unordered_map never moves its elements.
		std::recursive_mutex                    g_cacheLock;
		std::unordered_map<const void*, Owner>  g_sinkCache;
		std::unordered_map<RE::VMHandle, Owner> g_handleCache;

		std::string Narrow(std::wstring_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const int   size = WideCharToMultiByte(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
			if (size > 0) {
				WideCharToMultiByte(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), out.data(), size, nullptr, nullptr);
			}
			return out;
		}

		std::string ModulePath(HMODULE a_module)
		{
			std::wstring buffer(2048, L'\0');
			const DWORD  length = GetModuleFileNameW(a_module, buffer.data(), static_cast<DWORD>(buffer.size()));
			buffer.resize(length);
			return Narrow(buffer);
		}

		std::string FileName(const std::string& a_path)
		{
			const auto pos = a_path.find_last_of("\\/");
			return pos == std::string::npos ? a_path : a_path.substr(pos + 1);
		}

		std::string Stem(const std::string& a_fileName)
		{
			const auto pos = a_fileName.find_last_of('.');
			return Lower(pos == std::string::npos ? a_fileName : a_fileName.substr(0, pos));
		}

		std::string PluginOf(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			if (const auto* file = a_form->GetFile(0)) {
				return std::string(file->GetFilename());
			}
			return {};
		}
	}

	std::string Lower(std::string a_text)
	{
		for (auto& c : a_text) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return a_text;
	}

	const Owner& ForSink(const void* a_sink)
	{
		static const Owner unknown{ Kind::kUnknown, "unknown", "Unknown listener" };
		if (!a_sink) {
			return unknown;
		}

		const void*      vtbl = *static_cast<const void* const*>(a_sink);
		std::scoped_lock lock(g_cacheLock);
		if (const auto it = g_sinkCache.find(vtbl); it != g_sinkCache.end()) {
			return it->second;
		}

		Owner   owner;
		HMODULE module = nullptr;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(vtbl), &module) &&
			module) {
			const auto file = FileName(ModulePath(module));
			const auto stem = Stem(file);
			if (module == GetModuleHandleW(nullptr)) {
				owner = { Kind::kGame, "game", "Skyrim controls" };
			} else if (stem.starts_with("skse64_")) {
				owner = { Kind::kSkse, "skse", "SKSE (Papyrus key events)" };
			} else {
				owner = { Kind::kDll, "dll:" + stem, file };
			}
		} else {
			owner = { Kind::kUnknown, std::format("unknown:{:X}", reinterpret_cast<std::uintptr_t>(vtbl)), "Unknown listener" };
		}

		logger::info("Input code owner found: {} (vtable 0x{:X})", owner.display, reinterpret_cast<std::uintptr_t>(vtbl));
		return g_sinkCache.emplace(vtbl, std::move(owner)).first->second;
	}

	const Owner& ForHandle(RE::VMHandle a_handle)
	{
		std::scoped_lock lock(g_cacheLock);
		if (const auto it = g_handleCache.find(a_handle); it != g_handleCache.end()) {
			return it->second;
		}

		std::string plugin;
		auto*       vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		auto*       policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
		if (policy) {
			if (policy->HandleIsType(RE::ActiveEffect::VMTYPEID, a_handle)) {
				if (auto* effect = static_cast<RE::ActiveEffect*>(policy->GetObjectForHandle(RE::ActiveEffect::VMTYPEID, a_handle))) {
					plugin = PluginOf(effect->GetBaseObject());
				}
			} else {
				for (const auto type : { RE::BGSRefAlias::VMTYPEID, RE::BGSLocAlias::VMTYPEID, RE::BGSBaseAlias::VMTYPEID }) {
					if (!policy->HandleIsType(type, a_handle)) {
						continue;
					}
					if (auto* alias = static_cast<RE::BGSBaseAlias*>(policy->GetObjectForHandle(type, a_handle))) {
						plugin = PluginOf(alias->owningQuest);
					}
					break;
				}
			}
		}

		if (plugin.empty()) {
			const auto formID = static_cast<RE::FormID>(a_handle & 0xFFFFFFFF);
			if (auto* form = RE::TESForm::LookupByID(formID)) {
				plugin = PluginOf(form);
				if (plugin.empty()) {
					if (auto* ref = form->AsReference()) {
						plugin = PluginOf(ref->GetBaseObject());
					}
				}
			}
		}

		Owner owner;
		if (!plugin.empty()) {
			owner = { Kind::kPapyrus, "pap:" + Lower(plugin), plugin };
		} else {
			owner = { Kind::kPapyrus, std::format("pap:handle:{:X}", a_handle), std::format("Papyrus script (handle 0x{:X})", a_handle) };
		}
		return g_handleCache.emplace(a_handle, std::move(owner)).first->second;
	}

	void ClearHandleCache()
	{
		std::scoped_lock lock(g_cacheLock);
		g_handleCache.clear();
	}

	std::vector<Module> SksePluginModules()
	{
		std::vector<Module>  result;
		std::vector<HMODULE> modules(1024);
		DWORD                needed = 0;
		const HANDLE         process = GetCurrentProcess();
		if (!EnumProcessModules(process, modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) {
			logger::error("EnumProcessModules failed ({})", GetLastError());
			return result;
		}
		if (needed > modules.size() * sizeof(HMODULE)) {
			modules.resize(needed / sizeof(HMODULE));
			if (!EnumProcessModules(process, modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) {
				return result;
			}
		}
		modules.resize(needed / sizeof(HMODULE));

		for (const auto module : modules) {
			auto path = ModulePath(module);
			const auto lower = Lower(path);
			if (lower.find("\\skse\\plugins\\") == std::string::npos || !lower.ends_with(".dll")) {
				continue;
			}
			auto file = FileName(path);
			auto stem = Stem(file);
			result.push_back({ module, std::move(path), std::move(file), std::move(stem) });
		}
		return result;
	}
}
