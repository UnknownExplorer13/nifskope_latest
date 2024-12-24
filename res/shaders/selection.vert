#version 410 core

out vec4 C;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

uniform int numBones;
uniform mat4x3 boneTransforms[100];

layout ( location = 0 ) in vec3	vertexPosition;
layout ( location = 6 ) in vec4	boneWeights0;
layout ( location = 7 ) in vec4	boneWeights1;

void main( void )
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
				mat4x3	m = boneTransforms[bone];
				vTmp += m * v * w;
				wSum += w;
			}
		}
		if ( wSum > 0.0 )
			v = vec4( vTmp / wSum, 1.0 );
	}

	v = modelViewMatrix * v;
	gl_Position = projectionMatrix * v;
}
