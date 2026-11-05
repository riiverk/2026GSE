#version 330

// Continuous terrain.
//
// The map is still a grid of tile types, but nothing is drawn per tile. Each
// pixel is taken back to world space, the four surrounding tile centres are
// blended with a smoothed bilinear weight, and the result is sharpened into
// regions. Because the lookup position is wobbled by low-frequency noise first,
// the borders between grass, path, water and forest come out as curves instead
// of a staircase of diamonds, and there are no tile edges left to see.
//
// Keep Hash / Noise / the weight math identical to TerrainTypeAt in Level1.cpp:
// the CPU uses the same field to keep grass tufts off the water.

layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Types;     // R8, tile type index / 255, NEAREST
uniform ivec2     u_MapSize;   // tiles
uniform vec2      u_Screen;    // render target size (px)
uniform vec2      u_Offset;    // screen position of world (0,0), before zoom
uniform float     u_Zoom;      // world zoom around the screen centre
uniform vec2      u_Tile;      // tile width, height (px)
uniform float     u_Time;

uniform vec4      u_Colors[8]; // base colour per tile type
uniform int       u_TypeCount;
uniform int       u_WaterType; // -1 if none
uniform float     u_Warp;      // border wobble, in tiles
uniform float     u_Sharp;     // region sharpness (blend exponent)
uniform vec4      u_Haze;      // rgb, strength

// Integer hash: exact and identical on CPU and GPU. sin()-based hashes lose
// precision on the GPU once coordinates grow and start to band.
float Hash(ivec2 p)
{
	uint h = uint(p.x) * 1597334677u ^ uint(p.y) * 3812015801u;
	h = (h ^ (h >> 15)) * 2246822519u;
	h = (h ^ (h >> 13)) * 3266489917u;
	h ^= h >> 16;
	return float(h & 0x00FFFFFFu) / 16777215.0;
}

float Noise(vec2 x)
{
	vec2  i = floor(x);
	vec2  f = x - i;
	f = f * f * (3.0 - 2.0 * f);
	ivec2 c = ivec2(i);
	float a = Hash(c);
	float b = Hash(c + ivec2(1, 0));
	float d = Hash(c + ivec2(0, 1));
	float e = Hash(c + ivec2(1, 1));
	return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}

int TypeAt(ivec2 c)
{
	c = clamp(c, ivec2(0), u_MapSize - ivec2(1));
	return int(texelFetch(u_Types, c, 0).r * 255.0 + 0.5);
}

void main()
{
	// ---- pixel -> world ------------------------------------------------------
	vec2 frag = vec2(gl_FragCoord.x, u_Screen.y - gl_FragCoord.y);   // top-left origin
	vec2 sc   = u_Screen * 0.5;
	vec2 s    = sc + (frag - sc) / u_Zoom;       // undo the world zoom
	vec2 iso  = s - u_Offset;                    // undo the camera
	float a   = iso.x / (u_Tile.x * 0.5);
	float b   = iso.y / (u_Tile.y * 0.5);
	vec2 w    = vec2((a + b) * 0.5, (b - a) * 0.5);   // world, in tiles

	// Soft edge where the map ends, one pixel wide, so the big diamond outline
	// is anti-aliased against the sky.
	vec2  fw   = fwidth(w);
	float edge = min(min(w.x, w.y), min(float(u_MapSize.x) - w.x, float(u_MapSize.y) - w.y));
	float cover = clamp(edge / max(max(fw.x, fw.y), 1e-4), 0.0, 1.0);
	if (cover <= 0.0) discard;

	// ---- which terrain -------------------------------------------------------
	vec2 wp = w + (vec2(Noise(w * 0.9), Noise(w * 0.9 + vec2(17.3, 5.1))) - 0.5) * 2.0 * u_Warp;

	vec2  q  = wp - 0.5;            // tile centres sit on integers
	vec2  i0 = floor(q);
	vec2  f  = q - i0;
	f = f * f * (3.0 - 2.0 * f);   // rounder blobs than plain bilinear
	ivec2 c  = ivec2(i0);

	float wt[8];
	for (int k = 0; k < 8; ++k) wt[k] = 0.0;
	wt[TypeAt(c)]                  += (1.0 - f.x) * (1.0 - f.y);
	wt[TypeAt(c + ivec2(1, 0))]    += f.x * (1.0 - f.y);
	wt[TypeAt(c + ivec2(0, 1))]    += (1.0 - f.x) * f.y;
	wt[TypeAt(c + ivec2(1, 1))]    += f.x * f.y;

	vec3  col   = vec3(0.0);
	float total = 0.0;
	float water = 0.0;
	for (int k = 0; k < 8; ++k)
	{
		if (k >= u_TypeCount) break;
		float s2 = pow(wt[k], u_Sharp);
		col   += s2 * u_Colors[k].rgb;
		total += s2;
		if (k == u_WaterType) water = s2;
	}
	col   /= total;
	water /= total;

	// ---- surface detail ------------------------------------------------------
	// Large-period brightness drift, as the per-tile shading used to do but
	// without tile boundaries, plus a little fine grain so flat areas breathe.
	float n = (Noise(w * 0.23) - 0.5) * 0.20
	        + (Noise(w * 0.75) - 0.5) * 0.07
	        + (Noise(w * 3.10) - 0.5) * 0.035;
	col *= 0.99 + n;

	if (water > 0.01)
	{
		float t = u_Time;
		float sa = sin((w.x * 1.0 + w.y * 0.35) * 9.0 + t * 2.1) * 0.5 + 0.5;
		float sb = sin((w.x * 0.4 - w.y * 1.0) * 11.0 - t * 1.3) * 0.5 + 0.5;
		float sparkle = pow(sa * sb, 4.0);
		float swell   = sin((w.x * 1.5 + w.y * 1.0) + t * 0.9) * 0.5 + 0.5;
		vec3 wet = col + vec3(0.05, 0.07, 0.08) * swell + vec3(0.55, 0.62, 0.60) * sparkle;

		// Shoreline: a thin lighter band where water meets land.
		float shore = smoothstep(0.15, 0.45, water) * (1.0 - smoothstep(0.55, 0.85, water));
		wet = mix(wet, vec3(0.80, 0.86, 0.84), shore * 0.35);

		col = mix(col, wet, smoothstep(0.0, 0.5, water));
	}

	// ---- aerial haze, per pixel (frag.y is already on the zoomed screen) ------
	float ht = clamp(1.0 - frag.y / u_Screen.y, 0.0, 1.0);
	col = mix(col, u_Haze.rgb, ht * ht * u_Haze.w);

	FragColor = vec4(col, cover);
}
