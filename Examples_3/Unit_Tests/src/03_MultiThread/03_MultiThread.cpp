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

#define _USE_MATH_DEFINES

//tiny stl
#include "../../../../Common_3/ThirdParty/OpenSource/EASTL/vector.h"
#include "../../../../Common_3/ThirdParty/OpenSource/EASTL/string.h"

#include "../../../../Common_3/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"

//Interfaces
#include "../../../../Common_3/OS/Interfaces/ICameraController.h"
#include "../../../../Common_3/OS/Interfaces/ILog.h"
#include "../../../../Common_3/OS/Interfaces/IFileSystem.h"
#include "../../../../Common_3/OS/Interfaces/ITime.h"
#include "../../../../Common_3/OS/Interfaces/IThread.h"
#include "../../../../Common_3/OS/Interfaces/IProfiler.h"

#include "../../../../Middleware_3/UI/AppUI.h"
#include "../../../../Common_3/OS/Interfaces/IApp.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/OS/Math/MathTypes.h"
#include "../../../../Common_3/OS/Core/ThreadSystem.h"


// for cpu usage query
#if defined(_WIN32)
#if defined(_DURANGO)
#else
#include <Windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "comsuppw.lib")
#endif
#elif defined(__linux__)
#include <unistd.h>    // sysconf(), _SC_NPROCESSORS_ONLN
#elif defined(NX64)
//todo
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#endif

#include "../../../../Common_3/Renderer/IRenderer.h"
#include "../../../../Common_3/Renderer/IResourceLoader.h"

#include "../../../../Common_3/OS/Interfaces/IMemory.h"

// startdust hash function, use this to generate all the seed and update the position of all particles
#define RND_GEN(x) (x = x * 196314165 + 907633515)

struct ParticleData
{
	float    mPaletteFactor;
	uint32_t mData;
	uint32_t mTextureIndex;
};

struct ThreadData
{
	CmdPool*          pCmdPool;
	Cmd**             ppCmds;
	RenderTarget*     pRenderTarget;
	int               mStartPoint;
	int               mDrawCount;
    int               mThreadIndex;
    ThreadID          mThreadID;
	uint32_t          mFrameIndex;
};

struct ObjectProperty
{
	float mRotX = 0, mRotY = 0;
} gObjSettings;

const uint32_t gSampleCount = 60;
const uint32_t gImageCount = 3;

struct CpuGraphData
{
	int   mSampleIdx;
	float mSample[gSampleCount];
	float mSampley[gSampleCount];
	float mScale;
	int   mEmptyFlag;
};

struct ViewPortState
{
	float mOffsetX;
	float mOffsetY;
	float mWidth;
	float mHeight;
};

struct GraphVertex
{
	vec2 mPosition;
	vec4 mColor;
};

struct CpuGraph
{
	Buffer*       mVertexBuffer[gImageCount];    // vetex buffer for cpu sample
	ViewPortState mViewPort;                     //view port for different core
};

const int gTotalParticleCount = 2000000;
uint32_t  gGraphWidth = 200;
uint32_t  gGraphHeight = 100;

Renderer* pRenderer = NULL;

Queue*   pGraphicsQueue = NULL;
CmdPool* pCmdPool = NULL;
Cmd**    ppCmds = NULL;
CmdPool* pGraphCmdPool = NULL;
Cmd**    ppGraphCmds = NULL;

Fence*     pRenderCompleteFences[gImageCount] = { NULL };
Semaphore* pImageAcquiredSemaphore = NULL;
Semaphore* pRenderCompleteSemaphores[gImageCount] = { NULL };

SwapChain* pSwapChain = NULL;

Shader*        pShader = NULL;
Shader*        pSkyBoxDrawShader = NULL;
Shader*        pGraphShader = NULL;
Buffer*        pParticleVertexBuffer = NULL;
Buffer*        pProjViewUniformBuffer[gImageCount] = { NULL };
Buffer*        pSkyboxUniformBuffer[gImageCount] = { NULL };
Buffer*        pSkyBoxVertexBuffer = NULL;
Buffer*        pBackGroundVertexBuffer[gImageCount] = { NULL };
Pipeline*      pPipeline = NULL;
Pipeline*      pSkyBoxDrawPipeline = NULL;
Pipeline*      pGraphLinePipeline = NULL;
Pipeline*      pGraphLineListPipeline = NULL;
Pipeline*      pGraphTrianglePipeline = NULL;
RootSignature* pRootSignature = NULL;
RootSignature* pGraphRootSignature = NULL;
DescriptorSet* pDescriptorSet = NULL;
DescriptorSet* pDescriptorSetUniforms = NULL;
Texture*       pTextures[5];
Texture*       pSkyBoxTextures[6];
VirtualJoystickUI gVirtualJoystick;
Sampler* pSampler = NULL;
Sampler* pSamplerSkyBox = NULL;
uint32_t gFrameIndex = 0;

#if defined(_WIN32)
#if defined(_DURANGO)
#else
IWbemServices* pService;
IWbemLocator*  pLocator;
uint64_t*      pOldTimeStamp;
uint64_t*      pOldPprocUsage;
#endif
#elif (__linux__)
uint64_t* pOldTimeStamp;
uint64_t* pOldPprocUsage;
#elif defined(NX64)
//todo
#elif defined(__APPLE__)
NSLock*                CPUUsageLock;
processor_info_array_t prevCpuInfo;
mach_msg_type_number_t numPrevCpuInfo;
#endif

uint   gCoresCount;
float* pCoresLoadData;

uint32_t     gThreadCount = 0;
ThreadData*  pThreadData;
mat4         gProjectView;
mat4         gSkyboxProjectView;
ParticleData gParticleData;
uint32_t     gSeed;
float        gPaletteFactor;
uint         gTextureIndex;

UIApp              gAppUI;
ICameraController* pCameraController = NULL;

ThreadSystem* pThreadSystem;

ProfileToken* pGpuProfiletokens;

CpuGraphData* pCpuData;
CpuGraph*     pCpuGraph;

const char* pImageFileNames[] = { "Palette_Fire", "Palette_Purple", "Palette_Muted", "Palette_Rainbow", "Palette_Sky" };
const char* pSkyBoxImageFileNames[] = { "Skybox_right1",  "Skybox_left2",  "Skybox_top3",
										"Skybox_bottom4", "Skybox_front5", "Skybox_back6" };

TextDrawDesc gFrameTimeDraw = TextDrawDesc(0, 0xff00ffff, 18);

class MultiThread: public IApp
{
	public:
	MultiThread()
	{
#ifdef TARGET_IOS
		mSettings.mContentScaleFactor = 1.f;
#endif
	}
	
