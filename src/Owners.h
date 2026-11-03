#pragma once

// Who is listening for a key: the game itself, a Papyrus script (named by its plugin),
// or an SKSE DLL (named by its file).
namespace Owners
{
	enum class Kind
	{
		kUnknown,
		kGame,
		kSkse,
		kDll,
		kPapyrus
	};

	struct Owner
	{
		Kind        kind{ Kind::kUnknown };
		std::string id;       // "game", "skse", "dll:<name>", "pap:<plugin>"
		std::string display;  // shown in the window and the log
	};

	struct Module
	{
		void*       handle{ nullptr };
		std::string path;
		std::string fileName;  // "Foo.dll"
		std::string stem;      // "foo"
	};

	// Owner of an input event sink, from the DLL its vtable lives in (cached per vtable)
	const Owner& ForSink(const void* a_sink);

	// Owner of a Papyrus script object, from the plugin of its form/alias/effect (cached)
	const Owner& ForHandle(RE::VMHandle a_handle);

	void ClearHandleCache();

	// Loaded DLLs from Data\SKSE\Plugins
	std::vector<Module> SksePluginModules();

	std::string Lower(std::string a_text);
}
