#include "SimGgd.h"

#include <cstdio>

namespace Sim
{

std::string Trim(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'
		|| s[e - 1] == '\n')) --e;
	return s.substr(b, e - b);
}

std::vector<std::string> SplitList(const std::string& s, char sep)
{
	std::vector<std::string> out;
	std::string t = Trim(s);
	if (t.empty()) return out;

	size_t start = 0;
	for (;;)
	{
		size_t p = t.find(sep, start);
		out.push_back(Trim(t.substr(start, p == std::string::npos ? std::string::npos
			: p - start)));
		if (p == std::string::npos) break;
		start = p + 1;
	}
	return out;
}

std::string Unescape(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n')
		{
			out += '\n';
			++i;
		}
		else out += s[i];
	}
	return out;
}

static void SetErr(std::string* err, const char* path, int line, const char* msg)
{
	if (!err) return;
	char buf[512];
	snprintf(buf, sizeof(buf), "%s:%d: %s", path, line, msg);
	*err = buf;
}

bool ParseGgd(const char* path, GgdFile* out, std::string* err)
{
	out->path = path;
	out->blocks.clear();

	// SDL checks turn fopen's C4996 into an error on MSVC
	FILE* f = NULL;
#ifdef _MSC_VER
	if (fopen_s(&f, path, "rb") != 0) f = NULL;
#else
	f = fopen(path, "rb");
#endif
	if (!f)
	{
		SetErr(err, path, 0, "cannot open file");
		return false;
	}

	std::string text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
	fclose(f);

	// UTF-8 BOM. Required by the spec, but a missing one is not worth failing
	// the load over: the bytes are the same either way.
	size_t pos = 0;
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF
		&& (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
	{
		pos = 3;
	}

	int line = 0;
	GgdBlock* cur = 0;

	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		std::string raw = text.substr(pos, nl == std::string::npos ? std::string::npos
			: nl - pos);
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
		++line;

		std::string s = Trim(raw);
		if (s.empty() || s[0] == '#') continue;

		if (s[0] == '[')
		{
			size_t close = s.find(']');
			if (close == std::string::npos)
			{
				SetErr(err, path, line, "missing ']'");
				return false;
			}
			std::string head = Trim(s.substr(1, close - 1));
			size_t sp = head.find(' ');
			if (sp == std::string::npos)
			{
				SetErr(err, path, line, "block header needs '[<type> <id>]'");
				return false;
			}
			GgdBlock b;
			b.type = Trim(head.substr(0, sp));
			b.id = Trim(head.substr(sp + 1));
			b.line = line;
			out->blocks.push_back(b);
			cur = &out->blocks.back();
			continue;
		}

		size_t eq = s.find('=');
		if (eq == std::string::npos)
		{
			SetErr(err, path, line, "expected 'key = value'");
			return false;
		}
		if (!cur)
		{
			SetErr(err, path, line, "key outside of a block");
			return false;
		}

		GgdEntry e;
		e.append = (eq > 0 && s[eq - 1] == '+');
		e.key = Trim(s.substr(0, e.append ? eq - 1 : eq));
		e.value = Trim(s.substr(eq + 1));
		e.line = line;
		if (e.key.empty())
		{
			SetErr(err, path, line, "empty key");
			return false;
		}
		cur->entries.push_back(e);
	}
	return true;
}

} // namespace Sim
