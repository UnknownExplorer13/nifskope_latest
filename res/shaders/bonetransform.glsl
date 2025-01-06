uniform int numBones;
uniform mat3x4 boneTransforms[MAX_NUM_BONES];

#ifdef BT_POSITION_ONLY
void boneTransform( inout vec4 v )
{
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
			vTmp += v * boneTransforms[bone] * w;
			wSum += w;
		}
	}
	if ( wSum > 0.0 )
		v = vec4( vTmp / wSum, 1.0 );
}
#elif defined( BT_NO_TANGENTS )
void boneTransform( inout vec4 v, inout vec3 n )
{
	vec3	vTmp = vec3( 0.0 );
	vec3	nTmp = vec3( 0.0 );
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
			mat3x4	m = boneTransforms[bone];
			vTmp += v * m * w;
			nTmp += n * mat3( m ) * w;
			wSum += w;
		}
	}
	if ( wSum > 0.0 ) {
		v = vec4( vTmp / wSum, 1.0 );
		n = nTmp;
	}
}
#else
void boneTransform( inout vec4 v, inout vec3 n, inout vec3 t, inout vec3 b )
{
	vec3	vTmp = vec3( 0.0 );
	vec3	nTmp = vec3( 0.0 );
	vec3	tTmp = vec3( 0.0 );
	vec3	bTmp = vec3( 0.0 );
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
			mat3x4	m = boneTransforms[bone];
			mat3	r = mat3( m );
			vTmp += v * m * w;
			nTmp += n * r * w;
			tTmp += t * r * w;
			bTmp += b * r * w;
			wSum += w;
		}
	}
	if ( wSum > 0.0 ) {
		v = vec4( vTmp / wSum, 1.0 );
		n = nTmp;
		t = tTmp;
		b = bTmp;
	}
}
#endif
