#version 410 core

#include "uniforms.glsl"

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D SpecularMap;
uniform sampler2D LightMask;
uniform sampler2D TintMask;
uniform sampler2D DetailMask;
uniform sampler2D BacklightMap;

uniform vec3 specColor;
uniform float specStrength;
uniform float specGlossiness;

uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;
uniform int alphaTestFunc;
uniform float alphaThreshold;

uniform vec3 tintColor;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasSoftlight;
uniform bool hasBacklight;
uniform bool hasRimlight;
uniform bool hasModelSpaceNormals;
uniform bool hasSpecularMap;
uniform bool hasDetailMask;
uniform bool hasTintMask;
uniform bool hasTintColor;

uniform float lightingEffect1;
uniform float lightingEffect2;

uniform mat4 modelViewMatrix;

in vec3 LightDir;
in vec3 ViewDir;

in vec2 texCoord;

flat in vec4 A;
in vec4 C;
flat in vec4 D;

out vec4 fragColor;


vec3 tonemap(vec3 x)
{
	float a = 0.15;
	float b = 0.50;
	float c = 0.10;
	float d = 0.20;
	float e = 0.02;
	float f = 0.30;

	vec3 z = x * x * D.a * (A.a * 4.22978723);
	z = (z * (a * z + b * c) + d * e) / (z * (a * z + b) + d * f) - e / f;
	return sqrt(z / (A.a * 0.93333333));
}

vec3 toGrayscale(vec3 color)
{
	return vec3(dot(vec3(0.3, 0.59, 0.11), color));
}

float overlay( float base, float blend )
{
	float result;
	if ( base < 0.5 ) {
		result = 2.0 * base * blend;
	} else {
		result = 1.0 - 2.0 * (1.0 - blend) * (1.0 - base);
	}
	return result;
}

vec3 overlay( vec3 ba, vec3 bl )
{
	return vec3( overlay(ba.r, bl.r), overlay(ba.g, bl.g), overlay( ba.b, bl.b ) );
}

void main()
{
	vec2 offset = texCoord.st * uvScale + uvOffset;

	vec4 baseMap = texture( BaseMap, offset );

	vec4 color = baseMap;
	color.a = C.a * baseMap.a * alpha;
	if ( alphaTestFunc > 0 ) {
		if ( color.a < alphaThreshold && alphaTestFunc != 1 && alphaTestFunc != 3 && alphaTestFunc != 5 )
			discard;
		if ( color.a == alphaThreshold && alphaTestFunc != 2 && alphaTestFunc != 3 && alphaTestFunc != 6 )
			discard;
		if ( color.a > alphaThreshold && ( alphaTestFunc < 4 || alphaTestFunc > 6 ) )
			discard;
	}

	vec4 normalMap = texture( NormalMap, offset );

	vec3 normal = normalMap.rgb * 2.0 - 1.0;

	// Convert model space to view space
	//	Swizzled G/B values!
	normal = normalize( mat3(modelViewMatrix) * normal.rbg );

	// Face Normals
	//vec3 X = dFdx(v);
	//vec3 Y = dFdy(v);
	//vec3 constructedNormal = normalize(cross(X,Y));


	vec3 L = normalize(LightDir);
	vec3 E = normalize(ViewDir);
	vec3 R = reflect(-L, normal);
	vec3 H = normalize( L + E );

	float NdotL = max( dot(normal, L), 0.0 );
	float NdotH = max( dot(normal, H), 0.0 );
	float EdotN = max( dot(normal, E), 0.0 );
	float NdotNegL = max( dot(normal, -L), 0.0 );


	vec3 albedo = baseMap.rgb * C.rgb;
	vec3 diffuse = A.rgb + (D.rgb * NdotL);


	// Emissive
	vec3 emissive = vec3(0.0);
	if ( hasEmit ) {
		emissive += glowColor * glowMult;
	}

	// Specular

	float s = texture( SpecularMap, offset ).r;
	if ( !hasSpecularMap || hasBacklight ) {
		s = normalMap.a;
	}

	vec3 spec = clamp( specColor * specStrength * s * pow(NdotH, specGlossiness), 0.0, 1.0 );
	spec *= D.rgb;


	vec3 backlight = vec3(0.0);
	if ( hasBacklight ) {
		backlight = texture( BacklightMap, offset ).rgb;
		backlight *= NdotNegL;

		emissive += backlight * D.rgb;
	}

	vec4 mask = vec4(0.0);
	if ( hasRimlight || hasSoftlight ) {
		mask = texture( LightMask, offset );
	}

	vec3 rim = vec3(0.0);
	if ( hasRimlight ) {
		rim = mask.rgb * pow(vec3((1.0 - EdotN)), vec3(lightingEffect2));
		rim *= smoothstep( -0.2, 1.0, dot(-L, E) );

		emissive += rim * D.rgb;
	}

	vec3 soft = vec3(0.0);
	if ( hasSoftlight ) {
		float wrap = (dot(normal, L) + lightingEffect1) / (1.0 + lightingEffect1);

		soft = max( wrap, 0.0 ) * mask.rgb * smoothstep( 1.0, 0.0, NdotL );
		soft *= sqrt( clamp( lightingEffect1, 0.0, 1.0 ) );

		emissive += soft * D.rgb;
	}

	vec3 detail = vec3(0.0);
	if ( hasDetailMask ) {
		detail = texture( DetailMask, offset ).rgb;

		albedo = overlay( albedo, detail );
	}

	vec3 tint = vec3(0.0);
	if ( hasTintMask ) {
		tint = texture( TintMask, offset ).rgb;

		albedo = overlay( albedo, tint );
	}

	if ( hasDetailMask ) {
		albedo += albedo;
	}

	if ( hasTintColor ) {
		albedo *= tintColor;
	}

	color.rgb = albedo * (diffuse + emissive * glowScaleSRGB) + spec;
	color.rgb = tonemap( color.rgb );

	fragColor = color;
}
