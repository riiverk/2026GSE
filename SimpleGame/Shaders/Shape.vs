#version 330

// Screen-space quad. Every world object and every UI element is one of these,
// shaped by the fragment shader.

in vec3 a_Position;

out vec2 v_Local;   // -0.5 .. +0.5
out vec2 v_UV;      //  0.0 .. 1.0  (y = 0 at the top edge)

uniform vec4 u_Rect;    // centreX, centreY, halfWidth, halfHeight (pixels)
uniform vec2 u_Screen;  // window size (pixels)

void main()
{
	v_Local = a_Position.xy;
	v_UV    = a_Position.xy + vec2(0.5);

	vec2 p = u_Rect.xy + a_Position.xy * (u_Rect.zw * 2.0);

	vec2 ndc;
	ndc.x = p.x / u_Screen.x * 2.0 - 1.0;
	ndc.y = 1.0 - p.y / u_Screen.y * 2.0;

	gl_Position = vec4(ndc, 0.0, 1.0);
}
