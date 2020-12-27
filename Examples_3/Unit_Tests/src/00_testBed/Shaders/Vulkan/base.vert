/*
  (C) 2019 David Lettier
  lettier.com
*/

//#version 150
#version 450 core

#ifdef PREDEFINED_MACRO
#include "stdmacro_defs.inc"
#endif

#define NUMBER_OF_LIGHTS 4

layout (std140, UPDATE_FREQ_PER_FRAME, binding=0) uniform uniformBlock {
	uniform mat4 p3d_ModelViewMatrix;
	uniform mat4 p3d_ProjectionMatrix;
	uniform mat3 p3d_NormalMatrix;
};

struct LightSourceParameters_t
{ 
	vec4 color  ; 
	vec4 ambient  ; 
	vec4 diffuse  ; 
	vec4 specular  ; 
	vec4 position  ; 
	vec3  spotDirection  ; 
	float spotExponent  ; 
	float spotCutoff  ; 
	float spotCosCutoff  ; 
	float constantAttenuation  ; 
	float linearAttenuation  ; 
	float quadraticAttenuation  ; 
	vec3 attenuation  ; 

	mat4 shadowViewMatrix  ;
};
  

layout (std140, UPDATE_FREQ_PER_FRAME, binding=1) uniform lightSourceParameters {
 LightSourceParameters_t p3d_LightSource[NUMBER_OF_LIGHTS];
};

layout(location = 0) in vec4 p3d_Vertex;
layout(location = 1) in vec3 p3d_Normal;

layout(location = 2) in vec4 p3d_Color;

layout(location = 3) in vec2 p3d_MultiTexCoord0;
layout(location = 4) in vec2 p3d_MultiTexCoord1;

layout(location = 5) in vec3 p3d_Binormal;
layout(location = 6) in vec3 p3d_Tangent;

layout(location = 0) out vec4 vertexPosition;
layout(location = 1) out vec4 vertexColor;

layout(location = 2) out vec3 vertexNormal;
layout(location = 3) out vec3 binormal;
layout(location = 4) out vec3 tangent;

layout(location = 5) out vec2 normalCoord;
layout(location = 6) out vec2 diffuseCoord;

layout(location = 7) out vec4 vertexInShadowSpaces[NUMBER_OF_LIGHTS];

void main() {
  vertexColor    = p3d_Color;
  vertexPosition = p3d_ModelViewMatrix * p3d_Vertex;

  vertexNormal = normalize(p3d_NormalMatrix * p3d_Normal);
  binormal     = normalize(p3d_NormalMatrix * p3d_Binormal);
  tangent      = normalize(p3d_NormalMatrix * p3d_Tangent);

  normalCoord   = p3d_MultiTexCoord0;
  diffuseCoord  = p3d_MultiTexCoord1;

  for (int i = 0; i < p3d_LightSource.length(); ++i) {
    vertexInShadowSpaces[i] = p3d_LightSource[i].shadowViewMatrix * vertexPosition;
  }

  gl_Position = p3d_ProjectionMatrix * vertexPosition;
}
