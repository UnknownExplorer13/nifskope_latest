#version 410 core

in vec4 C;

// bit 0 = Scene::selecting
// bit 1 = vertex mode (drawing points instead of triangles)
// bits 8 to 15 = point size * 8 (0: do not draw smooth points)
uniform int selectionFlags;

out vec4 fragColor;

void main(void)
{
	float	a = C.a;

	if ( ( selectionFlags & 2 ) != 0 ) {
		// draw points as circles
		vec2	d = gl_PointCoord - vec2( 0.5 );
		float	r = sqrt( dot( d, d ) );
		if ( r > 0.5 )
			discard;

		// with anti-aliasing if enabled
		int	p = selectionFlags & 0xFF00;
		if ( p != 0 )
			a = a * clamp( ( 0.5 - r ) * float( p ) / 2048.0, 0.0, 1.0 );
	}

	fragColor = vec4( C.rgb, a );
}
