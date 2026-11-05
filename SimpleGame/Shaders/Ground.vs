#version 330

// Full-screen pass for the terrain. The fragment shader works out which piece
// of the world is under each pixel, so the vertex stage only covers the screen.

layout(location = 0) in vec3 a_Position;

void main()
{
	gl_Position = vec4(a_Position.xy * 2.0, 0.0, 1.0);
}
