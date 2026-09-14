#version 330

// Signed-distance shapes.
//
// Drawing shapes as distance fields instead of raw geometry buys three things
// the previous renderer could not have:
//   - analytically anti-aliased edges (this is what kills the "pixel" look)
//   - rounded corners at no extra cost
//   - genuinely soft shadows, by widening the edge falloff
//
// Shape ids:
//   0 rounded rect   1 ellipse   2 diamond   3 triangle
//   4 texture (text) 5 water tile

in vec2 v_Local;
in vec2 v_UV;

layout(location = 0) out vec4 FragColor;

uniform int       u_Shape;
uniform vec4      u_ColorA;   // colour at the top edge
uniform vec4      u_ColorB;   // colour at the bottom edge
uniform float     u_Radius;   // corner radius, in local units (0 .. 0.5)
uniform float     u_Soft;     // extra edge falloff; 0 = crisp, 0.3 = soft shadow
uniform vec4      u_Extra;    // x: time   y: ripple strength   z: rim light
uniform sampler2D u_Tex;

float sdRoundRect(vec2 p, vec2 half_, float r)
{
	vec2 d = abs(p) - half_ + vec2(r);
	return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

float sdEllipse(vec2 p)
{
	// The quad is already scaled to the ellipse extents, so a unit circle in
	// local space is all we need.
	return length(p) - 0.5;
}

float sdDiamond(vec2 p)
{
	return abs(p.x) + abs(p.y) - 0.5;
}

float sdTriangle(vec2 p)
{
	// Isoceles, apex at the top of the quad.
	vec2 apex = vec2(0.0, -0.5);
	vec2 nL = normalize(vec2(-1.0, -0.5));
	vec2 nR = normalize(vec2( 1.0, -0.5));
	float dL = dot(p - apex, nL);
	float dR = dot(p - apex, nR);
	float dB = p.y - 0.5;
	return max(max(dL, dR), dB);
}

void main()
{
	// ---- textured path (Korean text) ---------------------------------------
	if (u_Shape == 4)
	{
		vec4 t = texture(u_Tex, v_UV);
		FragColor = vec4(u_ColorA.rgb, u_ColorA.a * t.a);
		return;
	}

	float d;
	if      (u_Shape == 1) d = sdEllipse(v_Local);
	else if (u_Shape == 2) d = sdDiamond(v_Local);
	else if (u_Shape == 5) d = sdDiamond(v_Local);
	else if (u_Shape == 3) d = sdTriangle(v_Local);
	else                   d = sdRoundRect(v_Local, vec2(0.5), u_Radius);

	// Vertical gradient. Even a two-percent difference between top and bottom
	// stops a shape from reading as flat colour.
	vec4 col = mix(u_ColorA, u_ColorB, clamp(v_UV.y, 0.0, 1.0));

	// ---- water ---------------------------------------------------------------
	if (u_Shape == 5)
	{
		float t = u_Extra.x;
		float a = sin((v_UV.x + v_UV.y) * 17.0 + t * 2.1) * 0.5 + 0.5;
		float b = sin((v_UV.x - v_UV.y) * 23.0 - t * 1.3) * 0.5 + 0.5;
		float sparkle = pow(a * b, 4.0);
		float swell   = sin((v_UV.x * 3.0 + v_UV.y * 2.0) + t * 0.9) * 0.5 + 0.5;

		col.rgb += vec3(0.05, 0.07, 0.08) * swell;
		col.rgb += vec3(0.55, 0.62, 0.60) * sparkle * u_Extra.y;
	}

	// ---- rim light -----------------------------------------------------------
	// A thin bright band just inside the top edge. Cheap, and it does most of
	// the work of making a flat shape look lit.
	if (u_Extra.z > 0.0)
	{
		float band = smoothstep(0.06, 0.0, abs(d + 0.045)) * (1.0 - v_UV.y);
		col.rgb += vec3(band) * u_Extra.z;
	}

	float aa = max(fwidth(d), 0.0008);
	float w  = aa + u_Soft;
	float alpha = 1.0 - smoothstep(-w, w, d);

	FragColor = vec4(col.rgb, col.a * alpha);
}