	bool Init()
	{
        // FILE PATHS
        PathHandle programDirectory = fsGetApplicationDirectory();
        if (!fsPlatformUsesBundledResources())
        {
            PathHandle resourceDirRoot = fsAppendPathComponent(programDirectory, "../../../src/03_MultiThread");
            fsSetResourceDirRootPath(resourceDirRoot);
            
            fsSetRelativePathForResourceDirEnum(RD_TEXTURES,        "../../UnitTestResources/Textures");
            fsSetRelativePathForResourceDirEnum(RD_MESHES,             "../../UnitTestResources/Meshes");
            fsSetRelativePathForResourceDirEnum(RD_BUILTIN_FONTS,     "../../UnitTestResources/Fonts");
            fsSetRelativePathForResourceDirEnum(RD_ANIMATIONS,         "../../UnitTestResources/Animation");
            fsSetRelativePathForResourceDirEnum(RD_MIDDLEWARE_TEXT,     "../../../../Middleware_3/Text");
            fsSetRelativePathForResourceDirEnum(RD_MIDDLEWARE_UI,     "../../../../Middleware_3/UI");
        }
        
		InitCpuUsage();

		// gThreadCount is the amount of secondary threads: the amount of physical cores except the main thread
		gThreadCount = gCoresCount - 1;
		pThreadData = (ThreadData*)conf_calloc(gThreadCount, sizeof(ThreadData));

		// This information is per core
        pGpuProfiletokens = (ProfileToken*)conf_calloc(gCoresCount, sizeof(ProfileToken));
        eastl::string* ppGpuProfileNames = (eastl::string*)conf_calloc(gCoresCount, sizeof(eastl::string));
        const char** ppConstGpuProfileNames = (const char**)conf_calloc(gCoresCount, sizeof(const char*));
        Queue** ppQueues = (Queue**)conf_calloc(gCoresCount, sizeof(Queue*));

		gGraphWidth = mSettings.mWidth / 6;    //200;
		gGraphHeight = gCoresCount ? (mSettings.mHeight - 30 - gCoresCount * 10) / gCoresCount : 0;

		RendererDesc settings = { 0 };
		// settings.pLogFn = RendererLog;
		initRenderer(GetName(), &settings, &pRenderer);
		//check for init success
		if (!pRenderer)
			return false;

		QueueDesc queueDesc = {};
		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
		addQueue(pRenderer, &queueDesc, &pGraphicsQueue);
		CmdPoolDesc cmdPoolDesc = {};
		cmdPoolDesc.pQueue = pGraphicsQueue;
		addCmdPool(pRenderer, &cmdPoolDesc, &pCmdPool);
		CmdDesc cmdDesc = {};
		cmdDesc.pPool = pCmdPool;
		addCmd_n(pRenderer, &cmdDesc, gImageCount, &ppCmds);

		addCmdPool(pRenderer, &cmdPoolDesc, &pGraphCmdPool);
		cmdDesc.pPool = pGraphCmdPool;
		addCmd_n(pRenderer, &cmdDesc, gImageCount, &ppGraphCmds);

		// initial needed data for each thread
		for (uint32_t i = 0; i < gThreadCount; ++i)
		{
			// create cmd pools and and cmdbuffers for all thread
			addCmdPool(pRenderer, &cmdPoolDesc, &pThreadData[i].pCmdPool);
			cmdDesc.pPool = pThreadData[i].pCmdPool;
			addCmd_n(pRenderer, &cmdDesc, gImageCount, &pThreadData[i].ppCmds);

			// fill up the data for drawing point
			pThreadData[i].mStartPoint = i * (gTotalParticleCount / gThreadCount);
			pThreadData[i].mDrawCount = (gTotalParticleCount / gThreadCount);
			pThreadData[i].mThreadIndex = i;
			pThreadData[i].mThreadID = Thread::mainThreadID;
		}

		// initial Gpu profilers for each core
		for (uint32_t i = 0; i < gCoresCount; ++i)
		{
			ppGpuProfileNames[i] = (i == 0 ? eastl::string().sprintf("Gpu Main thread") : eastl::string().sprintf("Gpu Particle thread %u", i - 1));
            ppConstGpuProfileNames[i] = ppGpuProfileNames[i].c_str();
            ppQueues[i] = pGraphicsQueue;
		}

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			addFence(pRenderer, &pRenderCompleteFences[i]);
			addSemaphore(pRenderer, &pRenderCompleteSemaphores[i]);
		}
		addSemaphore(pRenderer, &pImageAcquiredSemaphore);

		HiresTimer timer;
		initResourceLoaderInterface(pRenderer);

		// load all image to GPU
		for (int i = 0; i < 5; ++i)
		{
            PathHandle path = fsGetPathInResourceDirEnum(RD_TEXTURES, pImageFileNames[i]);
			TextureLoadDesc textureDesc = {};
            textureDesc.pFilePath = path;
			textureDesc.ppTexture = &pTextures[i];
			addResource(&textureDesc, NULL, LOAD_PRIORITY_NORMAL);
		}

		for (int i = 0; i < 6; ++i)
		{
            PathHandle path = fsGetPathInResourceDirEnum(RD_TEXTURES, pSkyBoxImageFileNames[i]);
			TextureLoadDesc textureDesc = {};
			textureDesc.pFilePath = path;
			textureDesc.ppTexture = &pSkyBoxTextures[i];
			addResource(&textureDesc, NULL, LOAD_PRIORITY_NORMAL);
		}

		if (!gVirtualJoystick.Init(pRenderer, "circlepad", RD_TEXTURES))
		{
			LOGF(LogLevel::eERROR, "Could not initialize Virtual Joystick.");
			return false;
		}

		ShaderLoadDesc graphShader = {};
		graphShader.mStages[0] = { "Graph.vert", NULL, 0, RD_SHADER_SOURCES };
		graphShader.mStages[1] = { "Graph.frag", NULL, 0, RD_SHADER_SOURCES };

		ShaderLoadDesc particleShader = {};
		particleShader.mStages[0] = { "Particle.vert", NULL, 0, RD_SHADER_SOURCES };
		particleShader.mStages[1] = { "Particle.frag", NULL, 0, RD_SHADER_SOURCES };

		ShaderLoadDesc skyShader = {};
		skyShader.mStages[0] = { "Skybox.vert", NULL, 0, RD_SHADER_SOURCES };
		skyShader.mStages[1] = { "Skybox.frag", NULL, 0, RD_SHADER_SOURCES };

		addShader(pRenderer, &particleShader, &pShader);
		addShader(pRenderer, &skyShader, &pSkyBoxDrawShader);
		addShader(pRenderer, &graphShader, &pGraphShader);

		SamplerDesc samplerDesc = { FILTER_LINEAR,       FILTER_LINEAR,       MIPMAP_MODE_NEAREST,
									ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT };
		SamplerDesc skyBoxSamplerDesc = { FILTER_LINEAR,
										  FILTER_LINEAR,
										  MIPMAP_MODE_NEAREST,
										  ADDRESS_MODE_CLAMP_TO_EDGE,
										  ADDRESS_MODE_CLAMP_TO_EDGE,
										  ADDRESS_MODE_CLAMP_TO_EDGE };
		addSampler(pRenderer, &samplerDesc, &pSampler);
		addSampler(pRenderer, &skyBoxSamplerDesc, &pSamplerSkyBox);

		const char*       pStaticSamplerNames[] = { "uSampler0", "uSkyboxSampler" };
		Sampler*          pSamplers[] = { pSampler, pSamplerSkyBox };
		Shader*           shaders[] = { pShader, pSkyBoxDrawShader };
		RootSignatureDesc skyBoxRootDesc = {};
		skyBoxRootDesc.mStaticSamplerCount = 2;
		skyBoxRootDesc.ppStaticSamplerNames = pStaticSamplerNames;
		skyBoxRootDesc.ppStaticSamplers = pSamplers;
		skyBoxRootDesc.mShaderCount = 2;
		skyBoxRootDesc.ppShaders = shaders;
		skyBoxRootDesc.mMaxBindlessTextures = 5;
		addRootSignature(pRenderer, &skyBoxRootDesc, &pRootSignature);

		RootSignatureDesc graphRootDesc = {};
		graphRootDesc.mShaderCount = 1;
		graphRootDesc.ppShaders = &pGraphShader;
		addRootSignature(pRenderer, &graphRootDesc, &pGraphRootSignature);

