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
    float hash(float3 p)
    {
        (p = fract(((p * (float3)(0.31830990)) + (float3)(0.1))));
        (p *= (float3)(17.0));
        return fract((((p.x * p.y) * p.z) * ((p.x + p.y) + p.z)));
    };
    float noise(const float3 x)
    {
        float3 p = floor(x);
        float3 f = fract(x);
        (f = ((f * f) * ((float3)(3.0) - ((float3)(2.0) * f))));
        return mix(mix(mix(hash((p + float3(0, 0, 0))), hash((p + float3(1, 0, 0))), f.x), mix(hash((p + float3(0, 1, 0))), hash((p + float3(1, 1, 0))), f.x), f.y), mix(mix(hash((p + float3(0, 0, 1))), hash((p + float3(1, 0, 1))), f.x), mix(hash((p + float3(0, 1, 1))), hash((p + float3(1, 1, 1))), f.x), f.y), f.z);
    };
    struct VSOutput
    {
        float4 Position [[position]];
        float4 WorldPosition;
        float4 Normal;
        float4 UV;
        uint MatID;
    };
    struct PSOutput
    {
        float4 RedVarianceShadowMap [[color(0)]];
        float4 GreenVarianceShadowMap [[color(1)]];
        float4 BlueVarianceShadowMap [[color(2)]];
    };
    PSOutput main(VSOutput input)
    {
        Material mat = MaterialUniform.Materials[input.MatID];
        float4 matColor = (((mat.TextureFlags & (uint)(1)))?(MaterialTextures[mat.AlbedoTexID].sample(LinearSampler, input.UV.xy)):(mat.Color));
        float3 p = ((1.0) - mat.Transmission.xyz) * matColor.a;
        float e = noise((input.WorldPosition.xyz * (float3)(10000.0)));
        float3 normal = normalize(input.Normal.xyz);
        float3 ld = float3(CameraUniform.camViewMat[2][0], CameraUniform.camViewMat[2][1], CameraUniform.camViewMat[2][2]);
        float s = saturate(((mat.RefractionRatio - 1.0) * 0.5));
        float g = ((2.0 * saturate((1.0 - pow(dot(normalize(normal), (-ld.xyz)), ((128.0 * s) * s))))) - 1.0);
        (p = min(1.0, ((float3)((1.0 + (g * pow(s, 0.2)))) * p)));
        PSOutput output;
        float2 moments = ComputeMoments(input.Position.z);
        (output.RedVarianceShadowMap = float4(max(moments, (e > p.r)), 0.0, 0.0));
        (output.GreenVarianceShadowMap = float4(max(moments, (e > p.g)), 0.0, 0.0));
        (output.BlueVarianceShadowMap = float4(max(moments, (e > p.b)), 0.0, 0.0));
        return output;
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
    LinearSampler);
    return main.main(input0);
}
