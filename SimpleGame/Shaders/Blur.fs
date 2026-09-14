#version 330

// Separable gaussian. Run once horizontally, once vertically.

in vec2 v_UV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Tex;
uniform vec2      u_Dir;   // one texel step along the blur axis

void main()
{
	float w0 = 0.227027;
	float w1 = 0.194595;
	float w2 = 0.121622;
	float w3 = 0.054054;
	float w4 = 0.016216;

	vec3 sum = texture(u_Tex, v_UV).rgb * w0;

	sum += texture(u_Tex, v_UV + u_Dir * 1.0).rgb * w1;
	sum += texture(u_Tex, v_UV - u_Dir * 1.0).rgb * w1;
	sum += texture(u_Tex, v_UV + u_Dir * 2.0).rgb * w2;
	sum += texture(u_Tex, v_UV - u_Dir * 2.0).rgb * w2;
	sum += texture(u_Tex, v_UV + u_Dir * 3.0).rgb * w3;
	sum += texture(u_Tex, v_UV - u_Dir * 3.0).rgb * w3;
	sum += texture(u_Tex, v_UV + u_Dir * 4.0).rgb * w4;
	sum += texture(u_Tex, v_UV - u_Dir * 4.0).rgb * w4;

	FragColor = vec4(sum, 1.0);
}