		DescriptorSetDesc setDesc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 2 };
		addDescriptorSet(pRenderer, &setDesc, &pDescriptorSet);
		setDesc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount * 2 };
		addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetUniforms);

		gTextureIndex = 0;

		//#ifdef _WIN32
		//	  SYSTEM_INFO sysinfo;
		//	  GetSystemInfo(&sysinfo);
		//	  gCPUCoreCount = sysinfo.dwNumberOfProcessors;
		//#elif defined(__APPLE__)
		//	  gCPUCoreCount = (unsigned int)[[NSProcessInfo processInfo] processorCount];
		//#endif

		//Generate sky box vertex buffer
		float skyBoxPoints[] = {
			10.0f,  -10.0f, -10.0f, 6.0f,    // -z
			-10.0f, -10.0f, -10.0f, 6.0f,   -10.0f, 10.0f,  -10.0f, 6.0f,   -10.0f, 10.0f,
			-10.0f, 6.0f,   10.0f,  10.0f,  -10.0f, 6.0f,   10.0f,  -10.0f, -10.0f, 6.0f,

			-10.0f, -10.0f, 10.0f,  2.0f,    //-x
			-10.0f, -10.0f, -10.0f, 2.0f,   -10.0f, 10.0f,  -10.0f, 2.0f,   -10.0f, 10.0f,
			-10.0f, 2.0f,   -10.0f, 10.0f,  10.0f,  2.0f,   -10.0f, -10.0f, 10.0f,  2.0f,

			10.0f,  -10.0f, -10.0f, 1.0f,    //+x
			10.0f,  -10.0f, 10.0f,  1.0f,   10.0f,  10.0f,  10.0f,  1.0f,   10.0f,  10.0f,
			10.0f,  1.0f,   10.0f,  10.0f,  -10.0f, 1.0f,   10.0f,  -10.0f, -10.0f, 1.0f,

			-10.0f, -10.0f, 10.0f,  5.0f,    // +z
			-10.0f, 10.0f,  10.0f,  5.0f,   10.0f,  10.0f,  10.0f,  5.0f,   10.0f,  10.0f,
			10.0f,  5.0f,   10.0f,  -10.0f, 10.0f,  5.0f,   -10.0f, -10.0f, 10.0f,  5.0f,

			-10.0f, 10.0f,  -10.0f, 3.0f,    //+y
			10.0f,  10.0f,  -10.0f, 3.0f,   10.0f,  10.0f,  10.0f,  3.0f,   10.0f,  10.0f,
			10.0f,  3.0f,   -10.0f, 10.0f,  10.0f,  3.0f,   -10.0f, 10.0f,  -10.0f, 3.0f,

			10.0f,  -10.0f, 10.0f,  4.0f,    //-y
			10.0f,  -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f,
			-10.0f, 4.0f,   -10.0f, -10.0f, 10.0f,  4.0f,   10.0f,  -10.0f, 10.0f,  4.0f,
		};

		uint64_t       skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
		BufferLoadDesc skyboxVbDesc = {};
		skyboxVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		skyboxVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
		skyboxVbDesc.pData = skyBoxPoints;
		skyboxVbDesc.ppBuffer = &pSkyBoxVertexBuffer;
		addResource(&skyboxVbDesc, NULL, LOAD_PRIORITY_NORMAL);

		BufferLoadDesc ubDesc = {};
		ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		ubDesc.mDesc.mSize = sizeof(mat4);
		ubDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		ubDesc.pData = NULL;
		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			ubDesc.ppBuffer = &pProjViewUniformBuffer[i];
			addResource(&ubDesc, NULL, LOAD_PRIORITY_NORMAL);
			ubDesc.ppBuffer = &pSkyboxUniformBuffer[i];
			addResource(&ubDesc, NULL, LOAD_PRIORITY_NORMAL);
		}

		// generate partcile data
		unsigned int particleSeed = 23232323;    //we have gseed as global declaration, pick a name that is not gseed
		for (int i = 0; i < 6 * 9; ++i)
		{
			RND_GEN(particleSeed);
		}
		uint32_t* seedArray = NULL;
		seedArray = (uint32_t*)conf_malloc(gTotalParticleCount * sizeof(uint32_t));
		for (int i = 0; i < gTotalParticleCount; ++i)
		{
			RND_GEN(particleSeed);
			seedArray[i] = particleSeed;
		}
		uint64_t parDataSize = sizeof(uint32_t) * (uint64_t)gTotalParticleCount;

		BufferLoadDesc particleVbDesc = {};
		particleVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		particleVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		particleVbDesc.mDesc.mSize = parDataSize;
		particleVbDesc.pData = seedArray;
		particleVbDesc.ppBuffer = &pParticleVertexBuffer;
		addResource(&particleVbDesc, NULL, LOAD_PRIORITY_NORMAL);

		uint32_t graphDataSize = sizeof(GraphVertex) * gSampleCount * 3;    // 2 vertex for tri, 1 vertex for line strip

		//generate vertex buffer for all cores to draw cpu graph and setting up view port for each graph
		pCpuGraph = (CpuGraph*)conf_malloc(sizeof(CpuGraph) * gCoresCount);
		for (uint i = 0; i < gCoresCount; ++i)
		{
			pCpuGraph[i].mViewPort.mOffsetX = mSettings.mWidth - 10.0f - gGraphWidth;
			pCpuGraph[i].mViewPort.mWidth = (float)gGraphWidth;
			pCpuGraph[i].mViewPort.mOffsetY = 36 + i * (gGraphHeight + 4.0f);
			pCpuGraph[i].mViewPort.mHeight = (float)gGraphHeight;
			// create vertex buffer for each swapchain
			for (uint j = 0; j < gImageCount; ++j)
			{
				BufferLoadDesc vbDesc = {};
				vbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
				vbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
				vbDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;
				vbDesc.mDesc.mSize = graphDataSize;
				vbDesc.pData = NULL;
				vbDesc.ppBuffer = &pCpuGraph[i].mVertexBuffer[j];
				addResource(&vbDesc, NULL, LOAD_PRIORITY_NORMAL);
			}
		}
		graphDataSize = sizeof(GraphVertex) * gSampleCount;
		for (uint i = 0; i < gImageCount; ++i)
		{
			BufferLoadDesc vbDesc = {};
			vbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
			vbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
			vbDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;
			vbDesc.mDesc.mSize = graphDataSize;
			vbDesc.pData = NULL;
			vbDesc.ppBuffer = &pBackGroundVertexBuffer[i];
			addResource(&vbDesc, NULL, LOAD_PRIORITY_NORMAL);
		}

		if (!gAppUI.Init(pRenderer))
			return false;

		gAppUI.LoadFont("TitilliumText/TitilliumText-Bold.otf", RD_BUILTIN_FONTS);

		GuiDesc guiDesc = {};
		float   dpiScale = getDpiScale().x;
		guiDesc.mStartSize = vec2(140.0f / dpiScale, 320.0f / dpiScale);
		guiDesc.mStartPosition = vec2(mSettings.mWidth - guiDesc.mStartSize.getX() * 4.1f, guiDesc.mStartSize.getY() * 0.5f);

		// Initialize profiler
		initProfiler(pRenderer, ppQueues, ppConstGpuProfileNames, pGpuProfiletokens, gCoresCount);
        conf_free(ppQueues);
        conf_free(ppConstGpuProfileNames);
        conf_free(ppGpuProfileNames);

		initThreadSystem(&pThreadSystem);

		CameraMotionParameters cmp{ 100.0f, 800.0f, 1000.0f };
		vec3                   camPos{ 24.0f, 24.0f, 10.0f };
		vec3                   lookAt{ 0 };

		pCameraController = createFpsCameraController(camPos, lookAt);

		pCameraController->setMotionParameters(cmp);


		if (!initInputSystem(pWindow))
			return false;

		// App Actions
		InputActionDesc actionDesc = { InputBindings::BUTTON_FULLSCREEN, [](InputActionContext* ctx) { toggleFullscreen(((IApp*)ctx->pUserData)->pWindow); return true; }, this };
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
		typedef bool (*CameraInputHandler)(InputActionContext* ctx, uint32_t index);
		static CameraInputHandler onCameraInput = [](InputActionContext* ctx, uint32_t index)
		{
			if (!gAppUI.IsFocused() && *ctx->pCaptured)
			{
				gVirtualJoystick.OnMove(index, ctx->mPhase != INPUT_ACTION_PHASE_CANCELED, ctx->pPosition);
				index ? pCameraController->onRotate(ctx->mFloat2) : pCameraController->onMove(ctx->mFloat2);
			}
			return true;
		};
		actionDesc = { InputBindings::FLOAT_RIGHTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 1); }, NULL, 20.0f, 200.0f, 1.0f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::FLOAT_LEFTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 0); }, NULL, 20.0f, 200.0f, 1.0f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_NORTH, [](InputActionContext* ctx) { pCameraController->resetView(); return true; } };
		addInputAction(&actionDesc);

		waitForAllResourceLoads();
		LOGF(LogLevel::eINFO, "Load Time %lld", timer.GetUSec(false) / 1000);
		conf_free(seedArray);
		
		// Prepare descriptor sets
		DescriptorData params[7] = {};
		params[0].pName = "RightText";
		params[0].ppTextures = &pSkyBoxTextures[0];
		params[1].pName = "LeftText";
		params[1].ppTextures = &pSkyBoxTextures[1];
		params[2].pName = "TopText";
		params[2].ppTextures = &pSkyBoxTextures[2];
		params[3].pName = "BotText";
		params[3].ppTextures = &pSkyBoxTextures[3];
		params[4].pName = "FrontText";
		params[4].ppTextures = &pSkyBoxTextures[4];
		params[5].pName = "BackText";
		params[5].ppTextures = &pSkyBoxTextures[5];
		updateDescriptorSet(pRenderer, 0, pDescriptorSet, 6, params);

		params[0].pName = "uTex0";
		params[0].mCount = sizeof(pImageFileNames) / sizeof(pImageFileNames[0]);
		params[0].ppTextures = pTextures;
		updateDescriptorSet(pRenderer, 1, pDescriptorSet, 1, params);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			params[0] = {};
			params[0].pName = "uniformBlock";
			params[0].ppBuffers = &pSkyboxUniformBuffer[i];
			updateDescriptorSet(pRenderer, i * 2 + 0, pDescriptorSetUniforms, 1, params);
			params[0].ppBuffers = &pProjViewUniformBuffer[i];
			updateDescriptorSet(pRenderer, i * 2 + 1, pDescriptorSetUniforms, 1, params);
		}

		return true;
	}

	void Exit()
	{
		exitInputSystem();
		shutdownThreadSystem(pThreadSystem);
		waitQueueIdle(pGraphicsQueue);

		destroyCameraController(pCameraController);

		exitProfiler();

		gAppUI.Exit();

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeResource(pProjViewUniformBuffer[i]);
			removeResource(pSkyboxUniformBuffer[i]);
		}
		removeResource(pParticleVertexBuffer);
		removeResource(pSkyBoxVertexBuffer);

		for (uint i = 0; i < gImageCount; ++i)
			removeResource(pBackGroundVertexBuffer[i]);

		for (uint i = 0; i < gCoresCount; ++i)
		{
			// remove all vertex buffer belongs to graph
			for (uint j = 0; j < gImageCount; ++j)
				removeResource(pCpuGraph[i].mVertexBuffer[j]);
		}

		conf_free(pCpuGraph);

		for (uint i = 0; i < 5; ++i)
			removeResource(pTextures[i]);
		for (uint i = 0; i < 6; ++i)
			removeResource(pSkyBoxTextures[i]);

		gVirtualJoystick.Exit();

		removeSampler(pRenderer, pSampler);
		removeSampler(pRenderer, pSamplerSkyBox);

		removeDescriptorSet(pRenderer, pDescriptorSet);
		removeDescriptorSet(pRenderer, pDescriptorSetUniforms);

		removeShader(pRenderer, pShader);
		removeShader(pRenderer, pSkyBoxDrawShader);
		removeShader(pRenderer, pGraphShader);
		removeRootSignature(pRenderer, pRootSignature);
		removeRootSignature(pRenderer, pGraphRootSignature);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeFence(pRenderer, pRenderCompleteFences[i]);
			removeSemaphore(pRenderer, pRenderCompleteSemaphores[i]);
		}
		removeSemaphore(pRenderer, pImageAcquiredSemaphore);

		removeCmd_n(pRenderer, gImageCount, ppCmds);
		removeCmdPool(pRenderer, pCmdPool);
		removeCmd_n(pRenderer, gImageCount, ppGraphCmds);
		removeCmdPool(pRenderer, pGraphCmdPool);

		for (uint32_t i = 0; i < gThreadCount; ++i)
		{
			removeCmd_n(pRenderer, gImageCount, pThreadData[i].ppCmds);
			removeCmdPool(pRenderer, pThreadData[i].pCmdPool);
		}

		removeQueue(pRenderer, pGraphicsQueue);

		exitResourceLoaderInterface(pRenderer);
		removeRenderer(pRenderer);

		RemoveCpuUsage();

		conf_free(pThreadData);
        conf_free(pGpuProfiletokens);
	}

	bool Load()
	{
		if (!addSwapChain())
			return false;

		if (!gAppUI.Load(pSwapChain->ppRenderTargets))
			return false;

		if (!gVirtualJoystick.Load(pSwapChain->ppRenderTargets[0]))
			return false;

		loadProfilerUI(&gAppUI, mSettings.mWidth, mSettings.mHeight);

		//vertexlayout and pipeline for particles
		VertexLayout vertexLayout = {};
		vertexLayout.mAttribCount = 1;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32_UINT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;

		BlendStateDesc blendStateDesc = {};
		blendStateDesc.mSrcAlphaFactors[0] = BC_ONE;
		blendStateDesc.mDstAlphaFactors[0] = BC_ONE;
		blendStateDesc.mSrcFactors[0] = BC_ONE;
		blendStateDesc.mDstFactors[0] = BC_ONE;
		blendStateDesc.mMasks[0] = ALL;
		blendStateDesc.mRenderTargetMask = BLEND_STATE_TARGET_0;
		blendStateDesc.mIndependentBlend = false;

		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_NONE;

		PipelineDesc graphicsPipelineDesc = {};
		graphicsPipelineDesc.mType = PIPELINE_TYPE_GRAPHICS;
		GraphicsPipelineDesc& pipelineSettings = graphicsPipelineDesc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_POINT_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pBlendState = &blendStateDesc;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.pRootSignature = pRootSignature;
		pipelineSettings.pShaderProgram = pShader;
		pipelineSettings.pVertexLayout = &vertexLayout;
		addPipeline(pRenderer, &graphicsPipelineDesc, &pPipeline);

		//layout and pipeline for skybox draw
		vertexLayout = {};
		vertexLayout.mAttribCount = 1;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;

		pipelineSettings = { 0 };
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.pRootSignature = pRootSignature;
		pipelineSettings.pShaderProgram = pSkyBoxDrawShader;
		pipelineSettings.pVertexLayout = &vertexLayout;
		addPipeline(pRenderer, &graphicsPipelineDesc, &pSkyBoxDrawPipeline);

		/********** layout and pipeline for graph draw*****************/
		vertexLayout = {};
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat =
			(sizeof(GraphVertex) > 24
				 ? TinyImageFormat_R32G32B32A32_SFLOAT
				 : TinyImageFormat_R32G32_SFLOAT);    // Handle the case when padding is added to the struct (yielding 32 bytes instead of 24) on macOS
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_COLOR;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = TinyImageFormat_BitSizeOfBlock(vertexLayout.mAttribs[0].mFormat) / 8;

		pipelineSettings = { 0 };
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_LINE_STRIP;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.pRootSignature = pGraphRootSignature;
		pipelineSettings.pShaderProgram = pGraphShader;
		pipelineSettings.pVertexLayout = &vertexLayout;
		addPipeline(pRenderer, &graphicsPipelineDesc, &pGraphLinePipeline);

		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_STRIP;
		addPipeline(pRenderer, &graphicsPipelineDesc, &pGraphTrianglePipeline);

		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_LINE_LIST;
		addPipeline(pRenderer, &graphicsPipelineDesc, &pGraphLineListPipeline);
		/********************************************************************/

		return true;
	}

	void Unload()
	{
		waitQueueIdle(pGraphicsQueue);

		unloadProfilerUI();
		gVirtualJoystick.Unload();


		gAppUI.Unload();

		removePipeline(pRenderer, pPipeline);
		removePipeline(pRenderer, pSkyBoxDrawPipeline);
		removePipeline(pRenderer, pGraphLineListPipeline);
		removePipeline(pRenderer, pGraphLinePipeline);
		removePipeline(pRenderer, pGraphTrianglePipeline);

		removeSwapChain(pRenderer, pSwapChain);
	}

	void Update(float deltaTime)
	{
		updateInputSystem(mSettings.mWidth, mSettings.mHeight);
		/************************************************************************/
		// Input
		/************************************************************************/
		pCameraController->update(deltaTime);

		const float k_wrapAround = (float)(M_PI * 2.0);
		if (gObjSettings.mRotX > k_wrapAround)
			gObjSettings.mRotX -= k_wrapAround;
		if (gObjSettings.mRotX < -k_wrapAround)
			gObjSettings.mRotX += k_wrapAround;
		if (gObjSettings.mRotY > k_wrapAround)
			gObjSettings.mRotY -= k_wrapAround;
		if (gObjSettings.mRotY < -k_wrapAround)
			gObjSettings.mRotY += k_wrapAround;
		/************************************************************************/
		// Compute matrices
		/************************************************************************/
		// update camera with time
		mat4 modelMat = mat4::rotationX(gObjSettings.mRotX) * mat4::rotationY(gObjSettings.mRotY);
		mat4 viewMat = pCameraController->getViewMatrix();

		const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
		const float horizontal_fov = PI / 2.0f;
		mat4        projMat = mat4::perspective(horizontal_fov, aspectInverse, 0.1f, 100.0f);
		gProjectView = projMat * viewMat * modelMat;
		// update particle position matrix

		viewMat.setTranslation(vec3(0));
		gSkyboxProjectView = projMat * viewMat;

		gPaletteFactor += deltaTime * 0.25f;
		if (gPaletteFactor > 1.0f)
		{
			for (int i = 0; i < 9; ++i)
			{
				RND_GEN(gSeed);
			}
			gPaletteFactor = 0.0f;

			gTextureIndex = (gTextureIndex + 1) % 5;

			//   gPaletteFactor = 1.0;
		}
		gParticleData.mPaletteFactor = gPaletteFactor * gPaletteFactor * (3.0f - 2.0f * gPaletteFactor);
		gParticleData.mData = gSeed;
		gParticleData.mTextureIndex = gTextureIndex;

		static float currentTime = 0.0f;
		currentTime += deltaTime;

		// update cpu data graph
		if (currentTime * 1000.0f > 500)
		{
			CalCpuUsage();
			for (uint i = 0; i < gCoresCount; ++i)
			{
				pCpuData[i].mSampley[pCpuData[i].mSampleIdx] = 0.0f;
				pCpuData[i].mSample[pCpuData[i].mSampleIdx] = pCoresLoadData[i] / 100.0f;
				pCpuData[i].mSampleIdx = (pCpuData[i].mSampleIdx + 1) % gSampleCount;
			}

			currentTime = 0.0f;
		}

		/************************************************************************/
		// Update GUI
		/************************************************************************/
		gAppUI.Update(deltaTime);
	}

	void Draw()
	{
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &gFrameIndex);

		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[gFrameIndex];
		Semaphore*    pRenderCompleteSemaphore = pRenderCompleteSemaphores[gFrameIndex];
		Fence*        pRenderCompleteFence = pRenderCompleteFences[gFrameIndex];

		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, pRenderCompleteFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(pRenderer, 1, &pRenderCompleteFence);

		uint32_t frameIdx = gFrameIndex;

		SyncToken graphUpdateToken = {};

		for (uint32_t i = 0; i < gCoresCount; ++i)
			CpuGraphcmdUpdateBuffer(frameIdx, &pCpuData[i], &pCpuGraph[i], &graphUpdateToken);    // update vertex buffer for each cpugraph

		// update vertex buffer for background of the graph (grid)
		CpuGraphBackGroundUpdate(frameIdx, &graphUpdateToken);
		/*******record command for drawing particles***************/
		for (uint32_t i = 0; i < gThreadCount; ++i)
		{
			pThreadData[i].pRenderTarget = pRenderTarget;
			pThreadData[i].mFrameIndex = frameIdx;
		}
		addThreadSystemRangeTask(pThreadSystem, &MultiThread::ParticleThreadDraw, pThreadData, gThreadCount);
		// simply record the screen cleaning command

		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0].r = 0.0f;
		loadActions.mClearColorValues[0].g = 0.0f;
		loadActions.mClearColorValues[0].b = 0.0f;
		loadActions.mClearColorValues[0].a = 0.0f;

		Cmd* cmd = ppCmds[frameIdx];
		beginCmd(cmd);
		cmdBeginGpuFrameProfile(cmd, pGpuProfiletokens[0]); // pGpuProfiletokens[0] is reserved for main thread
		
		BufferUpdateDesc viewProjCbv = { pProjViewUniformBuffer[gFrameIndex] };
		beginUpdateResource(&viewProjCbv);
		*(mat4*)viewProjCbv.pMappedData = gProjectView;
		endUpdateResource(&viewProjCbv, NULL);

		BufferUpdateDesc skyboxViewProjCbv = { pSkyboxUniformBuffer[gFrameIndex] };
		beginUpdateResource(&skyboxViewProjCbv);
		*(mat4*)skyboxViewProjCbv.pMappedData = gSkyboxProjectView;
		endUpdateResource(&skyboxViewProjCbv, NULL);

		RenderTargetBarrier barrier = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
		//// draw skybox
        cmdBindPipeline(cmd, pSkyBoxDrawPipeline);
		cmdBindDescriptorSet(cmd, 0, pDescriptorSet);
		cmdBindDescriptorSet(cmd, gFrameIndex * 2 + 0, pDescriptorSetUniforms);

		const uint32_t skyboxStride = sizeof(float) * 4;
		cmdBindVertexBuffer(cmd, 1, &pSkyBoxVertexBuffer, &skyboxStride, NULL);
		cmdDraw(cmd, 36, 0);

		cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");

		gVirtualJoystick.Draw(cmd, { 1.0f, 1.0f, 1.0f, 1.0f });

        cmdDrawCpuProfile(cmd, float2(8, 15), &gFrameTimeDraw);

		gAppUI.DrawText(cmd, float2(8, 65), "CPU Times", NULL);

		gAppUI.DrawText(
			cmd, float2(8.f, 90.0f),
			eastl::string().sprintf("Main Thread - %f ms", getCpuAvgFrameTime()).c_str(),
			&gFrameTimeDraw);

		for (uint32_t i = 0; i < gThreadCount; ++i)
		{
			gAppUI.DrawText(
				cmd, float2(8.f, 115.0f + i * 25.0f),
				eastl::string().sprintf("Particle Thread %u - %f ms", i, getCpuProfileAvgTime("Threads", "Cpu draw", &pThreadData[i].mThreadID)).c_str(),
				&gFrameTimeDraw);
		}

		for (uint32_t i = 0; i < gCoresCount; ++i)
		{
            cmdDrawGpuProfile(cmd, float2(8.f, (130 + gThreadCount * 25.0f) + i * 50.0f), pGpuProfiletokens[i]);
		}

		gAppUI.Draw(cmd);
		cmdEndDebugMarker(cmd);

		cmdEndGpuFrameProfile(cmd, pGpuProfiletokens[0]); // pGpuProfiletokens[0] is reserved for main thread
		endCmd(cmd);

		beginCmd(ppGraphCmds[frameIdx]);
		for (uint i = 0; i < gCoresCount; ++i)
		{
			gGraphWidth = pRenderTarget->mWidth / 6;
			gGraphHeight = (pRenderTarget->mHeight - 30 - gCoresCount * 10) / gCoresCount;
			pCpuGraph[i].mViewPort.mOffsetX = pRenderTarget->mWidth - 10.0f - gGraphWidth;
			pCpuGraph[i].mViewPort.mWidth = (float)gGraphWidth;
			pCpuGraph[i].mViewPort.mOffsetY = 36 + i * (gGraphHeight + 4.0f);
			pCpuGraph[i].mViewPort.mHeight = (float)gGraphHeight;

			cmdBindRenderTargets(ppGraphCmds[frameIdx], 1, &pRenderTarget, NULL, NULL, NULL, NULL, -1, -1);
			cmdSetViewport(
				ppGraphCmds[frameIdx], pCpuGraph[i].mViewPort.mOffsetX, pCpuGraph[i].mViewPort.mOffsetY, pCpuGraph[i].mViewPort.mWidth,
				pCpuGraph[i].mViewPort.mHeight, 0.0f, 1.0f);
			cmdSetScissor(ppGraphCmds[frameIdx], 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

			const uint32_t graphDataStride = sizeof(GraphVertex);                     // vec2(position) + vec4(color)

			cmdBindPipeline(ppGraphCmds[frameIdx], pGraphTrianglePipeline);
			cmdBindVertexBuffer(ppGraphCmds[frameIdx], 1, &pBackGroundVertexBuffer[frameIdx], &graphDataStride, NULL);
			cmdDraw(ppGraphCmds[frameIdx], 4, 0);

			cmdBindPipeline(ppGraphCmds[frameIdx], pGraphLineListPipeline);
			cmdBindVertexBuffer(ppGraphCmds[frameIdx], 1, &pBackGroundVertexBuffer[frameIdx], &graphDataStride, NULL);
			cmdDraw(ppGraphCmds[frameIdx], 38, 4);

			cmdBindPipeline(ppGraphCmds[frameIdx], pGraphTrianglePipeline);
			cmdBindVertexBuffer(ppGraphCmds[frameIdx], 1, &(pCpuGraph[i].mVertexBuffer[frameIdx]), &graphDataStride, NULL);
			cmdDraw(ppGraphCmds[frameIdx], 2 * gSampleCount, 0);

			cmdBindPipeline(ppGraphCmds[frameIdx], pGraphLinePipeline);
			cmdBindVertexBuffer(ppGraphCmds[frameIdx], 1, &pCpuGraph[i].mVertexBuffer[frameIdx], &graphDataStride, NULL);
			cmdDraw(ppGraphCmds[frameIdx], gSampleCount, 2 * gSampleCount);
		}
		cmdSetViewport(ppGraphCmds[frameIdx], 0.0f, 0.0f, static_cast<float>(mSettings.mWidth), static_cast<float>(mSettings.mHeight), 0.0f, 1.0f);
		cmdSetScissor(ppGraphCmds[frameIdx], 0, 0, mSettings.mWidth, mSettings.mHeight);
		cmdDrawProfilerUI();

		cmdBindRenderTargets(ppGraphCmds[frameIdx], 0, NULL, NULL, NULL, NULL, NULL, -1, -1);

		barrier = { pRenderTarget, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(ppGraphCmds[frameIdx], 0, NULL, 0, NULL, 1, &barrier);
		endCmd(ppGraphCmds[frameIdx]);
		// wait all particle threads done
		waitThreadSystemIdle(pThreadSystem);
		// Wait till graph buffers have been uploaded to the gpu
		waitForToken(&graphUpdateToken);
		/***************draw cpu graph*****************************/
		/***************draw cpu graph*****************************/
		// gather all command buffer, it is important to keep the screen clean command at the beginning
		Cmd** allCmds = (Cmd**)alloca((gThreadCount + 2) * sizeof(Cmd*));
		allCmds[0] = cmd;

		for (uint32_t i = 0; i < gThreadCount; ++i)
		{
			allCmds[i + 1] = pThreadData[i].ppCmds[frameIdx];
		}
		allCmds[gThreadCount + 1] = ppGraphCmds[frameIdx];
		// submit all command buffer

		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = gThreadCount + 2;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = 1;
		submitDesc.ppCmds = allCmds;
		submitDesc.ppSignalSemaphores = &pRenderCompleteSemaphore;
		submitDesc.ppWaitSemaphores = &pImageAcquiredSemaphore;
		submitDesc.pSignalFence = pRenderCompleteFence;
		queueSubmit(pGraphicsQueue, &submitDesc);
		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = gFrameIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.ppWaitSemaphores = &pRenderCompleteSemaphore;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.mSubmitDone = true;
		queuePresent(pGraphicsQueue, &presentDesc);
		flipProfiler();
	}

	const char* GetName() { return "03_MultiThread"; }

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
		swapChainDesc.mEnableVsync = false;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}

