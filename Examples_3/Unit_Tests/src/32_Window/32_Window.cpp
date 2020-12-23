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


//Interfaces
#include "../../../../Common_3/OS/Interfaces/ICameraController.h"
#include "../../../../Common_3/OS/Interfaces/IApp.h"
#include "../../../../Common_3/OS/Interfaces/ILog.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/OS/Interfaces/IFileSystem.h"
#include "../../../../Common_3/OS/Interfaces/IOperatingSystem.h"
#include "../../../../Common_3/OS/Interfaces/ITime.h"
#include "../../../../Common_3/OS/Interfaces/IProfiler.h"
#include "../../../../Middleware_3/UI/AppUI.h"
#include "../../../../Common_3/Renderer/IRenderer.h"
#include "../../../../Common_3/Renderer/IResourceLoader.h"

//Math
#include "../../../../Common_3/OS/Math/MathTypes.h"

#include "../../../../Common_3/OS/Interfaces/IMemory.h"

#define COUNT_OF(a) (sizeof(a) / sizeof(a[0]))

const uint32_t gImageCount = 3;

Renderer* pRenderer = NULL;

Queue*   pGraphicsQueue = NULL;
CmdPool* pCmdPools[gImageCount] = { NULL };
Cmd*     pCmds[gImageCount] = { NULL };

SwapChain*    pSwapChain = NULL;
Fence*        pRenderCompleteFences[gImageCount] = {};
Semaphore*    pImageAcquiredSemaphore = NULL;
Semaphore*    pRenderCompleteSemaphores[gImageCount] = {};

Shader* pBasicShader = NULL;
Pipeline* pBasicPipeline = NULL;

Buffer*   pQuadVertexBuffer = NULL;
Buffer*   pQuadIndexBuffer = NULL;

RootSignature* pRootSignature = NULL;
Sampler*       pSampler = NULL;
Texture*       pTexture = NULL;
DescriptorSet* pDescriptorSetTexture = NULL;

uint32_t gFrameIndex = 0;
ProfileToken gGpuProfileToken = PROFILE_INVALID_TOKEN;

enum WindowMode : int32_t
{
	WM_WINDOWED,
	WM_FULLSCREEN,
	WM_BORDERLESS
};

int32_t gWindowMode = WM_WINDOWED;

Timer gHideTimer;

// up to 30 monitors
int32_t gCurRes[30];
int32_t gLastRes[30];

int32_t gWndX;
int32_t gWndY;
int32_t gWndW;
int32_t gWndH;

bool gCursorHidden = false;
int32_t gCursorInsideWindow = 0;
bool gCursorClipped = false;
bool gMinimizeRequested = false;

/// UI
UIApp gAppUI;
GuiComponent* pStandaloneControlsGUIWindow = NULL;

TextDrawDesc gFrameTimeDraw = TextDrawDesc(0, 0xff00ffff, 18);

const char* gTestScripts[]	 = { "TestFullScreen.lua", "TestCenteredWindow.lua", "TestNonCenteredWindow.lua", "TestBorderless.lua", "TestHideWindow.lua" };
uint32_t gScriptIndexes[]	 = { 0, 1, 2, 3, 4 };
uint32_t gCurrentScriptIndex = 0;

IApp* pApp;

static bool ValidateWindowPos(int32_t x, int32_t y)
{
	WindowsDesc* winDesc = pApp->pWindow;
	int clientWidthStart = (getRectWidth(winDesc->windowedRect) - getRectWidth(winDesc->clientRect)) >> 1;
	int clientHeightStart = getRectHeight(winDesc->windowedRect) - getRectHeight(winDesc->clientRect) - clientWidthStart;

	if (winDesc->centered)
	{
		uint32_t fsHalfWidth = getRectWidth(winDesc->fullscreenRect) >> 1;
		uint32_t fsHalfHeight = getRectHeight(winDesc->fullscreenRect) >> 1;
		uint32_t windowWidth = getRectWidth(winDesc->clientRect);
		uint32_t windowHeight = getRectHeight(winDesc->clientRect);
		uint32_t windowHalfWidth = windowWidth >> 1;
		uint32_t windowHalfHeight = windowHeight >> 1;

		uint32_t X = fsHalfWidth - windowHalfWidth;
		uint32_t Y = fsHalfHeight - windowHalfHeight;

		if ((winDesc->windowedRect.left + clientWidthStart) != X || (winDesc->windowedRect.top + clientHeightStart) != Y)
			return false;
	}
	else
	{
		if (x != winDesc->windowedRect.left + clientWidthStart ||
			y != winDesc->windowedRect.top + clientHeightStart)
			return false;
	}

	return true;
}

