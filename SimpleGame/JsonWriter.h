#pragma once

#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

// -----------------------------------------------------------------------------
// JsonWriter - one-line JSON builder for the monitor stream (Monitor.h)
//
// One message is always one line. An SSE frame ends at a blank line, so a raw
// newline inside a string would cut the message in half; every control
// character is escaped here instead of trusting the caller.
//
// Keys are ASCII literals. Values are UTF-8 (std::string, as Sim keeps its
// text) or UTF-16 (wchar_t, as the screen code keeps its names) - the wide
// overload converts so callers do not need Platform for it.
//
// Pass key = 0 for array elements.
// -----------------------------------------------------------------------------
class JsonWriter
{
public:
	JsonWriter() { m_S.reserve(8192); }

	void BeginObject(const char* key = 0) { Prefix(key); m_S += '{'; m_First.push_back(true); }
	void EndObject()                      { m_S += '}'; m_First.pop_back(); }
	void BeginArray(const char* key = 0)  { Prefix(key); m_S += '['; m_First.push_back(true); }
	void EndArray()                       { m_S += ']'; m_First.pop_back(); }

	void Null(const char* key)            { Prefix(key); m_S += "null"; }
	void Bool(const char* key, bool v)    { Prefix(key); m_S += v ? "true" : "false"; }

	void Int(const char* key, int v)
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", v);
		Prefix(key);
		m_S += buf;
	}

	void Num(const char* key, double v, int decimals = 2)
	{
		Prefix(key);
		// JSON has no NaN / Infinity; one bad float must not break the parse
		// of the whole snapshot on the browser side.
		if (v != v || std::fabs(v) > 1e300) { m_S += "null"; return; }
		char buf[48];
		snprintf(buf, sizeof(buf), "%.*f", decimals, v);
		m_S += buf;
	}

	void Str(const char* key, const char* v)
	{
		Prefix(key);
		if (!v) { m_S += "null"; return; }
		Quote(v);
	}

	void Str(const char* key, const std::string& v) { Str(key, v.c_str()); }

	void StrW(const char* key, const wchar_t* v)
	{
		Prefix(key);
		if (!v) { m_S += "null"; return; }
		std::string utf8;
		for (const wchar_t* p = v; *p; ++p)
		{
			unsigned int cp = (unsigned int)*p;
			// UTF-16 surrogate pair (wchar_t is 16-bit on Windows)
			if (cp >= 0xD800 && cp <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF)
			{
				cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned int)p[1] - 0xDC00);
				++p;
			}
			AppendUtf8(utf8, cp);
		}
		Quote(utf8.c_str());
	}

	const std::string& Get() const { return m_S; }
	void Clear() { m_S.clear(); m_First.clear(); }

private:
	void Prefix(const char* key)
	{
		if (!m_First.empty())
		{
			if (!m_First.back()) m_S += ',';
			m_First.back() = false;
		}
		if (key)
		{
			Quote(key);
			m_S += ':';
		}
	}

	void Quote(const char* s)
	{
		m_S += '"';
		for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
		{
			unsigned char c = *p;
			if (c == '"')       m_S += "\\\"";
			else if (c == '\\') m_S += "\\\\";
			else if (c == '\n') m_S += "\\n";
			else if (c == '\r') m_S += "\\r";
			else if (c == '\t') m_S += "\\t";
			else if (c < 0x20)
			{
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				m_S += buf;
			}
			else m_S += (char)c;   // UTF-8 bytes pass through
		}
		m_S += '"';
	}

	static void AppendUtf8(std::string& out, unsigned int cp)
	{
		if (cp < 0x80)
		{
			out += (char)cp;
		}
		else if (cp < 0x800)
		{
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	std::string       m_S;
	std::vector<bool> m_First;   // per open container: nothing written yet
};