#if defined(__linux__)
	enum CPUStates{ S_USER = 0,    S_NICE, S_SYSTEM, S_IDLE, S_IOWAIT, S_IRQ, S_SOFTIRQ, S_STEAL, S_GUEST, S_GUEST_NICE,

					NUM_CPU_STATES };
	typedef struct CPUData
	{
		eastl::string cpu;
		size_t          times[NUM_CPU_STATES];
	} CPUData;

	size_t GetIdleTime(const CPUData& e) { return e.times[S_IDLE] + e.times[S_IOWAIT]; }

	size_t GetActiveTime(const CPUData& e)
	{
		return e.times[S_USER] + e.times[S_NICE] + e.times[S_SYSTEM] + e.times[S_IRQ] + e.times[S_SOFTIRQ] + e.times[S_STEAL] +
			   e.times[S_GUEST] + e.times[S_GUEST_NICE];
	}
#endif

	void CalCpuUsage()
	{
#ifdef _WIN32
#if defined(_DURANGO)
#else
        HRESULT hr = NULL;
        ULONG   retVal;
        UINT    i;

        IWbemClassObject*     pclassObj;
        IEnumWbemClassObject* pEnumerator;

        hr = pService->ExecQuery(
            bstr_t("WQL"),
            bstr_t("SELECT TimeStamp_Sys100NS, PercentProcessorTime, Frequency_PerfTime FROM Win32_PerfRawData_PerfOS_Processor"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
        for (i = 0; i < gCoresCount; i++)
        {
            //Waiting for inifinite blocks resources and app.
            //Waiting for 15 ms (arbitrary) instead works much better
            hr = pEnumerator->Next(15, 1, &pclassObj, &retVal);
            if (!retVal)
            {
                break;
            }

            VARIANT vtPropTime;
            VARIANT vtPropClock;
            VariantInit(&vtPropTime);
            VariantInit(&vtPropClock);

            hr = pclassObj->Get(L"TimeStamp_Sys100NS", 0, &vtPropTime, 0, 0);
            UINT64 newTimeStamp = _wtoi64(vtPropTime.bstrVal);

            hr = pclassObj->Get(L"PercentProcessorTime", 0, &vtPropClock, 0, 0);
            UINT64 newPProcUsage = _wtoi64(vtPropClock.bstrVal);

            pCoresLoadData[i] =
                (float)(1.0 - (((double)newPProcUsage - (double)pOldPprocUsage[i]) / ((double)newTimeStamp - (double)pOldTimeStamp[i]))) *
                100.0f;

            if (pCoresLoadData[i] < 0)
                pCoresLoadData[i] = 0.0;
            else if (pCoresLoadData[i] > 100.0)
                pCoresLoadData[i] = 100.0;

            pOldPprocUsage[i] = newPProcUsage;
            pOldTimeStamp[i] = newTimeStamp;

            VariantClear(&vtPropTime);
            VariantClear(&vtPropClock);

            pclassObj->Release();
        }

        pEnumerator->Release();
#endif    //#if defined(_DURANGO)
#elif (__linux__)
		eastl::vector<CPUData> entries;
		entries.reserve(gCoresCount);
		// Open cpu stat file

		PathHandle statPath = fsCreatePath(fsGetSystemFileSystem(), "/proc/stat");
		FileStream* fh = fsOpenFile(statPath, FM_READ_BINARY);

		if (fh)
		{
			// While eof not detected, keep parsing the stat file
			while (!fsStreamAtEnd(fh))
			{
				entries.emplace_back(CPUData());
				CPUData& entry = entries.back();
				char     dummyCpuName[256];    // dummy cpu name, not used.
				int bytesRead;
				fsScanFromStream(
					fh, &bytesRead, "%s %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu", &dummyCpuName[0], &entry.times[0], &entry.times[1],
					&entry.times[2], &entry.times[3], &entry.times[4], &entry.times[5], &entry.times[6], &entry.times[7], &entry.times[8],
					&entry.times[9]);
			}
			// Close the cpu stat file
			fsCloseStream(fh);
		}

		for (uint32_t i = 0; i < gCoresCount; i++)
		{
			float ACTIVE_TIME = static_cast<float>(GetActiveTime(entries[i]));
			float IDLE_TIME = static_cast<float>(GetIdleTime(entries[i]));

			pCoresLoadData[i] = (ACTIVE_TIME - pOldPprocUsage[i]) / ((float)(IDLE_TIME + ACTIVE_TIME) - pOldTimeStamp[i]) * 100.0f;

			pOldPprocUsage[i] = ACTIVE_TIME;
			pOldTimeStamp[i] = IDLE_TIME + ACTIVE_TIME;
		}
#elif defined(NX64)
		//
#elif defined(__APPLE__)
		processor_info_array_t cpuInfo;
		mach_msg_type_number_t numCpuInfo;

		natural_t     numCPUsU = 0U;
		kern_return_t err = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUsU, &cpuInfo, &numCpuInfo);

		if (err == KERN_SUCCESS)
		{
			[CPUUsageLock lock];

			for (uint32_t i = 0; i < gCoresCount; i++)
			{
				float inUse, total;

				if (prevCpuInfo)
				{
					inUse =
						((cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER]) +
						 (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM]) +
						 (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE]));
					total = inUse + (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE]);
				}
				else
				{
					inUse = cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] + cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] +
							cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE];
					total = inUse + cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
				}

				pCoresLoadData[i] = (float(inUse) / float(total)) * 100;

				if (pCoresLoadData[i] < 0)
					pCoresLoadData[i] = 0.0;
				else if (pCoresLoadData[i] > 100.0)
					pCoresLoadData[i] = 100.0;
			}

			[CPUUsageLock unlock];

			if (prevCpuInfo)
			{
				size_t prevCpuInfoSize = sizeof(integer_t) * numPrevCpuInfo;
				vm_deallocate(mach_task_self(), (vm_address_t)prevCpuInfo, prevCpuInfoSize);
			}

			prevCpuInfo = cpuInfo;
			numPrevCpuInfo = numCpuInfo;
		}

