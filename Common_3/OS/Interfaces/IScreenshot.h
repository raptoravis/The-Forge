/*
 * Copyright (c) 2018-2020 The Forge Interactive Inc.
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

#pragma once
#include "../../Renderer/IRenderer.h"

// Flip R and B channel by default or not.
#if defined(_WINDOWS) || defined(ORBIS) || defined(XBOX) || defined(METAL)
#define FLIP_REDBLUE_CHANNEL true
#else
#define FLIP_REDBLUE_CHANNEL false
#endif

void initScreenshotInterface(Renderer* pRenderer, Queue* pQueue);
// Use one renderpass prior to calling captureScreenshot() to prepare pSwapChain for copy.
bool prepareScreenshot(SwapChain* pSwapChain);
void captureScreenshot(SwapChain* pSwapChain, uint32_t swapChainRtIndex, ResourceState renderTargetCurrentState, const char* pngFileName, bool flipRedBlueChannel = FLIP_REDBLUE_CHANNEL);
void exitScreenshotInterface();
