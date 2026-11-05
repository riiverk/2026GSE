#version 330

// Signed-distance shapes.
//
// Shape ids:
//   0 rounded rect   1 ellipse   2 diamond   3 triangle
//   4 text glyph (atlas)  5 water tile        6 flame
//   7 bowl (lower half of an ellipse, flat edge on top)
//   8 sprite (RGBA atlas baked in Blender, straight alpha)
//
// Everything that used to be a uniform is now per-instance data handed over
// by the vertex shader as flat varyings.

in vec2 v_Local;
in vec2 v_UV;
in vec2 v_TexUV;

flat in vec4  v_ColorA;   // colour at the top edge
flat in vec4  v_ColorB;   // colour at the bottom edge
flat in vec4  v_Params;   // x: shape  y: corner radius (0 .. 0.5)  z: softness
flat in vec4  v_Extra;    // x: time   y: intensity   z: rim light
flat in float v_Haze;
flat in float v_Alpha;

layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Tex;    // glyph atlas, single channel
uniform sampler2D u_Sprite; // sprite atlas, RGBA
uniform vec4      u_Haze;   // rgb, strength

float sdRoundRect(vec2 p, vec2 half_, float r)
{
	vec2 d = abs(p) - half_ + vec2(r);
	return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

float sdEllipse(vec2 p)
{
	return length(p) - 0.5;
}

float sdDiamond(vec2 p)
{
	return abs(p.x) + abs(p.y) - 0.5;
}

float sdTriangle(vec2 p)
{
	vec2 apex = vec2(0.0, -0.5);
	vec2 nL = normalize(vec2(-1.0, -0.5));
	vec2 nR = normalize(vec2( 1.0, -0.5));
	return max(max(dot(p - apex, nL), dot(p - apex, nR)), p.y - 0.5);
}

// Lower half of an ellipse whose flat edge is the top of the quad. A bowl seen
// from above: the rim opening is drawn separately on top of it, so the upper
// half of the ellipse must not exist at all or the bowl reads as a ball.
float sdBowl(vec2 p)
{
	vec2 q = vec2(p.x / 0.5, p.y + 0.5);
	return max((length(q) - 1.0) * 0.5, -(p.y + 0.5));
}

// Value noise, good enough for flame turbulence.
float Hash21(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float Noise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = Hash21(i);
	float b = Hash21(i + vec2(1.0, 0.0));
	float c = Hash21(i + vec2(0.0, 1.0));
	float d = Hash21(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
	int shape = int(v_Params.x + 0.5);

	// ---- textured path (Korean text) ---------------------------------------
	if (shape == 4)
	{
		float a = texture(u_Tex, v_TexUV).r;
		FragColor = vec4(v_ColorA.rgb, v_ColorA.a * a * v_Alpha);
		return;
	}

	// ---- sprite ----------------------------------------------------------------
	// The sprite already carries its own lighting from Blender. Only the tint,
	// the aerial haze and the see-through fade are applied here, so a sprite
	// behaves like the SDF props around it.
	if (shape == 8)
	{
		vec4 t = texture(u_Sprite, v_TexUV);
		if (t.a < 0.004) discard;
		vec3 c = t.rgb * v_ColorA.rgb;
		c = mix(c, u_Haze.rgb, v_Haze);
		FragColor = vec4(c, t.a * v_ColorA.a * v_Alpha);
		return;
	}

	// ---- flame ---------------------------------------------------------------
	// Fully procedural: the flame silhouette, the turbulence and the colour
	// ramp all come from the shader, so one quad is one fire.
	if (shape == 6)
	{
		float t = v_Extra.x;
		vec2  uv = v_UV;

		// Rising turbulence
		float n = Noise(vec2(uv.x * 5.0, uv.y * 4.0 - t * 2.4)) * 0.55
		        + Noise(vec2(uv.x * 11.0, uv.y * 9.0 - t * 4.1)) * 0.28;

		// Teardrop body: wide at the base, pinched at the tip
		float taper = pow(1.0 - uv.y, 0.55);
		float width = 0.40 * taper;
		float dx    = abs(uv.x - 0.5) - (n - 0.42) * 0.10 * taper;

		float body = 1.0 - smoothstep(width * 0.55, width, dx);
		body *= smoothstep(0.02, 0.20, 1.0 - uv.y);   // fade the tip
		body *= smoothstep(0.0, 0.12, uv.y);          // fade the base

		float heat = clamp(body * (0.75 + n * 0.55) * v_Extra.y, 0.0, 1.0);
		if (heat < 0.02) discard;

		// White hot core -> yellow -> orange -> deep red edge
		vec3 col = mix(vec3(0.72, 0.10, 0.02), vec3(1.00, 0.52, 0.10),
			smoothstep(0.10, 0.45, heat));
		col = mix(col, vec3(1.00, 0.88, 0.45), smoothstep(0.45, 0.72, heat));
		col = mix(col, vec3(1.00, 0.98, 0.90), smoothstep(0.78, 0.96, heat));

		FragColor = vec4(col, heat * v_ColorA.a * v_Alpha);
		return;
	}

	float d;
	if      (shape == 1) d = sdEllipse(v_Local);
	else if (shape == 2) d = sdDiamond(v_Local);
	else if (shape == 5) d = sdDiamond(v_Local);
	else if (shape == 3) d = sdTriangle(v_Local);
	else if (shape == 7) d = sdBowl(v_Local);
	else                 d = sdRoundRect(v_Local, vec2(0.5), v_Params.y);

	vec4 col = mix(v_ColorA, v_ColorB, clamp(v_UV.y, 0.0, 1.0));

	// Aerial haze tints the base colour. Water sparkle and rim light go on
	// top afterwards, as they did when the haze was mixed in on the CPU.
	col.rgb = mix(col.rgb, u_Haze.rgb, v_Haze);

	// ---- water ---------------------------------------------------------------
	if (shape == 5)
	{
		float t = v_Extra.x;
		float a = sin((v_UV.x + v_UV.y) * 17.0 + t * 2.1) * 0.5 + 0.5;
		float b = sin((v_UV.x - v_UV.y) * 23.0 - t * 1.3) * 0.5 + 0.5;
		float sparkle = pow(a * b, 4.0);
		float swell   = sin((v_UV.x * 3.0 + v_UV.y * 2.0) + t * 0.9) * 0.5 + 0.5;

		col.rgb += vec3(0.05, 0.07, 0.08) * swell;
		col.rgb += vec3(0.55, 0.62, 0.60) * sparkle * v_Extra.y;
	}

	// ---- rim light -----------------------------------------------------------
	if (v_Extra.z > 0.0)
	{
		float band = smoothstep(0.06, 0.0, abs(d + 0.045)) * (1.0 - v_UV.y);
		col.rgb += vec3(band) * v_Extra.z;
	}

	float aa = max(fwidth(d), 0.0008);
	float w  = aa + v_Params.z;
	float alpha = 1.0 - smoothstep(-w, w, d);

	FragColor = vec4(col.rgb, col.a * alpha * v_Alpha);
}
