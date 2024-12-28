#version 410 core

out vec4 C;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

uniform vec4 vertexColorOverride;	// components greater than zero replace the vertex color
uniform vec4 highlightColor;

// bit 0 = Scene::selecting
// bit 1 = vertex mode (drawing points instead of triangles)
// bits 8 to 15 = point size * 8 (0: do not draw smooth points)
uniform int selectionFlags;
// if Scene::selecting == false: vertex selected (-1: none)
// if Scene::selecting == true: value to add to color key (e.g. shapeID << 16)
uniform int selectionParam;

uniform int numBones;
uniform mat4x3 boneTransforms[100];

layout ( location = 0 ) in vec3	vertexPosition;
layout ( location = 1 ) in vec4	vertexColor;
layout ( location = 5 ) in vec4	boneWeights0;
layout ( location = 6 ) in vec4	boneWeights1;

void main()
{
	vec4	v = vec4( vertexPosition, 1.0 );

	if ( numBones > 0 ) {
		vec3	vTmp = vec3( 0.0 );
		float	wSum = 0.0;
		for ( int i = 0; i < 8; i++ ) {
			float	bw;
			if ( i < 4 )
				bw = boneWeights0[i];
			else
				bw = boneWeights1[i & 3];
			if ( bw > 0.0 ) {
				int	bone = int( bw );
				if ( bone >= numBones )
					continue;
				float	w = fract( bw );
				vTmp += boneTransforms[bone] * v * w;
				wSum += w;
			}
		}
		if ( wSum > 0.0 )
			v = vec4( vTmp / wSum, 1.0 );
	}

	v = projectionMatrix * ( modelViewMatrix * v );

	if ( ( selectionFlags & 1 ) != 0 ) {
		int	colorKey = selectionParam + 1;
		if ( ( selectionFlags & 2 ) != 0 )
			colorKey = colorKey + gl_VertexID;
		C = unpackUnorm4x8( uint( colorKey ) ) + vec4( 0.0005 );
	} else if ( !( gl_VertexID == selectionParam && ( selectionFlags & 2 ) != 0 ) ) {
		C = mix( vertexColor, vertexColorOverride, greaterThan( vertexColorOverride, vec4( 0.0 ) ) );
	} else {
		C = highlightColor;
	}

	gl_Position = v;
}
