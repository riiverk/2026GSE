#version 330

// Final composite: bloom, tone curve, grade, vignette.
//
// The grade is where the Provencal palette actually lands. Individual objects
// are painted in roughly the right colours; this pass pulls the whole frame
// into one light.
//
// The old shoulder was c/(c+0.28)*1.28, which lifted 0.2 all the way to 0.53.
// Everything landed in the top half of the range, so the frame read as bright
// and flat with no shadow anywhere. The curve below rolls off only the
// highlights and then pushes contrast about a midtone pivot, which is what
// puts the dark end back.

in vec2 v_UV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform float     u_BloomAmount;
uniform float     u_Vignette;
uniform float     u_Exposure;
uniform float     u_White;        // 롤오프 화이트 포인트. 낮을수록 빨리 포화
uniform float     u_Contrast;
uniform float     u_Pivot;        // 대비 기준점. 이 밝기는 변하지 않는다
uniform float     u_Saturation;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main()
{
	vec3 c = texture(u_Scene, v_UV).rgb;
	vec3 b = texture(u_Bloom, v_UV).rgb;

	c += b * u_BloomAmount;
	c *= u_Exposure;

	// 하늘빛. 위가 조금 밝고 따뜻, 아래가 조금 어둡고 차다. 월드가 아니라
	// 화면 기준 앰비언트지만, 쿼터뷰에서는 위쪽이 곧 먼 곳이라 위화감이 없고
	// 평평하던 지면에 상하 입체가 생긴다.
	float sky = 1.0 - v_UV.y;
	c *= mix(vec3(0.965, 0.980, 1.010), vec3(1.030, 1.028, 1.010), sky);

	// 하이라이트 롤오프 (extended Reinhard). 어두운 쪽은 거의 건드리지 않는다.
	c = c * (1.0 + c / (u_White * u_White)) / (1.0 + c);

	// 피벗 기준 대비 — 그림자를 실제로 바닥까지 내리는 단계.
	c = (c - u_Pivot) * u_Contrast + u_Pivot;
	c = max(c, vec3(0.0));

	// 스플릿 톤. 햇빛이 따뜻하면 그늘은 차다. 이 한 줄이 명암을 "조명"으로
	// 읽히게 만든다.
	float l = dot(c, LUMA);
	float hi = smoothstep(0.22, 0.78, l);
	c *= mix(vec3(0.965, 0.985, 1.055), vec3(1.020, 1.010, 0.995), hi);

	// 채도
	l = dot(c, LUMA);
	c = mix(vec3(l), c, u_Saturation);

	// 비네팅. 모서리는 어둡게만이 아니라 살짝 차갑게 빠진다.
	vec2 q = v_UV - 0.5;
	float v = clamp(1.0 - dot(q, q) * u_Vignette, 0.0, 1.0);
	c *= mix(vec3(0.92, 0.94, 1.0), vec3(1.0), v);
	c *= v;

	FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