static bool ValidateWindowSize(int32_t width, int32_t height)
{
	RectDesc clientRect = pApp->pWindow->clientRect;
	if (getRectWidth(clientRect) != width || getRectHeight(clientRect) != height)
		return false;
	return true;
}

static void SetWindowed()
{
	pApp->pWindow->maximized = false;

	int clientWidthStart = (getRectWidth(pApp->pWindow->windowedRect) - getRectWidth(pApp->pWindow->clientRect)) >> 1,
		clientHeightStart = getRectHeight(pApp->pWindow->windowedRect) - getRectHeight(pApp->pWindow->clientRect) - clientWidthStart;
	int32_t x = pApp->pWindow->windowedRect.left + clientWidthStart,
			y = pApp->pWindow->windowedRect.top + clientHeightStart,
			w = getRectWidth(pApp->pWindow->clientRect),
			h = getRectHeight(pApp->pWindow->clientRect);

	if (pApp->pWindow->fullScreen)
	{
		toggleFullscreen(pApp->pWindow);
		LOGF(LogLevel::eINFO, "SetWindowed() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetWindowed() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
	}
	if (pApp->pWindow->borderlessWindow)
	{
		toggleBorderless(pApp->pWindow, getRectWidth(pApp->pWindow->clientRect), getRectHeight(pApp->pWindow->clientRect));

		bool centered = pApp->pWindow->centered;
		pApp->pWindow->centered = false;
		LOGF(LogLevel::eINFO, "SetWindowed() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetWindowed() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
		pApp->pWindow->centered = centered;
	}
	gWindowMode = WindowMode::WM_WINDOWED;
}

static void SetFullscreen()
{
	if (!pApp->pWindow->fullScreen)
	{
		toggleFullscreen(pApp->pWindow);
		gWindowMode = WindowMode::WM_FULLSCREEN;
	}
}

static void SetBorderless()
{
	int clientWidthStart = (getRectWidth(pApp->pWindow->windowedRect) - getRectWidth(pApp->pWindow->clientRect)) >> 1,
		clientHeightStart = getRectHeight(pApp->pWindow->windowedRect) - getRectHeight(pApp->pWindow->clientRect) - clientWidthStart;
	int32_t x = pApp->pWindow->windowedRect.left + clientWidthStart,
			y = pApp->pWindow->windowedRect.top + clientHeightStart,
			w = getRectWidth(pApp->pWindow->clientRect),
			h = getRectHeight(pApp->pWindow->clientRect);

	if (pApp->pWindow->fullScreen)
	{
		toggleFullscreen(pApp->pWindow);
		gWindowMode = WindowMode::WM_WINDOWED;
		LOGF(LogLevel::eINFO, "SetBorderless() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetBorderless() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
	}
	else
	{
		gWindowMode = WindowMode::WM_BORDERLESS;
		toggleBorderless(pApp->pWindow, getRectWidth(pApp->pWindow->clientRect), getRectHeight(pApp->pWindow->clientRect));
		if(!pApp->pWindow->borderlessWindow)
			gWindowMode = WindowMode::WM_WINDOWED;

		bool centered = pApp->pWindow->centered;
		pApp->pWindow->centered = false;
		LOGF(LogLevel::eINFO, "SetBorderless() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetBorderless() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
		pApp->pWindow->centered = centered;
	}
}

static void MaximizeWindow()
{
	maximizeWindow(pApp->pWindow);
}

static void MinimizeWindow()
{
	gMinimizeRequested = true;
}

static void HideWindow()
{
	gHideTimer.Reset();
	hideWindow(pApp->pWindow);
}

static void ShowWindow()
{
	showWindow(pApp->pWindow);
}

static void UpdateResolution()
{
	uint32_t monitorCount = getMonitorCount();
	for (uint32_t i = 0; i < monitorCount; ++i)
	{
		if (gCurRes[i] != gLastRes[i])
		{
			MonitorDesc* monitor = getMonitor(i);

			int32_t resIndex = gCurRes[i];
			setResolution(monitor, &monitor->resolutions[resIndex]);

			gLastRes[i] = gCurRes[i];
		}
	}
}

static void MoveWindow()
{
	SetWindowed();
	int clientWidthStart = (getRectWidth(pApp->pWindow->windowedRect) - getRectWidth(pApp->pWindow->clientRect)) >> 1,
		clientHeightStart = getRectHeight(pApp->pWindow->windowedRect) - getRectHeight(pApp->pWindow->clientRect) - clientWidthStart;
	setWindowRect(pApp->pWindow, { gWndX, gWndY, gWndX + gWndW, gWndY + gWndH });
	LOGF(LogLevel::eINFO, "MoveWindow() Position check: %s", ValidateWindowPos(gWndX + clientWidthStart, gWndY + clientHeightStart) ? "SUCCESS" : "FAIL");
	LOGF(LogLevel::eINFO, "MoveWindow() Size check: %s", ValidateWindowSize(gWndW, gWndH) ? "SUCCESS" : "FAIL");
}

static void SetRecommendedWindowSize()
{
	SetWindowed();

	RectDesc rect;
	getRecommendedResolution(&rect);

	setWindowRect(pApp->pWindow, rect);
}

static void HideCursor2Sec()
{
	gCursorHidden = true;
	gHideTimer.Reset();
	hideCursor();
}

static void ToggleClipCursor()
{
	gCursorClipped = !gCursorClipped;
	setEnableCaptureInput(gCursorClipped);
}

static void RunScript()
{
	gAppUI.RunTestScript(gTestScripts[gCurrentScriptIndex]);
}

bool gTestGraphicsReset = false;
void testGraphicsReset()
{
	gTestGraphicsReset = !gTestGraphicsReset;
}
class WindowTest : public IApp
{
public:
	bool Init()
	{
		pApp = this;

		// FILE PATHS
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_SOURCES, "Shaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_DEBUG, RD_SHADER_BINARIES, "CompiledShaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_GPU_CONFIG, "GPUCfg");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_TEXTURES, "Textures");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_MESHES, "Meshes");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS, "Fonts");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_ANIMATIONS, "Animation");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SCRIPTS, "Scripts");


		if (!initInputSystem(pWindow))
			return false;

		// App Actions
		InputActionDesc actionDesc = { InputBindings::BUTTON_DUMP, [](InputActionContext* ctx) {  dumpProfileData(((Renderer*)ctx->pUserData), ((Renderer*)ctx->pUserData)->pName); return true; }, pRenderer };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_FULLSCREEN, [](InputActionContext* ctx)
				{
						if (gWindowMode == WM_FULLSCREEN || gWindowMode == WM_BORDERLESS)
						{
								gWindowMode = WM_WINDOWED;
								SetWindowed();
						}
						else if (gWindowMode == WM_WINDOWED)
						{
								gWindowMode = WM_FULLSCREEN;
								SetFullscreen();
						}
						return true;
				}, this };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_EXIT, [](InputActionContext* ctx) { requestShutdown(); return true; } };
		addInputAction(&actionDesc);
		actionDesc =
		{
				InputBindings::BUTTON_ANY, [](InputActionContext* ctx)
				{
						bool capture = gAppUI.OnButton(ctx->mBinding, ctx->mBool, ctx->pPosition);
						setEnableCaptureInput(capture && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);
						return true;
				}, this
		};
		addInputAction(&actionDesc);

		return true;
	}

	void Exit()
	{
		exitInputSystem();
	}

	bool Load()
	{
		if (mSettings.mResetGraphics || !pRenderer) 
		{
			// window and renderer setup
			RendererDesc settings = { 0 };
			initRenderer(GetName(), &settings, &pRenderer);
			//check for init success
			if (!pRenderer)
				return false;

			QueueDesc queueDesc = {};
			queueDesc.mType = QUEUE_TYPE_GRAPHICS;
			queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
			addQueue(pRenderer, &queueDesc, &pGraphicsQueue);

			for (uint32_t i = 0; i < gImageCount; ++i)
			{
				CmdPoolDesc cmdPoolDesc = {};
				cmdPoolDesc.pQueue = pGraphicsQueue;
				addCmdPool(pRenderer, &cmdPoolDesc, &pCmdPools[i]);
				CmdDesc cmdDesc = {};
				cmdDesc.pPool = pCmdPools[i];
				addCmd(pRenderer, &cmdDesc, &pCmds[i]);

				addFence(pRenderer, &pRenderCompleteFences[i]);
				addSemaphore(pRenderer, &pRenderCompleteSemaphores[i]);
			}
			addSemaphore(pRenderer, &pImageAcquiredSemaphore);

			initResourceLoaderInterface(pRenderer);

			TextureLoadDesc textureDesc = {};
			textureDesc.pFileName = "skybox/hw_sahara/sahara_bk";
			textureDesc.ppTexture = &pTexture;
			addResource(&textureDesc, NULL);

			ShaderLoadDesc basicShader = {};
			basicShader.mStages[0] = { "basic.vert", NULL, 0 };
			basicShader.mStages[1] = { "basic.frag", NULL, 0 };
			addShader(pRenderer, &basicShader, &pBasicShader);

			SamplerDesc samplerDesc = {};
			samplerDesc.mAddressU = ADDRESS_MODE_REPEAT;
			samplerDesc.mAddressV = ADDRESS_MODE_REPEAT;
			samplerDesc.mAddressW = ADDRESS_MODE_REPEAT;
			samplerDesc.mMinFilter = FILTER_LINEAR;
			samplerDesc.mMagFilter = FILTER_LINEAR;
			samplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;
			addSampler(pRenderer, &samplerDesc, &pSampler);

			Shader*           shaders[] = { pBasicShader };
			const char*       pStaticSamplers[] = { "Sampler" };
			RootSignatureDesc rootDesc = {};
			rootDesc.mStaticSamplerCount = 1;
			rootDesc.ppStaticSamplerNames = pStaticSamplers;
			rootDesc.ppStaticSamplers = &pSampler;
			rootDesc.mShaderCount = COUNT_OF(shaders);
			rootDesc.ppShaders = shaders;
			addRootSignature(pRenderer, &rootDesc, &pRootSignature);

			DescriptorSetDesc desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
			addDescriptorSet(pRenderer, &desc, &pDescriptorSetTexture);

			float vertices[] =
			{
					 1.0f,  1.0f, 0.0f, 1.0f,  1.0f, 0.0f, // 0 top_right 
					-1.0f,  1.0f, 0.0f, 1.0f,  0.0f, 0.0f, // 1 top_left
					-1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 1.0f, // 2 bot_left
					 1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 1.0f, // 3 bot_right
			};

			uint32_t indices[] =
			{
					0, 1, 2, 0, 2, 3
			};

			BufferLoadDesc vbDesc = {};
			vbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
			vbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
			vbDesc.mDesc.mSize = sizeof(vertices);
			vbDesc.pData = vertices;
			vbDesc.ppBuffer = &pQuadVertexBuffer;
			addResource(&vbDesc, NULL);

			BufferLoadDesc ibDesc = {};
			ibDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
			ibDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
			ibDesc.mDesc.mSize = sizeof(indices);
			ibDesc.pData = indices;
			ibDesc.ppBuffer = &pQuadIndexBuffer;
			addResource(&ibDesc, NULL);

			if (!gAppUI.Init(pRenderer))
				return false;
			gAppUI.AddTestScripts(gTestScripts, COUNT_OF(gTestScripts));

			gAppUI.LoadFont("TitilliumText/TitilliumText-Bold.otf");

			const TextDrawDesc UIPanelWindowTitleTextDesc = { 0, 0xffff00ff, 16 };

			float   dpiScale = getDpiScale().x;
			vec2    UIPosition = { mSettings.mWidth * 0.01f, mSettings.mHeight * 0.30f };
			vec2    UIPanelSize = vec2(1000.f, 1000.f) / dpiScale;
			GuiDesc guiDesc(UIPosition, UIPanelSize, UIPanelWindowTitleTextDesc);
			pStandaloneControlsGUIWindow = gAppUI.AddGuiComponent("Window", &guiDesc);

			RadioButtonWidget rbWindowed("Windowed", &gWindowMode, WM_WINDOWED);
			RadioButtonWidget rbFullscreen("Fullscreen", &gWindowMode, WM_FULLSCREEN);
			RadioButtonWidget rbBorderless("Borderless", &gWindowMode, WM_BORDERLESS);

			rbWindowed.pOnEdited = SetWindowed;
			rbFullscreen.pOnEdited = SetFullscreen;
			rbBorderless.pOnEdited = SetBorderless;
			rbWindowed.mDeferred = true;
			rbFullscreen.mDeferred = true;
			rbBorderless.mDeferred = true;

			ButtonWidget bMaximize("Maximize");
			ButtonWidget bMinimize("Minimize");

			bMaximize.pOnEdited = MaximizeWindow;
			bMinimize.pOnEdited = MinimizeWindow;
			bMaximize.mDeferred = true;
			bMinimize.mDeferred = true;

			ButtonWidget bHide("Hide for 2s");
			bHide.pOnEdited = HideWindow;

			CheckboxWidget rbCentered("Centered", &(pApp->pWindow->centered));

			RectDesc recRes;
			getRecommendedResolution(&recRes);

			uint32_t recWidth = recRes.right - recRes.left;
			uint32_t recHeight = recRes.bottom - recRes.top;

			SliderIntWidget setRectSliderX("x", &gWndX, 0, recWidth);
			SliderIntWidget setRectSliderY("y", &gWndY, 0, recHeight);
			SliderIntWidget setRectSliderW("w", &gWndW, 1, recWidth);
			SliderIntWidget setRectSliderH("h", &gWndH, 1, recHeight);

			ButtonWidget bSetRect("Set window rectangle");
			ButtonWidget bRecWndSize("Set recommended window rectangle");

			bSetRect.pOnEdited = MoveWindow;
			bRecWndSize.pOnEdited = SetRecommendedWindowSize;
			bSetRect.mDeferred = true;
			bRecWndSize.mDeferred = true;

						// Reset graphics with a button.
			ButtonWidget testGPUReset("ResetGraphicsDevice");
			testGPUReset.pOnEdited = testGraphicsReset;
			pStandaloneControlsGUIWindow->AddWidget(testGPUReset);

			pStandaloneControlsGUIWindow->AddWidget(rbWindowed);
			pStandaloneControlsGUIWindow->AddWidget(rbFullscreen);
			pStandaloneControlsGUIWindow->AddWidget(rbBorderless);
			pStandaloneControlsGUIWindow->AddWidget(bMaximize);
			pStandaloneControlsGUIWindow->AddWidget(bMinimize);
			pStandaloneControlsGUIWindow->AddWidget(bHide);
			pStandaloneControlsGUIWindow->AddWidget(rbCentered);
			pStandaloneControlsGUIWindow->AddWidget(setRectSliderX);
			pStandaloneControlsGUIWindow->AddWidget(setRectSliderY);
			pStandaloneControlsGUIWindow->AddWidget(setRectSliderW);
			pStandaloneControlsGUIWindow->AddWidget(setRectSliderH);
			pStandaloneControlsGUIWindow->AddWidget(bSetRect);
			pStandaloneControlsGUIWindow->AddWidget(bRecWndSize);

			uint32_t numMonitors = getMonitorCount();
			pStandaloneControlsGUIWindow->AddWidget(LabelWidget(
				eastl::string("Number of displays: ") + eastl::to_string(numMonitors)));

			for (uint32_t i = 0; i < numMonitors; ++i)
			{
				MonitorDesc* monitor = getMonitor(i);

				char publicDisplayName[128];
#if defined(_WINDOWS) || defined(XBOX) // Win platform uses wide chars			
				if (128 == wcstombs(publicDisplayName, monitor->publicDisplayName, sizeof(publicDisplayName)))
					publicDisplayName[127] = '\0';
#else
				strcpy(publicDisplayName, monitor->publicDisplayName);
#endif

				CollapsingHeaderWidget monitorHeader(
					eastl::string(publicDisplayName) + " (" +
					eastl::to_string(monitor->physicalSize.x) + 'x' + eastl::to_string(monitor->physicalSize.y) + " mm; " +
					eastl::to_string(monitor->dpi.x) + " dpi; " +
					eastl::to_string(monitor->resolutionCount) + " resolutions)");

				for (uint32_t j = 0; j < monitor->resolutionCount; ++j)
				{
					Resolution res = monitor->resolutions[j];

					eastl::string strRes = eastl::to_string(res.mWidth) + 'x' + eastl::to_string(res.mHeight);
					if (monitor->defaultResolution.mWidth == res.mWidth &&
						monitor->defaultResolution.mHeight == res.mHeight)
					{
						strRes += " (native)";
						gCurRes[i] = j;
						gLastRes[i] = j;
					}

					RadioButtonWidget rbRes(strRes.c_str(), &gCurRes[i], j);
					rbRes.pOnEdited = UpdateResolution;
					rbRes.mDeferred = false;

					monitorHeader.AddSubWidget(rbRes);
				}

				pStandaloneControlsGUIWindow->AddWidget(monitorHeader);
			}

			CollapsingHeaderWidget InputCotrolsWidget("Cursor");

			ButtonWidget bHideCursor("Hide Cursor for 2s");
			bHideCursor.pOnEdited = HideCursor2Sec;
			InputCotrolsWidget.AddSubWidget(bHideCursor);

			LabelWidget lCursorInWindow("Cursor inside window?");
			InputCotrolsWidget.AddSubWidget(lCursorInWindow);

			RadioButtonWidget rCursorInsideRectFalse("No", &gCursorInsideWindow, 0);
			InputCotrolsWidget.AddSubWidget(rCursorInsideRectFalse);
			RadioButtonWidget rCursorInsideRectTrue("Yes", &gCursorInsideWindow, 1);
			InputCotrolsWidget.AddSubWidget(rCursorInsideRectTrue);

			ButtonWidget bClipCursor("Clip Cursor to Window");
			bClipCursor.pOnEdited = ToggleClipCursor;
			InputCotrolsWidget.AddSubWidget(bClipCursor);

			pStandaloneControlsGUIWindow->AddWidget(InputCotrolsWidget);

			DropdownWidget ddTestScripts("Test Scripts", &gCurrentScriptIndex, gTestScripts, gScriptIndexes, COUNT_OF(gTestScripts));
			ButtonWidget bRunScript("Run");
			bRunScript.pOnEdited = RunScript;

			pStandaloneControlsGUIWindow->AddWidget(ddTestScripts);
			pStandaloneControlsGUIWindow->AddWidget(bRunScript);

			// Initialize microprofiler and it's UI.
			initProfiler();
			initProfilerUI(&gAppUI, mSettings.mWidth, mSettings.mHeight);

			// Gpu profiler can only be added after initProfile.
			gGpuProfileToken = addGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
			waitForAllResourceLoads();
		}

		if (!addSwapChain())
			return false;

		if (!gAppUI.Load(pSwapChain->ppRenderTargets))
			return false;

		//layout and pipeline for sphere draw
		VertexLayout vertexLayout = {};
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_TEXCOORD0;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_BACK;

		DepthStateDesc depthStateDesc = {};

		PipelineDesc desc = {};
		desc.mType = PIPELINE_TYPE_GRAPHICS;
		GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.pRootSignature = pRootSignature;
		pipelineSettings.pShaderProgram = pBasicShader;
		pipelineSettings.pVertexLayout = &vertexLayout;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		addPipeline(pRenderer, &desc, &pBasicPipeline);

		// Prepare descriptor sets
		DescriptorData params[2] = {};
		params[0].pName = "Texture";
		params[0].ppTextures = &pTexture;
		updateDescriptorSet(pRenderer, 0, pDescriptorSetTexture, 1, params);

		waitForAllResourceLoads();

		return true;
	}

	void Unload()
	{
		waitQueueIdle(pGraphicsQueue);

		gAppUI.Unload();

		removePipeline(pRenderer, pBasicPipeline);
		removeSwapChain(pRenderer, pSwapChain);

		if (mSettings.mResetGraphics || mSettings.mQuit) 
		{
			exitProfilerUI();

			exitProfiler();

			gAppUI.Exit();

			removeDescriptorSet(pRenderer, pDescriptorSetTexture);

			removeResource(pQuadVertexBuffer);
			removeResource(pQuadIndexBuffer);
			removeResource(pTexture);

			removeSampler(pRenderer, pSampler);
			removeShader(pRenderer, pBasicShader);
			removeRootSignature(pRenderer, pRootSignature);

			for (uint32_t i = 0; i < gImageCount; ++i)
			{
				removeFence(pRenderer, pRenderCompleteFences[i]);
				removeSemaphore(pRenderer, pRenderCompleteSemaphores[i]);

				removeCmd(pRenderer, pCmds[i]);
				removeCmdPool(pRenderer, pCmdPools[i]);
			}
			removeSemaphore(pRenderer, pImageAcquiredSemaphore);

			exitResourceLoaderInterface(pRenderer);
			removeQueue(pRenderer, pGraphicsQueue);
			removeRenderer(pRenderer);
		}
	}

	void Update(float deltaTime)
	{
		updateInputSystem(mSettings.mWidth, mSettings.mHeight);

		if (pWindow->hide || gCursorHidden)
		{
			unsigned msec = gHideTimer.GetMSec(false);
			if (msec >= 2000)
			{
				ShowWindow();
				showCursor();
				gCursorHidden = false;
			}
		}

		gCursorInsideWindow = isCursorInsideTrackingArea();

		gAppUI.Update(deltaTime);
	}

	void Draw()
	{
		uint32_t swapchainImageIndex;
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
		Semaphore*    pRenderCompleteSemaphore = pRenderCompleteSemaphores[gFrameIndex];
		Fence*        pRenderCompleteFence = pRenderCompleteFences[gFrameIndex];

		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, pRenderCompleteFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(pRenderer, 1, &pRenderCompleteFence);

		// Reset cmd pool for this frame
		resetCmdPool(pRenderer, pCmdPools[gFrameIndex]);

		Cmd* cmd = pCmds[gFrameIndex];
		beginCmd(cmd);

		cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

		const uint32_t stride = 6 * sizeof(float);

		RenderTargetBarrier barriers[2];

		barriers[0] = { pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0].r = 0.0f;
		loadActions.mClearColorValues[0].g = 0.0f;
		loadActions.mClearColorValues[0].b = 0.0f;
		loadActions.mClearColorValues[0].a = 0.0f;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Basic Draw");
		cmdBindPipeline(cmd, pBasicPipeline);
		cmdBindDescriptorSet(cmd, 0, pDescriptorSetTexture);
		cmdBindVertexBuffer(cmd, 1, &pQuadVertexBuffer, &stride, NULL);
		cmdBindIndexBuffer(cmd, pQuadIndexBuffer, INDEX_TYPE_UINT32, 0);
		cmdDrawIndexed(cmd, 6, 0, 0);
		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

		loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_LOAD;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);
		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

		const float txtIndent = 8.f;
		float2 txtSizePx = cmdDrawCpuProfile(cmd, float2(txtIndent, 15.f), &gFrameTimeDraw);
		cmdDrawGpuProfile(cmd, float2(txtIndent, txtSizePx.y + 30.f), gGpuProfileToken, &gFrameTimeDraw);

		cmdDrawProfilerUI();

		gAppUI.Gui(pStandaloneControlsGUIWindow);
		gAppUI.Draw(cmd);
		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

		barriers[0] = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		endCmd(cmd);

		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = 1;
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &pRenderCompleteSemaphore;
		submitDesc.ppWaitSemaphores = &pImageAcquiredSemaphore;
		submitDesc.pSignalFence = pRenderCompleteFence;
		queueSubmit(pGraphicsQueue, &submitDesc);

		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = swapchainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.ppWaitSemaphores = &pRenderCompleteSemaphore;
		presentDesc.mSubmitDone = true;
		PresentStatus presentStatus = queuePresent(pGraphicsQueue, &presentDesc);
		flipProfiler();

		if (presentStatus == PRESENT_STATUS_DEVICE_RESET)
		{
			Thread::Sleep(5000);// Wait for a few seconds to allow the driver to come back online before doing a reset.
			mSettings.mResetGraphics = true;
		}

		// Test re-creating graphics resources mid app.
		if (gTestGraphicsReset)
		{
			mSettings.mResetGraphics = true;
			gTestGraphicsReset = false;
		}

		gFrameIndex = (gFrameIndex + 1) % gImageCount;

		if (gMinimizeRequested)
		{
			minimizeWindow(pApp->pWindow);
			gMinimizeRequested = false;
		}
	}

	const char* GetName() { return "32_Window"; }

	bool addSwapChain()
	{
		SwapChainDesc swapChainDesc = {};
		swapChainDesc.mWindowHandle = pWindow->handle;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.ppPresentQueues = &pGraphicsQueue;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = gImageCount;
		swapChainDesc.mColorFormat = getRecommendedSwapchainFormat(true);
		swapChainDesc.mEnableVsync = mSettings.mDefaultVSyncEnabled;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}
};
DEFINE_APPLICATION_MAIN(WindowTest)

