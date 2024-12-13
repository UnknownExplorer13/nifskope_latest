#version 410 core

out vec3 LightDir;
out vec3 ViewDir;

out vec3 v;

out vec4 A;
out vec4 C;
out vec4 D;


void main( void )
{
	gl_Position = ftransform();
	gl_TexCoord[0] = gl_MultiTexCoord0;
	
	v = vec3(gl_ModelViewMatrix * gl_Vertex);

	if (gl_ProjectionMatrix[3][3] == 1.0)
		v = vec3(0.0, 0.0, -1.0);	// orthographic view
	ViewDir = -v.xyz;
	LightDir = gl_LightSource[0].position.xyz;

	A = vec4(sqrt(gl_LightSource[0].ambient.rgb) * 0.375, gl_LightSource[0].ambient.a);
	C = gl_Color;
	D = sqrt(gl_LightSource[0].diffuse);
}
