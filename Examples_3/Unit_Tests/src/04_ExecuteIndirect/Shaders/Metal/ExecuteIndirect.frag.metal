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

#include <metal_stdlib>
using namespace metal;

#include "argument_buffers.h"

struct VsIn
{
    float4 position [[attribute(0)]];
    float4 normal [[attribute(1)]];
};

struct PsIn
{
    float4 position [[position]];
    float3 posModel;
    float3 normal;
    float4 albedo;
};

fragment float4 stageMain(
    PsIn In                       [[stage_in]],
    ExecuteIndirectArgData
)
{
    const float3 lightDir = -normalize(float3(2,6,1));

    float wrap_diffuse = saturate(dot(lightDir, normalize(In.normal)));
    float light = 2.0f * wrap_diffuse + 0.06f;

    float3 uvw = In.posModel * 0.5 + 0.5;

    float3 blendWeights = abs(normalize(In.posModel));
    blendWeights = saturate((blendWeights - 0.2) * 7);
    blendWeights /= float3(blendWeights.x + blendWeights.y + blendWeights.z);
    
    float3 coord1 = float3(uvw.yz, In.albedo.w);
    float3 coord2 = float3(uvw.zx, In.albedo.w);
    float3 coord3 = float3(uvw.xy, In.albedo.w);

    float3 texColor = float3(0,0,0);
    texColor += blendWeights.x * uTex0.sample(uSampler0, coord1.xy, uint(coord1.z)).xyz;
    texColor += blendWeights.y * uTex0.sample(uSampler0, coord2.xy, uint(coord2.z)).xyz;
    texColor += blendWeights.z * uTex0.sample(uSampler0, coord3.xy, uint(coord3.z)).xyz;
    
	float coverage = saturate(In.position.z * 4000.0f);

    float3 color = In.albedo.xyz;
    color *= light;
    color *= texColor * 2;
	color *= coverage;

    return float4(color, 1);
}
