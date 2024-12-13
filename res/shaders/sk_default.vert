#version 410 core

out vec3 LightDir;
out vec3 ViewDir;

out mat3 tbnMatrix;
out mat3 reflMatrix;
out vec3 v;

out vec4 A;
out vec4 C;
out vec4 D;

uniform mat3 viewMatrix;

// FIXME: these uniforms are never set
uniform bool isGPUSkinned;
uniform mat4 boneTransforms[100];

void main( void )
{
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
	gl_TexCoord[0] = gl_MultiTexCoord0;

	vec3 v;
	if ( isGPUSkinned ) {
		mat4 bt = boneTransforms[int(gl_MultiTexCoord3[0])] * gl_MultiTexCoord4[0];
		bt += boneTransforms[int(gl_MultiTexCoord3[1])] * gl_MultiTexCoord4[1];
		bt += boneTransforms[int(gl_MultiTexCoord3[2])] * gl_MultiTexCoord4[2];
		bt += boneTransforms[int(gl_MultiTexCoord3[3])] * gl_MultiTexCoord4[3];

		vec4	V = bt * gl_Vertex;
		vec3	n = vec3( bt * vec4(gl_Normal, 0.0) );
		vec3	t = vec3( bt * vec4(gl_MultiTexCoord1.xyz, 0.0) );
		vec3	b = vec3( bt * vec4(gl_MultiTexCoord2.xyz, 0.0) );

		gl_Position = gl_ModelViewProjectionMatrix * V;
		tbnMatrix[2] = normalize( gl_NormalMatrix * n );
		tbnMatrix[1] = normalize( gl_NormalMatrix * t );
		tbnMatrix[0] = normalize( gl_NormalMatrix * b );
		v = vec3( gl_ModelViewMatrix * V );
	} else {
		tbnMatrix[2] = normalize( gl_NormalMatrix * gl_Normal );
		tbnMatrix[1] = normalize( gl_NormalMatrix * gl_MultiTexCoord1.xyz );
		tbnMatrix[0] = normalize( gl_NormalMatrix * gl_MultiTexCoord2.xyz );
		v = vec3( gl_ModelViewMatrix * gl_Vertex );
	}

	reflMatrix = viewMatrix;

	if (gl_ProjectionMatrix[3][3] == 1.0)
		v = vec3(0.0, 0.0, -1.0);	// orthographic view
	ViewDir = -v.xyz;
	LightDir = gl_LightSource[0].position.xyz;

	A = vec4(sqrt(gl_LightSource[0].ambient.rgb) * 0.375, gl_LightSource[0].ambient.a);
	C = gl_Color;
	D = sqrt(gl_LightSource[0].diffuse);
}
