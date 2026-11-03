#include "Fix.h"

#include "Keys.h"

namespace Fix
{
	namespace
	{
		namespace fs = std::filesystem;

		std::string Lower(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_s;
		}

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		struct Lines
		{
			std::vector<std::string> lines;  // without '\n'; a '\r' stays on the line
			bool                     endsWithNewline{ true };
		};

		std::optional<Lines> Read(const std::string& a_path, bool a_mayBeMissing)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return a_mayBeMissing ? std::optional<Lines>(Lines{}) : std::nullopt;
			}
			std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			Lines       out;
			out.endsWithNewline = all.empty() || all.back() == '\n';
			std::size_t start = 0;
			while (start < all.size()) {
				const auto nl = all.find('\n', start);
				if (nl == std::string::npos) {
					out.lines.push_back(all.substr(start));
					break;
				}
				out.lines.push_back(all.substr(start, nl - start));
				start = nl + 1;
			}
			return out;
		}

		bool Write(const std::string& a_path, const Lines& a_lines)
		{
			std::error_code ec;
			if (const auto dir = fs::path(a_path).parent_path(); !dir.empty()) {
				fs::create_directories(dir, ec);
			}
			std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
			if (!out) {
				return false;
			}
			for (std::size_t i = 0; i < a_lines.lines.size(); ++i) {
				out << a_lines.lines[i];
				if (i + 1 < a_lines.lines.size() || a_lines.endsWithNewline) {
					out << '\n';
				}
			}
			out.close();
			return static_cast<bool>(out);
		}

		// The new value, written the way the old one was ("38" -> "20", "0x26" -> "0x14")
		std::string Format(const std::string& a_token, std::uint32_t a_value)
		{
			if (a_token.size() > 2 && a_token[0] == '0' && (a_token[1] == 'x' || a_token[1] == 'X')) {
				const auto digits = a_token.substr(2);
				const bool lower = std::any_of(digits.begin(), digits.end(), [](char c) { return c >= 'a' && c <= 'f'; });
				const auto width = std::max<std::size_t>(2, digits.size());
				auto       hex = lower ? std::format("{:0{}x}", a_value, width) : std::format("{:0{}X}", a_value, width);
				return a_token.substr(0, 2) + hex;
			}
			return std::to_string(a_value);
		}

		bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

		// Replaces the last whole-word a_token after the setting name on a_line
		bool ReplaceOnLine(std::string& a_line, const std::string& a_setting, const std::string& a_token, const std::string& a_new)
		{
			const auto lowerLine = Lower(a_line);
			const auto at = lowerLine.find(Lower(a_setting));
			if (at == std::string::npos) {
				return false;
			}
			const auto sep = a_line.find_first_of("=:", at + a_setting.size());
			if (sep == std::string::npos) {
				return false;
			}
			const auto  lowerToken = Lower(a_token);
			std::size_t found = std::string::npos;
			for (auto pos = lowerLine.find(lowerToken, sep + 1); pos != std::string::npos; pos = lowerLine.find(lowerToken, pos + 1)) {
				const bool leftOk = pos == 0 || !IsWordChar(a_line[pos - 1]);
				const auto end = pos + a_token.size();
				const bool rightOk = end >= a_line.size() || !IsWordChar(a_line[end]);
				if (leftOk && rightOk) {
					found = pos;
				}
			}
			if (found == std::string::npos) {
				return false;
			}
			a_line.replace(found, a_token.size(), a_new);
			return true;
		}

		// MCM Helper user settings: set [section] setting=value, adding what is missing
		bool SetIniValue(Lines& a_file, const std::string& a_section, const std::string& a_setting, const std::string& a_value)
		{
			const auto  wantSection = Lower(a_section);
			const auto  wantKey = Lower(a_setting);
			bool        inSection = a_section.empty();
			std::size_t sectionEnd = std::string::npos;  // index after the section's last non-blank line
			bool        sectionFound = a_section.empty();
			for (std::size_t i = 0; i < a_file.lines.size(); ++i) {
				const auto t = Trim(a_file.lines[i]);
				if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
					inSection = Lower(Trim(std::string_view(t).substr(1, t.size() - 2))) == wantSection;
					if (inSection) {
						sectionFound = true;
						sectionEnd = i + 1;
					}
					continue;
				}
				if (!inSection) {
					continue;
				}
				if (!t.empty()) {
					sectionEnd = i + 1;
				}
				const auto eq = t.find('=');
				if (eq != std::string::npos && Lower(Trim(std::string_view(t).substr(0, eq))) == wantKey) {
					const bool cr = !a_file.lines[i].empty() && a_file.lines[i].back() == '\r';
					a_file.lines[i] = Trim(std::string_view(t).substr(0, eq)) + "=" + a_value + (cr ? "\r" : "");
					return true;
				}
			}
			if (!sectionFound) {
				if (!a_file.lines.empty() && !Trim(a_file.lines.back()).empty()) {
					a_file.lines.emplace_back();
				}
				a_file.lines.push_back("[" + a_section + "]");
				a_file.lines.push_back(a_setting + "=" + a_value);
				a_file.endsWithNewline = true;
				return true;
			}
			const auto at = sectionEnd == std::string::npos ? a_file.lines.size() : sectionEnd;
			a_file.lines.insert(a_file.lines.begin() + static_cast<std::ptrdiff_t>(at), a_setting + "=" + a_value);
			return true;
		}
	}

	Result MoveKey(const Scanner::Origin& a_origin, std::uint32_t a_newCode)
	{
		if (!a_origin.Movable()) {
			return { false, "This setting is written as a key name; change it by hand." };
		}
		if (a_newCode == 0 || a_newCode >= Keys::kMouseBase) {
			return { false, "Only keyboard keys can be chosen." };
		}
		std::uint32_t value = a_newCode;
		if (a_origin.vk) {
			const int vk = Keys::ToVirtualKey(a_newCode);
			if (vk <= 0) {
				return { false, std::format("{} has no Windows key code.", Keys::Name(a_newCode)) };
			}
			value = static_cast<std::uint32_t>(vk);
		}
		const auto newText = Format(a_origin.token, value);

		if (a_origin.mcmDefaults) {
			// The defaults file belongs to the mod; the player's choice goes to MCM Helper's user file
			auto file = Read(a_origin.userPath, true);
			if (!file) {
				return { false, std::format("Could not read {}", a_origin.userPath) };
			}
			SetIniValue(*file, a_origin.section, a_origin.setting, newText);
			if (!Write(a_origin.userPath, *file)) {
				return { false, std::format("Could not write {}", a_origin.userPath) };
			}
			logger::info("Moved {} [{}] {} to {} ({} = {}) in {}", a_origin.FileName(), a_origin.section, a_origin.setting,
				Keys::Name(a_newCode), a_origin.setting, newText, a_origin.userPath);
			return { true, std::format("{} set to {} in {}. Restart Skyrim to use it.", a_origin.setting, Keys::Name(a_newCode), a_origin.FileName()), newText };
		}

		auto file = Read(a_origin.path, false);
		if (!file || a_origin.line >= file->lines.size()) {
			return { false, std::format("Could not read {}", a_origin.path) };
		}
		auto&      line = file->lines[a_origin.line];
		const auto before = line;
		if (!ReplaceOnLine(line, a_origin.setting, a_origin.token, newText)) {
			return { false, std::format("{} changed since the game started ({} no longer reads {}). Restart Skyrim and try again.",
								a_origin.FileName(), a_origin.setting, a_origin.token) };
		}
		if (!Write(a_origin.path, *file)) {
			return { false, std::format("Could not write {} (is it open in another program?)", a_origin.path) };
		}
		logger::info("Moved {}: line {} '{}' -> '{}'", a_origin.path, a_origin.line + 1, Trim(before), Trim(line));
		return { true, std::format("{} in {} is now {}. Restart Skyrim to use it.", a_origin.setting, a_origin.FileName(), Keys::Name(a_newCode)), newText };
	}
}