#endif
	}

	int InitCpuUsage()
	{
		gCoresCount = 0;
#ifdef _WIN32
#if defined(_DURANGO)
		gCoresCount = Thread::GetNumCPUCores();
#else
        IWbemClassObject*     pclassObj;
        IEnumWbemClassObject* pEnumerator;
        HRESULT               hr;
        ULONG                 retVal;

        pService = NULL;
        pLocator = NULL;
        pOldTimeStamp = NULL;
        pOldPprocUsage = NULL;
        pCoresLoadData = NULL;

        CoInitializeEx(0, COINIT_MULTITHREADED);
        CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);

        hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLocator);
        if (FAILED(hr))
        {
            return 0;
        }
        hr = pLocator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pService);
        if (FAILED(hr))
        {
            return 0;
        }

        CoSetProxyBlanket(
            pService, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

        hr = pService->ExecQuery(
            bstr_t("WQL"), bstr_t("SELECT * FROM Win32_Processor"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
            &pEnumerator);
        hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclassObj, &retVal);
        if (retVal)
        {
            VARIANT vtProp;
            VariantInit(&vtProp);
            hr = pclassObj->Get(L"NumberOfLogicalProcessors", 0, &vtProp, 0, 0);
            gCoresCount = vtProp.uintVal;
            VariantClear(&vtProp);
        }

        pclassObj->Release();
        pEnumerator->Release();

        if (gCoresCount)
        {
            pOldTimeStamp = (uint64_t*)conf_malloc(sizeof(uint64_t) * gCoresCount);
            pOldPprocUsage = (uint64_t*)conf_malloc(sizeof(uint64_t) * gCoresCount);
        }
#endif
#elif defined(__linux__)
		int numCPU = sysconf(_SC_NPROCESSORS_ONLN);
		gCoresCount = numCPU;
		if (gCoresCount)
		{
			pOldTimeStamp = (uint64_t*)conf_malloc(sizeof(uint64_t) * gCoresCount);
			pOldPprocUsage = (uint64_t*)conf_malloc(sizeof(uint64_t) * gCoresCount);
		}
#elif defined(__APPLE__)
		processor_info_array_t cpuInfo;
		mach_msg_type_number_t numCpuInfo;

		natural_t     numCPUsU = 0U;
		kern_return_t err = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUsU, &cpuInfo, &numCpuInfo);

		assert(err == KERN_SUCCESS);

		gCoresCount = numCPUsU;

		CPUUsageLock = [[NSLock alloc] init];
