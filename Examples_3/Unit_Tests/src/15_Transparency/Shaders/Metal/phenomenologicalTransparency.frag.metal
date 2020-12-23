/*
 * Copyright (c) 2018-2021 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
*/

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
        float4 matColor = (((mat.TextureFlags & (uint)(1)))?(MaterialTextures[mat.AlbedoTexID].sample(LinearSampler, uv)):(mat.Color));
        float3 viewVec = normalize((worldPos - CameraUniform.camPosition.xyz));
        if ((nDotl < 0.05))
        {
            (nDotl = 0.05);
        }
        float3 diffuse = ((LightUniformBlock.lightColor.xyz * matColor.xyz) * (float3)(nDotl));
        float3 specular = (LightUniformBlock.lightColor.xyz * (float3)(pow(saturate(dot(reflect((-LightUniformBlock.lightDirection.xyz), normal), viewVec)), SPECULAR_EXP)));
        float3 finalColor = saturate((diffuse + (specular * (float3)(0.5))));
#if USE_SHADOWS!=0
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
#if (PT_USE_DIFFUSION + PT_USE_REFRACTION) != 0
        float4 CSPosition;
#endif
#if PT_USE_REFRACTION != 0
        float4 CSNormal;
#endif
    };
    struct PSOutput
    {
        float4 Accumulation [[color(0)]];
        float4 Modulation [[color(1)]];
#if PT_USE_REFRACTION != 0
        float4 Refraction [[color(2)]];
#endif
    };
#if PT_USE_DIFFUSION!=0
    constant Uniforms_ObjectUniformBlock & ObjectUniformBlock;
    texture2d<float> DepthTexture;
    sampler PointSampler;
