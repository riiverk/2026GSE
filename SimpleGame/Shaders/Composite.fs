#version 330

// Final composite: bloom, colour grade, vignette.
//
// The grade is where the Provencal palette actually lands. Individual objects
// are painted in roughly the right colours; this pass pulls the whole frame
// into one light.

in vec2 v_UV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform float     u_BloomAmount;
uniform float     u_Vignette;

void main()
{
	vec3 c = texture(u_Scene, v_UV).rgb;
	vec3 b = texture(u_Bloom, v_UV).rgb;

	c += b * u_BloomAmount;

	// Warm lift: a touch of amber in the shadows, a touch of gain in the reds.
	vec3 lift = vec3(0.020, 0.014, 0.008);
	vec3 gain = vec3(1.055, 1.010, 0.945);
	c = lift + c * gain;

	// Gentle saturation. Pushes the terracotta and the olive apart.
	float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
	c = mix(vec3(l), c, 1.14);

	// Soft shoulder so highlights roll off instead of clipping flat.
	c = c / (c + vec3(0.28)) * 1.28;

	vec2 q = v_UV - 0.5;
	float v = 1.0 - dot(q, q) * u_Vignette;
	c *= clamp(v, 0.0, 1.0);

	FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
