#version 330

// Full-screen pass. Reuses the same unit quad VBO as the shape shader.

in vec3 a_Position;
out vec2 v_UV;

void main()
{
	v_UV = a_Position.xy + vec2(0.5);
	gl_Position = vec4(a_Position.xy * 2.0, 0.0, 1.0);
}
