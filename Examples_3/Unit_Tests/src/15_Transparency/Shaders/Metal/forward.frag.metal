/* Write your header comments here */
#include <metal_stdlib>
using namespace metal;

#include "argument_buffers.h"

struct Fragment_Shader
{
#define SPECULAR_EXP 10.0
#if USE_SHADOWS!=0
    texture2d<float> VSM;
    sampler VSMSampler;
#if PT_USE_CAUSTICS!=0
    texture2d<float> VSMRed;
    texture2d<float> VSMGreen;
    texture2d<float> VSMBlue;
#endif

    float2 ComputeMoments(float depth)
    {
        float2 moments;
        (moments.x = depth);
        float2 pd = float2(dfdx(depth), dfdy(depth));
        (moments.y = ((depth * depth) + (0.25 * dot(pd, pd))));
        return moments;
    };
    float ChebyshevUpperBound(float2 moments, float t)
    {
        float p = (t <= moments.x);
        float variance = (moments.y - (moments.x * moments.x));
        (variance = max(variance, 0.0010000000));
        float d = (t - moments.x);
        float pMax = (variance / (variance + (d * d)));
        return max(p, pMax);
    };
    float3 ShadowContribution(float2 shadowMapPos, float distanceToLight)
    {
        float2 moments = VSM.sample(VSMSampler, shadowMapPos).xy;
        float3 shadow = ChebyshevUpperBound(moments, distanceToLight);
#if PT_USE_CAUSTICS!=0
        (moments = (float2)(VSMRed.sample(VSMSampler, shadowMapPos).xy));
        (shadow.r *= ChebyshevUpperBound(moments, distanceToLight));
        (moments = (float2)(VSMGreen.sample(VSMSampler, shadowMapPos).xy));
        (shadow.g *= ChebyshevUpperBound(moments, distanceToLight));
        (moments = (float2)(VSMBlue.sample(VSMSampler, shadowMapPos).xy));
        (shadow.b *= ChebyshevUpperBound(moments, distanceToLight));
#endif

        return shadow;
    };
#endif

    constant Uniforms_LightUniformBlock & LightUniformBlock;
    constant Uniforms_CameraUniform & CameraUniform;
    constant Uniforms_MaterialUniform & MaterialUniform;

    constant texture2d<float, access::sample>* MaterialTextures;
    sampler LinearSampler;
    float4 Shade(uint matID, float2 uv, float3 worldPos, float3 normal)
    {
        float nDotl = dot(normal, (-LightUniformBlock.lightDirection.xyz));
        Material mat = MaterialUniform.Materials[matID];
		float4 texColor = MaterialTextures[mat.AlbedoTexID].sample(LinearSampler, uv);
        float4 matColor = ((mat.TextureFlags & 1) ? texColor : mat.Color);

        float3 viewVec = normalize((worldPos - CameraUniform.camPosition.xyz));
        if ((nDotl < 0.05))
        {
            (nDotl = 0.05);
        }
        float3 diffuse = ((LightUniformBlock.lightColor.xyz * matColor.xyz) * (float3)(nDotl));
        float3 specular = (LightUniformBlock.lightColor.xyz * (float3)(pow(saturate(dot(reflect((-LightUniformBlock.lightDirection.xyz), normal), viewVec)), SPECULAR_EXP)));
        float3 finalColor = saturate((diffuse + (specular * (float3)(0.5))));
#if USE_SHADOWS != 0
        float4 shadowMapPos = ((LightUniformBlock.lightViewProj)*(float4(worldPos, 1.0)));
        (shadowMapPos.y = (-shadowMapPos.y));
        (shadowMapPos.xy = ((shadowMapPos.xy + (float2)(1.0)) * (float2)(0.5)));
        if ((((clamp(shadowMapPos.x, 0.01, 0.99) == shadowMapPos.x) && (clamp(shadowMapPos.y, 0.01, 0.99) == shadowMapPos.y)) && (shadowMapPos.z > 0.0)))
        {
            float3 lighting = ShadowContribution(shadowMapPos.xy, shadowMapPos.z);
            (finalColor *= lighting);
        }
#endif

        return float4(finalColor, matColor.a);
    };
    struct VSOutput
    {
        float4 Position [[position]];
        float4 WorldPosition;
        float4 Normal;
        float4 UV;
        uint MatID;
    };
    float4 main(VSOutput input)
    {
        return Shade(input.MatID, input.UV.xy, input.WorldPosition.xyz, normalize(input.Normal.xyz));
    };

    Fragment_Shader(

#if USE_SHADOWS!=0
texture2d<float> VSM,sampler VSMSampler,
#if PT_USE_CAUSTICS!=0
texture2d<float> VSMRed,texture2d<float> VSMGreen,texture2d<float> VSMBlue,
#endif
#endif
constant Uniforms_LightUniformBlock & LightUniformBlock,constant Uniforms_CameraUniform & CameraUniform,constant Uniforms_MaterialUniform & MaterialUniform,constant texture2d<float, access::sample>* MaterialTextures,sampler LinearSampler) :

#if USE_SHADOWS!=0
VSM(VSM),VSMSampler(VSMSampler),
#if PT_USE_CAUSTICS!=0
VSMRed(VSMRed),VSMGreen(VSMGreen),VSMBlue(VSMBlue),
#endif

#endif
LightUniformBlock(LightUniformBlock),CameraUniform(CameraUniform),MaterialUniform(MaterialUniform),MaterialTextures(MaterialTextures),LinearSampler(LinearSampler) {}
};

fragment float4 stageMain(
    Fragment_Shader::VSOutput input                 [[stage_in]],
    DECLARE_ARG_DATA()
)
{
    Fragment_Shader::VSOutput input0;
    input0.Position = float4(input.Position.xyz, 1.0 / input.Position.w);
    input0.WorldPosition = input.WorldPosition;
    input0.Normal = input.Normal;
    input0.UV = input.UV;
    input0.MatID = input.MatID;
    Fragment_Shader main(
#if USE_SHADOWS!=0
	VSM,
	VSMSampler,
#if PT_USE_CAUSTICS!=0
	VSMRed,
	VSMGreen,
	VSMBlue,
#endif
#endif
	fsDataPerFrame.LightUniformBlock,
	fsDataPerFrame.CameraUniform,
	fsDataPerFrame.MaterialUniform,
	fsData.MaterialTextures,
	LinearSampler
	);
    return main.main(input0);
}
