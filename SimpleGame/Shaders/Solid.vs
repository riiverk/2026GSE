#version 330

// Screen-space quad shader.
//
// The unit shape in the VBO spans -0.5 .. +0.5 on both axes. It is scaled by a
// half-extent given in PIXELS and then translated to a pixel centre, so the
// horizontal and vertical scale are independent. Non-uniform scale is what the
// isometric diamond (2:1) requires and what the original u_Trans shader could
// not express.
//
// Pixel space: origin at the top-left of the window, +y pointing down.

in vec3 a_Position;

uniform vec4 u_Rect;    // centreX, centreY, halfWidth, halfHeight  (pixels)
uniform vec2 u_Screen;  // window width, height (pixels)

void main()
{
	vec2 p = u_Rect.xy + a_Position.xy * (u_Rect.zw * 2.0);

	vec2 ndc;
	ndc.x = p.x / u_Screen.x * 2.0 - 1.0;
	ndc.y = 1.0 - p.y / u_Screen.y * 2.0;

	gl_Position = vec4(ndc, 0.0, 1.0);
}
