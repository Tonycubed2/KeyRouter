#include "Choices.h"

#include "Keys.h"

namespace Choices
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/KeyRouter_Choices.ini";

		struct Entry
		{
			std::uint32_t code{ 0 };
			std::string   set;      // sorted, ", " joined
			std::string   picked;
			std::string   comment;  // without the leading "; "
		};

		std::mutex         g_lock;
		std::vector<Entry> g_entries;

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		std::string Lower(std::string a_text)
		{
			for (auto& c : a_text) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_text;
		}

		std::string Canonical(std::vector<std::string> a_set)
		{
			for (auto& s : a_set) {
				s = Lower(Trim(s));
			}
			std::erase_if(a_set, [](const std::string& s) { return s.empty(); });
			std::sort(a_set.begin(), a_set.end());
			a_set.erase(std::unique(a_set.begin(), a_set.end()), a_set.end());
			std::string out;
			for (const auto& s : a_set) {
				out += (out.empty() ? "" : ", ") + s;
			}
			return out;
		}

		std::vector<std::string> SplitCommas(std::string_view a_text)
		{
			std::vector<std::string> out;
			std::string              cur;
			for (const char c : a_text) {
				if (c == ',') {
					out.push_back(cur);
					cur.clear();
				} else {
					cur.push_back(c);
				}
			}
			out.push_back(cur);
			return out;
		}

		// g_lock held
		void Write()
		{
			std::ofstream out(kPath, std::ios::trunc);
			if (!out) {
				logger::error("Could not write {}", kPath);
				return;
			}
			out << "; Key Router remembered choices.\n"
				   "; Each entry: key code = picked mod | all mods that wanted the key.\n"
				   "; It is used only when exactly those mods want that key again; otherwise the window asks.\n"
				   "; Delete an entry (both lines) to be asked again. \"nobody\" means the key goes to none of them.\n\n"
				   "[Choices]\n";
			for (const auto& e : g_entries) {
				if (!e.comment.empty()) {
					out << "; " << e.comment << "\n";
				}
				out << std::format("0x{:02X} = {} | {}\n", e.code, e.picked, e.set);
			}
			logger::info("Saved {} remembered choice(s) to {}", g_entries.size(), kPath);
		}
	}

	void Load()
	{
		std::scoped_lock lock(g_lock);
		g_entries.clear();
		std::ifstream in(kPath);
		if (!in) {
			logger::info("No remembered choices yet ({} not found)", kPath);
			return;
		}
		std::string line;
		std::string comment;
		bool        inSection = false;
		while (std::getline(in, line)) {
			const auto t = Trim(line);
			if (t.empty()) {
				continue;
			}
			if (t.front() == ';' || t.front() == '#') {
				comment = Trim(std::string_view(t).substr(1));
				continue;
			}
			if (t.front() == '[') {
				inSection = Lower(t) == "[choices]";
				continue;
			}
			if (!inSection) {
				continue;
			}
			const auto eq = t.find('=');
			const auto bar = t.find('|');
			if (eq == std::string::npos || bar == std::string::npos || bar < eq) {
				logger::warn("KeyRouter_Choices.ini: could not read line '{}'", t);
				continue;
			}
			const auto codeText = Trim(std::string_view(t).substr(0, eq));
			const auto code = Keys::ParseToken(codeText, false);
			if (!code || *code >= Keys::kMaxCode) {
				logger::warn("KeyRouter_Choices.ini: unknown key '{}'", codeText);
				continue;
			}
			Entry e;
			e.code = *code;
			e.picked = Lower(Trim(std::string_view(t).substr(eq + 1, bar - eq - 1)));
			e.set = Canonical(SplitCommas(std::string_view(t).substr(bar + 1)));
			e.comment = comment;
			comment.clear();
			if (e.picked.empty() || e.set.empty()) {
				logger::warn("KeyRouter_Choices.ini: incomplete line '{}'", t);
				continue;
			}
			g_entries.push_back(std::move(e));
		}
		for (const auto& e : g_entries) {
			logger::info("Remembered choice: {} -> {} (when {} compete)", Keys::Name(e.code), e.picked, e.set);
		}
	}

	std::optional<Choice> Find(std::uint32_t a_code, const std::vector<std::string>& a_set)
	{
		const auto       set = Canonical(a_set);
		std::scoped_lock lock(g_lock);
		for (const auto& e : g_entries) {
			if (e.code == a_code && e.set == set) {
				return Choice{ e.picked };
			}
		}
		return std::nullopt;
	}

	void Remember(std::uint32_t a_code, const std::vector<std::string>& a_set, const std::string& a_picked, const std::string& a_comment)
	{
		const auto       set = Canonical(a_set);
		std::scoped_lock lock(g_lock);
		std::erase_if(g_entries, [&](const Entry& e) { return e.code == a_code && e.set == set; });
		g_entries.push_back(Entry{ a_code, set, Lower(a_picked), a_comment });
		Write();
	}

	std::vector<Listed> List()
	{
		std::scoped_lock    lock(g_lock);
		std::vector<Listed> out;
		for (const auto& e : g_entries) {
			out.push_back({ e.code, e.set, e.picked, e.comment });
		}
		return out;
	}

	void Forget(std::uint32_t a_code, const std::string& a_set)
	{
		std::scoped_lock lock(g_lock);
		const auto       before = g_entries.size();
		if (a_code == 0 && a_set.empty()) {
			g_entries.clear();
		} else {
			std::erase_if(g_entries, [&](const Entry& e) { return e.code == a_code && e.set == a_set; });
		}
		if (g_entries.size() != before) {
			logger::info("Forgot {} remembered choice(s)", before - g_entries.size());
			Write();
		}
	}
}
