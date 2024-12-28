
uniform vec4 lightSourcePosition[3];	// W0 = environment map rotation (-1.0 to 1.0), W1, W2 = viewport X, Y
uniform vec4 lightSourceDiffuse[3];		// A0 = overall brightness, A1, A2 = viewport width, height

uniform vec3 lineParams;			// width, stipple factor, stipple pattern

out vec4 lineCoords;

void drawLine( vec4 p0, vec4 p1 )
{
	if ( !( p0.w > 0.0 && p1.w > 0.0 ) )
		return;

	vec3	p0_ndc = p0.xyz / p0.w;
	vec3	p1_ndc = p1.xyz / p1.w;

	vec2	vpScale = vec2( lightSourceDiffuse[1].a, lightSourceDiffuse[2].a ) * 0.5;
	vec2	vpOffs = vec2( lightSourcePosition[1].w, lightSourcePosition[2].w ) + vpScale;

	vec2	p0_ss = p0_ndc.xy * vpScale;
	vec2	p1_ss = p1_ndc.xy * vpScale;
	lineCoords = vec4( p0_ss, p1_ss ) + vec4( vpOffs, vpOffs );

	vec2	d = normalize( p1_ss - p0_ss ) * ( lineParams.x * 0.5 );
	vec2	n = vec2( -d.y, d.x ) / vpScale;
	d = d / vpScale;

	gl_Position = vec4( p0_ndc.xy - d, p0_ndc.z, 1.0 );
	EmitVertex();
	gl_Position = vec4( p0_ndc.xy + n, p0_ndc.z, 1.0 );
	EmitVertex();
	gl_Position = vec4( p0_ndc.xy - n, p0_ndc.z, 1.0 );
	EmitVertex();
	gl_Position = vec4( p1_ndc.xy + n, p1_ndc.z, 1.0 );
	EmitVertex();
	gl_Position = vec4( p1_ndc.xy - n, p1_ndc.z, 1.0 );
	EmitVertex();
	gl_Position = vec4( p1_ndc.xy + d, p1_ndc.z, 1.0 );
	EmitVertex();

	EndPrimitive();
}

