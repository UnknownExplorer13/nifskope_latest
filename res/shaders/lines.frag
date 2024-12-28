#version 410 core

in vec4 C;
in vec4 lineCoords;

uniform vec3 lineParams;			// width, stipple factor, stipple pattern

out vec4 fragColor;

void main()
{
	if ( lineParams.y > 0.0 && lineParams.z > 0.0 ) {
		// stipple
		vec2	p0 = lineCoords.xy;
		vec2	p1 = lineCoords.zw;
		vec2	l = p1 - p0;
		vec2	p = gl_FragCoord.xy - p0;
		vec2	d = normalize( l );
		d = vec2( dot( p, d ), p.y * d.x - p.x * d.y );		// position relative to line (p0 = 0.0,0.0, p1 = length,0.0)

		if ( ( ( 1 << ( int( d.x / lineParams.y ) & 15 ) ) & int( lineParams.z ) ) == 0 )
			discard;
	}

	fragColor = C;
}