#endif

    float WeightFunction(float alpha, float depth)
    {
        float tmp = (1.0 - (depth * 0.99));
        (tmp *= ((tmp * tmp) * 10000.0));
        return clamp((alpha * tmp), 0.0010000000, 150.0);
    };
    float2 ComputeRefractionOffset(float3 csNormal, float3 csPosition, float eta)
    {
        const float2 backSizeInMeters = (1000.0 * (1.0 / ((1.0 - eta) * 100.0)));
        const float backgroundZ = (csPosition.z - 4.0);
        float3 dir = normalize(csPosition);
        float3 refracted = refract(dir, csNormal, eta);
        bool totalInternalRefraction = (dot(refracted, refracted) < 0.010000000);
        if (totalInternalRefraction)
        {
            return 0.0;
        }
        else
        {
            float3 plane = csPosition;
            (plane.z -= backgroundZ);
            float2 hit = (plane.xy - ((refracted.xy * (float2)(plane.z)) / (float2)(refracted.z)));
            float2 backCoord = ((hit / backSizeInMeters) + (float2)(0.5));
            float2 startCoord = ((csPosition.xy / backSizeInMeters) + (float2)(0.5));
            return (backCoord - startCoord);
        }
    };
    PSOutput main(VSOutput input)
    {
        PSOutput output;
        float3 transmission = MaterialUniform.Materials[input.MatID].Transmission.xyz;
        float collimation = MaterialUniform.Materials[input.MatID].Collimation;
        float4 finalColor = Shade(input.MatID, input.UV.xy, input.WorldPosition.xyz, normalize(input.Normal.xyz));
        float d = (input.Position.z / input.Position.w);
        float4 premultipliedColor = float4((finalColor.rgb * (float3)(finalColor.a)), finalColor.a);
        float coverage = finalColor.a;
        (output.Modulation.rgb = ((float3)(coverage) * ((float3)(1.0) - transmission)));
        (coverage *= (1.0 - (((transmission.r + transmission.g) + transmission.b) * (1.0 / 3.0))));
        float w = WeightFunction(coverage, d);
        (output.Accumulation = (float4(premultipliedColor.rgb, coverage) * (float4)(w)));
#if PT_USE_DIFFUSION!=0
        if ((collimation < 1.0))
        {
            float backgroundDepth = DepthTexture.read(uint2(input.Position.xy), 0).r;
            (backgroundDepth = (CameraUniform.camClipInfo[0] / ((CameraUniform.camClipInfo[1] * backgroundDepth) + CameraUniform.camClipInfo[2])));
            const float scaling = 8.0;
            const float focusRate = 0.1;
            (output.Modulation.a = ((((scaling * coverage) * (1.0 - collimation)) * (1.0 - (focusRate / ((focusRate + input.CSPosition.z) - backgroundDepth)))) / max(abs(input.CSPosition.z), 0.000010000000)));
            (output.Modulation.a *= output.Modulation.a);
            (output.Modulation.a = max(output.Modulation.a, (1.0 / 256.0)));
        }
        else
        {
            (output.Modulation.a = 0.0);
        }
#else
        (output.Modulation.a = 0.0);
#endif

#if PT_USE_REFRACTION!=0
        float eta = (1.0 / MaterialUniform.Materials[input.MatID].RefractionRatio);
        float2 refractionOffset = 0.0;
        if ((eta != 1.0))
        {
            (refractionOffset = ComputeRefractionOffset(normalize(input.CSNormal.xyz), input.CSPosition.xyz, eta));
        }
        (output.Refraction = float4(((refractionOffset * (float2)(coverage)) * (float2)(8.0)), 0.0, 0.0));
#endif

        return output;
    };

    Fragment_Shader(
#if USE_SHADOWS!=0
texture2d<float> VSM,
sampler VSMSampler,
#if PT_USE_CAUSTICS!=0
texture2d<float> VSMRed,
texture2d<float> VSMGreen,
texture2d<float> VSMBlue,
#endif
#endif
constant Uniforms_LightUniformBlock & LightUniformBlock,
constant Uniforms_CameraUniform & CameraUniform,
constant Uniforms_MaterialUniform & MaterialUniform,
constant texture2d<float, access::sample>* MaterialTextures,
sampler LinearSampler
#if PT_USE_DIFFUSION!=0
,
constant Uniforms_ObjectUniformBlock & ObjectUniformBlock,
texture2d<float> DepthTexture,
sampler PointSampler
#endif
) :

#if USE_SHADOWS!=0
VSM(VSM),VSMSampler(VSMSampler),
#if PT_USE_CAUSTICS!=0
VSMRed(VSMRed),VSMGreen(VSMGreen),VSMBlue(VSMBlue),
#endif

#endif
LightUniformBlock(LightUniformBlock),CameraUniform(CameraUniform),MaterialUniform(MaterialUniform),MaterialTextures(MaterialTextures),LinearSampler(LinearSampler)
#if PT_USE_DIFFUSION!=0
    ,
    ObjectUniformBlock(ObjectUniformBlock),
    DepthTexture(DepthTexture),
    PointSampler(PointSampler)
#endif
 {}
};

fragment Fragment_Shader::PSOutput stageMain(
    Fragment_Shader::VSOutput input [[stage_in]],
	DECLARE_ARG_DATA()
)
{
    Fragment_Shader::VSOutput input0;
    input0.Position = float4(input.Position.xyz, 1.0 / input.Position.w);
    input0.WorldPosition = input.WorldPosition;
    input0.Normal = input.Normal;
    input0.UV = input.UV;
    input0.MatID = input.MatID;
#if (PT_USE_REFRACTION + PT_USE_DIFFUSION) != 0
    input0.CSPosition = input.CSPosition;
#endif
#if PT_USE_REFRACTION != 0
    input0.CSNormal = input.CSNormal;
#endif
	
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
#if PT_USE_DIFFUSION!=0
,
	fsDataPerFrame.ObjectUniformBlock,
	DepthTexture,
	PointSampler
#endif
	);
    return main.main(input0);
}
