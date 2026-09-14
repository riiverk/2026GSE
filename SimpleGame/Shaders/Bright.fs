#version 330

// Bright pass: keep only what is above the threshold, so the blur that follows
// blooms highlights instead of smearing the whole frame.

in vec2 v_UV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Tex;
uniform float     u_Threshold;

void main()
{
	vec3 c = texture(u_Tex, v_UV).rgb;
	float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
	float k = max(l - u_Threshold, 0.0) / max(1.0 - u_Threshold, 0.0001);
	FragColor = vec4(c * k, 1.0);
}
