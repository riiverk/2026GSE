#pragma once

#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// GGD - line based data format for Data/Sim/*.ggd (STORY_SIM.md 15.1)
//
//   # comment (whole line only)
//   [<type> <id>]          block start
//   key = value
//   key += value           append to a list
//
// Files are UTF-8 with BOM. The parser only splits text; it knows nothing
// about NPCs or actions. Strings stay UTF-8 all the way to the screen side.
// -----------------------------------------------------------------------------
namespace Sim
{
	struct GgdEntry
	{
		std::string key;
		std::string value;
		bool        append;
		int         line;
	};

	struct GgdBlock
	{
		std::string           type;
		std::string           id;
		int                   line;
		std::vector<GgdEntry> entries;
	};

	struct GgdFile
	{
		std::string           path;
		std::vector<GgdBlock> blocks;
	};

	// false on I/O or syntax error; *err gets "path:line: message"
	bool ParseGgd(const char* path, GgdFile* out, std::string* err);

	std::string Trim(const std::string& s);

	// "a|b|c" -> {a, b, c}. Empty input gives an empty list.
	std::vector<std::string> SplitList(const std::string& s, char sep);

	// "\n" escape in text values -> real newline
	std::string Unescape(const std::string& s);
}
