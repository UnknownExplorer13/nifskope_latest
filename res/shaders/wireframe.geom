#version 410 core

layout ( triangles ) in;
layout ( triangle_strip, max_vertices = 12 ) out;

uniform mat4 projectionMatrix;
uniform vec4 lightSourcePosition[3];	// W0 = environment map rotation (-1.0 to 1.0), W1, W2 = viewport X, Y
uniform vec4 lightSourceDiffuse[3];		// A0 = overall brightness, A1, A2 = viewport width, height

uniform float lineWidth;

in vec4 vsColor[];
out vec4 C;

void drawLine( vec4 p0, vec4 p1, vec4 p1Color )
{
	float	p0zw = p0.z + p0.w;
	float	p1zw = p1.z + p1.w;
	if ( !( p0zw > 0.0 && p1zw > 0.0 ) ) {
		if ( p0zw > 0.000001 )
			p1 = mix( p0, p1, p0zw / ( p0zw - p1zw ) );
		else if ( p1zw > 0.000001 )
			p0 = mix( p1, p0, p1zw / ( p1zw - p0zw ) );
		else
			return;
	}

	vec3	p0_ndc = p0.xyz / p0.w;
	vec3	p1_ndc = p1.xyz / p1.w;

	vec2	vpScale = vec2( lightSourceDiffuse[1].a, lightSourceDiffuse[2].a ) * 0.5;
	vec2	vpOffs = vec2( lightSourcePosition[1].w, lightSourcePosition[2].w ) + vpScale;

	vec2	p0_ss = p0_ndc.xy * vpScale;
	vec2	p1_ss = p1_ndc.xy * vpScale;

	vec2	d = normalize( p1_ss - p0_ss ) * max( lineWidth * 0.5, 0.5 );
	vec2	n = vec2( -d.y, d.x ) / vpScale;

	gl_Position = vec4( p0_ndc.xy + n, p0_ndc.z, 1.0 );
	EmitVertex();
	gl_Position = vec4( p0_ndc.xy - n, p0_ndc.z, 1.0 );
	EmitVertex();

	C = p1Color;

	gl_Position = vec4( p1_ndc.xy + n, p1_ndc.z, 1.0 );
	EmitVertex();
	gl_Position = vec4( p1_ndc.xy - n, p1_ndc.z, 1.0 );
	EmitVertex();

	EndPrimitive();
}

void main()
{
	vec4	p0 = projectionMatrix * gl_in[0].gl_Position;
	vec4	p1 = projectionMatrix * gl_in[1].gl_Position;
	vec4	p2 = projectionMatrix * gl_in[2].gl_Position;

	float	alphaMult = min( lineWidth, 1.0 );
	vec4	c0 = vec4( vsColor[0].rgb, vsColor[0].a * alphaMult );
	C = c0;

	drawLine( p0, p1, vec4( vsColor[1].rgb, vsColor[1].a * alphaMult ) );
	drawLine( p1, p2, vec4( vsColor[2].rgb, vsColor[2].a * alphaMult ) );
	drawLine( p2, p0, c0 );
}
