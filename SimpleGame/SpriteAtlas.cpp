// 스프라이트 아틀라스 로더. PCH 를 쓰지 않는다 (stb_image 구현을 품고 있어서).

#include "SpriteAtlas.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

// /sdl 이 C4996 을 오류로 올리므로 stdio 경로(fopen)는 아예 빼고 메모리에서 읽는다
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4146 4244 4245 4456 4457 4701 4996)
#endif
#include "Dependencies/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static bool ReadAll(const std::string& path, std::vector<unsigned char>* out)
{
	std::ifstream f(path.c_str(), std::ios::binary);
	if (!f) return false;
	f.seekg(0, std::ios::end);
	std::streamoff n = f.tellg();
	if (n <= 0) return false;
	f.seekg(0, std::ios::beg);
	out->resize((size_t)n);
	f.read((char*)&(*out)[0], n);
	return (bool)f;
}

static std::string DirOf(const std::string& p)
{
	size_t k = p.find_last_of("/\\");
	return (k == std::string::npos) ? std::string() : p.substr(0, k + 1);
}

bool SpriteAtlas::Load(const char* txtPath)
{
	m_Frames.clear();
	m_Anims.clear();
	m_Statics.clear();
	m_Pixels.clear();
	m_Error.clear();

	std::ifstream f(txtPath);
	if (!f)
	{
		m_Error = std::string("cannot open ") + txtPath;
		return false;
	}

	std::string png;
	int aw = 0, ah = 0;
	float curScale = 1.0f;
	int expect = 0;        // 지금 블록에서 더 읽어야 할 frame 줄 수
	std::string line;
	int lineNo = 0;

	while (std::getline(f, line))
	{
		++lineNo;
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if (line.empty() || line[0] == '#') continue;

		std::istringstream ss(line);
		std::string tag;
		ss >> tag;

		if (tag == "atlas")
		{
			ss >> png >> aw >> ah;
		}
		else if (tag == "anim")
		{
			// anim <name> <dirs> <frames> <fps> <loop> <scale>
			SpriteAnim a;
			int loop = 1;
			ss >> a.name >> a.dirs >> a.frames >> a.fps >> loop >> curScale;
			a.loop = (loop != 0);
			a.first = (int)m_Frames.size();
			m_Anims.push_back(a);
			expect = a.dirs * a.frames;
		}
		else if (tag == "sprite")
		{
			// sprite <name> <scale>
			Named n;
			ss >> n.name >> curScale;
			n.frame = (int)m_Frames.size();
			m_Statics.push_back(n);
			expect = 1;
		}
		else if (tag == "f")
		{
			// f <x> <y> <w> <h> <ox> <oy>   (아틀라스 px, 피벗 기준 좌상단 오프셋)
			if (expect <= 0 || aw <= 0)
			{
				std::ostringstream es;
				es << "line " << lineNo << ": frame without block";
				m_Error = es.str();
				return false;
			}
			float x, y, w, h, ox, oy;
			ss >> x >> y >> w >> h >> ox >> oy;
			SpriteFrame fr;
			fr.u0 = x / aw;
			fr.v0 = y / ah;
			fr.u1 = (x + w) / aw;
			fr.v1 = (y + h) / ah;
			fr.w = w / curScale;
			fr.h = h / curScale;
			fr.ox = ox / curScale;
			fr.oy = oy / curScale;
			m_Frames.push_back(fr);
			--expect;
		}
	}

	if (png.empty() || m_Frames.empty())
	{
		m_Error = "no atlas or frames in sprite list";
		return false;
	}

	std::vector<unsigned char> bytes;
	std::string pngPath = DirOf(txtPath) + png;
	if (!ReadAll(pngPath, &bytes))
	{
		m_Error = "cannot read " + pngPath;
		return false;
	}

	int w = 0, h = 0, comp = 0;
	unsigned char* px = stbi_load_from_memory(&bytes[0], (int)bytes.size(), &w, &h, &comp, 4);
	if (!px)
	{
		m_Error = "PNG decode failed: " + pngPath;
		return false;
	}
	m_W = w;
	m_H = h;
	m_Pixels.assign(px, px + (size_t)w * h * 4);
	stbi_image_free(px);
	return true;
}

int SpriteAtlas::FindAnim(const char* name) const
{
	for (size_t i = 0; i < m_Anims.size(); ++i)
		if (m_Anims[i].name == name) return (int)i;
	return -1;
}

int SpriteAtlas::FindSprite(const char* name) const
{
	for (size_t i = 0; i < m_Statics.size(); ++i)
		if (m_Statics[i].name == name) return m_Statics[i].frame;
	return -1;
}

const SpriteFrame& SpriteAtlas::AnimFrameIndex(int anim, int dir5, int frame) const
{
	const SpriteAnim& a = m_Anims[anim];
	if (a.dirs <= 1) dir5 = 0;
	if (dir5 < 0) dir5 = 0;
	if (dir5 >= a.dirs) dir5 = a.dirs - 1;
	if (frame < 0) frame = 0;
	if (frame >= a.frames) frame = a.frames - 1;
	return m_Frames[a.first + dir5 * a.frames + frame];
}

const SpriteFrame& SpriteAtlas::AnimFrameAt(int anim, int dir5, float t) const
{
	const SpriteAnim& a = m_Anims[anim];
	int k = (int)floorf(t * a.fps);
	if (a.loop)
	{
		k %= a.frames;
		if (k < 0) k += a.frames;
	}
	return AnimFrameIndex(anim, dir5, k);
}

float SpriteAtlas::AnimDuration(int anim) const
{
	const SpriteAnim& a = m_Anims[anim];
	return a.fps > 0.0f ? a.frames / a.fps : 0.0f;
}

// 스프라이트의 방향은 월드 8방향과 같다.
//   s=(+1,+1)  se=(+1,0)  e=(+1,-1)  ne=(0,-1)  n=(-1,-1)
//   nw=(-1,0) -> ne 반전,  w=(-1,+1) -> e 반전,  sw=(0,+1) -> se 반전
int SpriteAtlas::SpriteDirFromWorld(float fx, float fy, bool* flip)
{
	*flip = false;
	if (fx * fx + fy * fy < 1e-8f) return SDIR_S;
	float a = atan2f(fy, fx);                        // 라디안, 월드 기준
	int k = (int)floorf(a / 0.785398163f + 0.5f);    // 45도 단위 반올림, -4..4
	switch (k)
	{
	case 1:  return SDIR_S;
	case 0:  return SDIR_SE;
	case -1: return SDIR_E;
	case -2: return SDIR_NE;
	case -3: return SDIR_N;
	case 4:
	case -4: *flip = true; return SDIR_NE;   // nw
	case 3:  *flip = true; return SDIR_E;    // w
	case 2:  *flip = true; return SDIR_SE;   // sw
	default: return SDIR_S;
	}
}
