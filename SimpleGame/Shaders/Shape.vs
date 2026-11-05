#version 330

// Instanced screen-space quads. Every world object and every UI element is one
// instance of the same unit quad; per-shape data comes from the instance
// buffer, so thousands of shapes go out in one draw call.
//
// Rotation is applied around an arbitrary pivot expressed in LOCAL units
// (-0.5 .. +0.5). A limb sets its pivot to the joint end of the bone, so the
// bone swings about the joint instead of sliding away from it.
//
// Two kinds of batch use this shader:
//   dynamic  positions are final screen pixels (camera and zoom already
//            applied on the CPU). u_Offset = 0, u_Scale = 1.
//   static   positions are recorded without the camera, once. The camera
//            (u_Offset) and world zoom (u_Scale) are applied here.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 i_Rect;     // centre x, y, half w, half h (px)
layout(location = 2) in vec4 i_ColorA;   // top colour
layout(location = 3) in vec4 i_ColorB;   // bottom colour
layout(location = 4) in vec4 i_Rot;      // cos, sin, pivot x, pivot y (local)
layout(location = 5) in vec4 i_Params;   // shape, corner radius, softness, anchor flags
layout(location = 6) in vec4 i_Extra;    // time offset, intensity/ripple, rim, sway freq
layout(location = 7) in vec4 i_Anchor;   // anchor x, y (pre-zoom px), depth, unused
layout(location = 8) in vec4 i_Sway;     // pivot x, y (px), phase, amplitude (rad)
layout(location = 9) in vec4 i_UV;       // atlas u0, v0, u1, v1 (text only)

out vec2 v_Local;   // -0.5 .. +0.5
out vec2 v_UV;      //  0.0 .. 1.0  (y = 0 at the top edge)
out vec2 v_TexUV;

flat out vec4  v_ColorA;
flat out vec4  v_ColorB;
flat out vec4  v_Params;
flat out vec4  v_Extra;   // x = absolute time
flat out float v_Haze;    // 0 .. strength
flat out float v_Alpha;   // see-through multiplier

uniform vec2  u_Screen;
uniform vec2  u_Offset;
uniform float u_Scale;
uniform float u_Time;

uniform vec4  u_Haze;          // rgb, strength
uniform float u_HazeZoom;      // world zoom used to place the anchor on screen
uniform vec4  u_SeeThru;       // player depth, player x, player y, enabled
uniform vec4  u_SeeThruShape;  // half width, height above, depth below, alpha

void main()
{
	v_Local = a_Position.xy;
	v_UV    = a_Position.xy + vec2(0.5);
	v_TexUV = mix(i_UV.xy, i_UV.zw, v_UV);

	vec2  center = i_Rect.xy + u_Offset;
	vec2  halfSz = i_Rect.zw;
	float c = i_Rot.x;
	float s = i_Rot.y;

	// Wind sway: the whole part orbits the sway pivot (a trunk joint) and
	// turns by the same angle, exactly like a rotated parent joint would.
	if (i_Sway.w != 0.0)
	{
		float a  = sin(u_Time * i_Extra.w + i_Sway.z) * i_Sway.w;
		float ca = cos(a);
		float sa = sin(a);
		vec2  pv = i_Sway.xy + u_Offset;
		vec2  d  = center - pv;
		center = pv + vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
		float nc = c * ca - s * sa;
		float ns = s * ca + c * sa;
		c = nc;
		s = ns;
	}

	// World zoom for static batches (1.0 for dynamic ones).
	vec2 sc = u_Screen * 0.5;
	center = sc + (center - sc) * u_Scale;
	halfSz *= u_Scale;

	vec2 size  = halfSz * 2.0;
	vec2 pivot = i_Rot.zw;

	// Offset from the pivot, in pixels, then rotated.
	vec2 dd = (a_Position.xy - pivot) * size;
	vec2 r  = vec2(dd.x * c - dd.y * s, dd.x * s + dd.y * c);
	vec2 p  = center + pivot * size + r;

	vec2 ndc;
	ndc.x = p.x / u_Screen.x * 2.0 - 1.0;
	ndc.y = 1.0 - p.y / u_Screen.y * 2.0;
	gl_Position = vec4(ndc, 0.0, 1.0);

	// ---- per-object effects, evaluated once per instance -------------------
	int   flags  = int(i_Params.w + 0.5);
	vec2  anchor = i_Anchor.xy + u_Offset;

	// Aerial perspective: the higher on screen (farther away), the more the
	// colour leans toward the sky. Uses the object's anchor, not the pixel,
	// so a whole tree fades as one.
	float haze = 0.0;
	if ((flags & 1) != 0)
	{
		float zy = sc.y + (anchor.y - sc.y) * u_HazeZoom;
		float t  = clamp(1.0 - zy / u_Screen.y, 0.0, 1.0);
		haze = t * t * u_Haze.w;
	}

	// See-through: a tall object standing in front of the player (deeper in
	// painter order) and covering the player's spot turns translucent.
	float alpha = 1.0;
	if ((flags & 2) != 0 && u_SeeThru.w > 0.5 && i_Anchor.z > u_SeeThru.x)
	{
		float ddx = abs(anchor.x - u_SeeThru.y);
		float ddy = anchor.y - u_SeeThru.z;
		if (ddx < u_SeeThruShape.x && ddy > -u_SeeThruShape.z && ddy < u_SeeThruShape.y)
			alpha = u_SeeThruShape.w;
	}

	v_ColorA = i_ColorA;
	v_ColorB = i_ColorB;
	v_Params = i_Params;
	v_Extra  = vec4(i_Extra.x + u_Time, i_Extra.y, i_Extra.z, i_Extra.w);
	v_Haze   = haze;
	v_Alpha  = alpha;
}