#elif defined(ORBIS) || defined(NX64)
		gCoresCount = Thread::GetNumCPUCores();
#endif

		pCpuData = (CpuGraphData*)conf_malloc(sizeof(CpuGraphData) * gCoresCount);
		for (uint i = 0; i < gCoresCount; ++i)
		{
			pCpuData[i].mSampleIdx = 0;
			pCpuData[i].mScale = 1.0f;
			for (uint j = 0; j < gSampleCount; ++j)
			{
				pCpuData[i].mSample[j] = 0.0f;
				pCpuData[i].mSampley[j] = 0.0f;
			}
		}

		if (gCoresCount)
		{
			pCoresLoadData = (float*)conf_malloc(sizeof(float) * gCoresCount);
			float zeroFloat = 0.0;
			memset(pCoresLoadData, *(int*)&zeroFloat, sizeof(float) * gCoresCount);
		}

		CalCpuUsage();
		return 1;
	}

	void RemoveCpuUsage()
	{
		conf_free(pCpuData);
#if (defined(_WIN32) && !defined(_DURANGO)) || defined(__linux__)
        conf_free(pOldTimeStamp);
        conf_free(pOldPprocUsage);
#endif
		conf_free(pCoresLoadData);
	}

	void CpuGraphBackGroundUpdate(uint32_t frameIdx, SyncToken* token)
	{
		BufferUpdateDesc backgroundVbUpdate = { pBackGroundVertexBuffer[frameIdx] };
		beginUpdateResource(&backgroundVbUpdate);
		GraphVertex* backGroundPoints = (GraphVertex*)backgroundVbUpdate.pMappedData;
		memset(backGroundPoints, 0, pBackGroundVertexBuffer[frameIdx]->mSize);

		// background data
		backGroundPoints[0].mPosition = vec2(-1.0f, -1.0f);
		backGroundPoints[0].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);
		backGroundPoints[1].mPosition = vec2(1.0f, -1.0f);
		backGroundPoints[1].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);
		backGroundPoints[2].mPosition = vec2(-1.0f, 1.0f);
		backGroundPoints[2].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);
		backGroundPoints[3].mPosition = vec2(1.0f, 1.0f);
		backGroundPoints[3].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);

		const float woff = 2.0f / gGraphWidth;
		const float hoff = 2.0f / gGraphHeight;

		backGroundPoints[4].mPosition = vec2(-1.0f + woff, -1.0f + hoff);
		backGroundPoints[4].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
		backGroundPoints[5].mPosition = vec2(1.0f - woff, -1.0f + hoff);
		backGroundPoints[5].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
		backGroundPoints[6].mPosition = vec2(1.0f - woff, -1.0f + hoff);
		backGroundPoints[6].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
		backGroundPoints[7].mPosition = vec2(1.0f - woff, 1.0f - hoff);
		backGroundPoints[7].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
		backGroundPoints[8].mPosition = vec2(1.0f - woff, 1.0f - hoff);
		backGroundPoints[8].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
		backGroundPoints[9].mPosition = vec2(-1.0f + woff, 1.0f - hoff);
		backGroundPoints[9].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
		backGroundPoints[10].mPosition = vec2(-1.0f + woff, 1.0f - hoff);
		backGroundPoints[10].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
		backGroundPoints[11].mPosition = vec2(-1.0f + woff, -1.0f + hoff);
		backGroundPoints[11].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);

		for (int i = 1; i <= 6; ++i)
		{
			backGroundPoints[12 + i * 2].mPosition =
				vec2(-1.0f + i * (2.0f / 6.0f) - 2.0f * ((pCpuData[0].mSampleIdx % (gSampleCount / 6)) / (float)gSampleCount), -1.0f);
			backGroundPoints[12 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
			backGroundPoints[13 + i * 2].mPosition =
				vec2(-1.0f + i * (2.0f / 6.0f) - 2.0f * ((pCpuData[0].mSampleIdx % (gSampleCount / 6)) / (float)gSampleCount), 1.0f);
			backGroundPoints[13 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
		}
		// start from 24

		for (int i = 1; i <= 9; ++i)
		{
			backGroundPoints[24 + i * 2].mPosition = vec2(-1.0f, -1.0f + i * (2.0f / 10.0f));
			backGroundPoints[24 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
			backGroundPoints[25 + i * 2].mPosition = vec2(1.0f, -1.0f + i * (2.0f / 10.0f));
			backGroundPoints[25 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
		}
		//start from 42

		backGroundPoints[42].mPosition = vec2(-1.0f, -1.0f);
		backGroundPoints[42].mColor = vec4(0.85f, 0.9f, 0.0f, 0.25f);
		backGroundPoints[43].mPosition = vec2(1.0f, -1.0f);
		backGroundPoints[43].mColor = vec4(0.85f, 0.9f, 0.0f, 0.25f);
		backGroundPoints[44].mPosition = vec2(-1.0f, 1.0f);
		backGroundPoints[44].mColor = vec4(0.85f, 0.9f, 0.0f, 0.25f);
		backGroundPoints[45].mPosition = vec2(1.0f, 1.0f);
		backGroundPoints[45].mColor = vec4(0.85f, 0.9f, 0.0f, 0.25f);

		backGroundPoints[42].mPosition = vec2(-1.0f, -1.0f);
		backGroundPoints[42].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);
		backGroundPoints[43].mPosition = vec2(1.0f, -1.0f);
		backGroundPoints[43].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);
		backGroundPoints[44].mPosition = vec2(-1.0f, 1.0f);
		backGroundPoints[44].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);
		backGroundPoints[45].mPosition = vec2(1.0f, 1.0f);
		backGroundPoints[45].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);

		endUpdateResource(&backgroundVbUpdate, token);
	}

	void CpuGraphcmdUpdateBuffer(uint32_t frameIdx, CpuGraphData* graphData, CpuGraph* graph, SyncToken* token)
	{
		BufferUpdateDesc vbUpdate = { graph->mVertexBuffer[frameIdx] };
		beginUpdateResource(&vbUpdate);
		GraphVertex* points = (GraphVertex*)vbUpdate.pMappedData;
		memset(points, 0, graph->mVertexBuffer[frameIdx]->mSize);

		int index = graphData->mSampleIdx;
		// fill up tri vertex
		for (int i = 0; i < gSampleCount; ++i)
		{
			if (--index < 0)
				index = gSampleCount - 1;
			points[i * 2].mPosition = vec2((1.0f - i * (2.0f / gSampleCount)) * 0.999f - 0.02f, -0.97f);
			points[i * 2].mColor = vec4(0.0f, 0.85f, 0.0f, 1.0f);
			points[i * 2 + 1].mPosition = vec2(
				(1.0f - i * (2.0f / gSampleCount)) * 0.999f - 0.02f,
				(2.0f * ((graphData->mSample[index] + graphData->mSampley[index]) * graphData->mScale - 0.5f)) * 0.97f);
			points[i * 2 + 1].mColor = vec4(0.0f, 0.85f, 0.0f, 1.0f);
		}

		//line vertex
		index = graphData->mSampleIdx;
		for (int i = 0; i < gSampleCount; ++i)
		{
			if (--index < 0)
				index = gSampleCount - 1;
			points[i + 2 * gSampleCount].mPosition = vec2(
				(1.0f - i * (2.0f / gSampleCount)) * 0.999f - 0.02f,
				(2.0f * ((graphData->mSample[index] + graphData->mSampley[index]) * graphData->mScale - 0.5f)) * 0.97f);
			points[i + 2 * gSampleCount].mColor = vec4(0.0f, 0.85f, 0.0f, 1.0f);
		}

		endUpdateResource(&vbUpdate, token);
	}

	// thread for recording particle draw
	static void ParticleThreadDraw(void* pData, uintptr_t i)
	{
		ThreadData& data = ((ThreadData*)pData)[i];
        if(data.mThreadID ==  Thread::mainThreadID)
            data.mThreadID = Thread::GetCurrentThreadID();
        PROFILER_SET_CPU_SCOPE("Threads", "Cpu draw", 0xffffff);
		Cmd*        cmd = data.ppCmds[data.mFrameIndex];
		beginCmd(cmd);
		cmdBeginGpuFrameProfile(cmd, pGpuProfiletokens[data.mThreadIndex + 1]); // pGpuProfiletokens[0] is reserved for main thread

		cmdBindRenderTargets(cmd, 1, &data.pRenderTarget, NULL, NULL, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)data.pRenderTarget->mWidth, (float)data.pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, data.pRenderTarget->mWidth, data.pRenderTarget->mHeight);

		const uint32_t parDataStride = sizeof(uint32_t);
		cmdBindPipeline(cmd, pPipeline);
		cmdBindDescriptorSet(cmd, 1, pDescriptorSet);
		cmdBindDescriptorSet(cmd, data.mFrameIndex * 2 + 1, pDescriptorSetUniforms);
		cmdBindPushConstants(cmd, pRootSignature, "particleRootConstant", &gParticleData);
		cmdBindVertexBuffer(cmd, 1, &pParticleVertexBuffer, &parDataStride, NULL);

		cmdDrawInstanced(cmd, data.mDrawCount, data.mStartPoint, 1, 0);

		cmdEndGpuFrameProfile(cmd, pGpuProfiletokens[data.mThreadIndex + 1]);  // pGpuProfiletokens[0] is reserved for main thread
		endCmd(cmd);
	}
};

DEFINE_APPLICATION_MAIN(MultiThread)
