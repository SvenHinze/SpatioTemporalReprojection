/*
  Copyrighted(c) 2020, TH Köln.All rights reserved. Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met :

  * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
  * Neither the name of TH Köln nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT OWNER
  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Authors: Niko Wissmann, Martin Misiak
 */

#include "Reprojection.h"

struct layoutsData
{
    uint32_t pos;
    std::string name;
    ResourceFormat format;
};

static const layoutsData kLayoutData[VERTEX_LOCATION_COUNT] =
{
    { VERTEX_POSITION_LOC,      VERTEX_POSITION_NAME,       ResourceFormat::RGB32Float },
    { VERTEX_NORMAL_LOC,        VERTEX_NORMAL_NAME,         ResourceFormat::RGB32Float },
    { VERTEX_BITANGENT_LOC,     VERTEX_BITANGENT_NAME,      ResourceFormat::RGB32Float },
    { VERTEX_TEXCOORD_LOC,      VERTEX_TEXCOORD_NAME,       ResourceFormat::RGB32Float }, //for some reason this is rgb
    { VERTEX_LIGHTMAP_UV_LOC,   VERTEX_LIGHTMAP_UV_NAME,    ResourceFormat::RGB32Float }, //for some reason this is rgb
    { VERTEX_BONE_WEIGHT_LOC,   VERTEX_BONE_WEIGHT_NAME,    ResourceFormat::RGBA32Float},
    { VERTEX_BONE_ID_LOC,       VERTEX_BONE_ID_NAME,        ResourceFormat::RGBA8Uint  },
    { VERTEX_DIFFUSE_COLOR_LOC, VERTEX_DIFFUSE_COLOR_NAME,  ResourceFormat::RGBA32Float},
    { VERTEX_QUADID_LOC,        VERTEX_QUADID_NAME,         ResourceFormat::R32Float   },
};

size_t Reprojection::sLightArrayOffset = ConstantBuffer::kInvalidOffset;
size_t Reprojection::sLightCountOffset = ConstantBuffer::kInvalidOffset;
size_t Reprojection::sCameraDataOffset = ConstantBuffer::kInvalidOffset;

Reprojection::SharedPtr Reprojection::create(const Dictionary & params)
{
    Reprojection::SharedPtr ptr(new Reprojection());
    return ptr;
}

RenderPassReflection Reprojection::reflect() const
{
    RenderPassReflection r;
    r.addInput("depth", "");
    r.addInput("leftIn", "");
    r.addInput("shadowDepth", "");
    r.addInput("gbufferNormal", "");
    r.addInput("gbufferPosition", "");

    r.addInternal("internalDepth", "").format(ResourceFormat::D32FloatS8X24).bindFlags(Resource::BindFlags::DepthStencil);
    r.addOutput("out", "").format(ResourceFormat::RGBA32Float).bindFlags(Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::RenderTarget);
    return r;
}

void Reprojection::initialize(const RenderData * pRenderData)
{
    // Compute Program
    mpComputeProgram = ComputeProgram::createFromFile("QuadLevelCompute.slang", "main");
    mpComputeState = ComputeState::create();
    mpComputeState->setProgram(mpComputeProgram);
    mpComputeProgVars = ComputeVars::create(mpComputeProgram->getReflector());
    mpDepthDiffResultBuffer = StructuredBuffer::create(mpComputeProgram, "gDiffResult", (pRenderData->getTexture("leftIn")->getWidth() / mQuadDivideFactor) * (pRenderData->getTexture("leftIn")->getHeight() / mQuadDivideFactor));

    // Reprojection Program (Grid)
    GraphicsProgram::Desc progDesc;
    progDesc
        .addShaderLibrary("ReprojectionVS.slang").vsEntry("main")
        .addShaderLibrary("ReprojectionHS.slang").hsEntry("main")
        .addShaderLibrary("ReprojectionDS.slang").dsEntry("main")
#if _USEGEOSHADER
        .addShaderLibrary("ReprojectionGS.slang").gsEntry("main")
#endif
#if _USETRIANGLECOUNTSHADER
        .addShaderLibrary("TriangleCountGS.slang").gsEntry("main")
#endif
        .addShaderLibrary("ReprojectionPS.slang").psEntry("main");

    mpProgram = GraphicsProgram::create(progDesc);
    mpVars = GraphicsVars::create(mpProgram->getReflector());
    mpState = GraphicsState::create();
    mpState->setProgram(mpProgram);
    mpFbo = Fbo::create();

    // Ray Trace Program
    RtProgram::Desc rtProgDesc;
    rtProgDesc.addShaderLibrary("ReprojectionRT.slang").setRayGen("rayGen");
    rtProgDesc.addHitGroup(0, "primaryClosestHit", "").addMiss(0, "primaryMiss");
    mpRaytraceProgram = RtProgram::create(rtProgDesc);
    mpRtState = RtState::create();
    mpRtState->setProgram(mpRaytraceProgram);
    mpRtState->setMaxTraceRecursionDepth(1);
    mpRtVars = RtProgramVars::create(mpRaytraceProgram, mpScene);
    mpRtRenderer = RtStaticSceneRenderer::create(mpScene);

    // Start Defines
    setDefine("_EIGHT_NEIGHBOR", mbUseEightNeighbor);
    setDefine("_SHOWDISOCCLUSION", mbShowDisocclusion);
    setDefine("_DEBUG_THIRDPERSON", mbUseThirdPersonCam);
    setDefine("_PERFRAGMENT", true);
    setDefine("_BINOCULAR_METRIC", mbUseBinocularMetric);
    if (mbUseBinocularMetric)
        mpComputeProgram->addDefine("_BINOCULAR_METRIC");
    else
        mpComputeProgram->removeDefine("_BINOCULAR_METRIC");
#if _USEGEOSHADER
    setDefine("_DISCARD_TRIANGLES", mbUseGeoShader);
#endif

    // Wireframe Raster State
    RasterizerState::Desc wireframeDesc;
    wireframeDesc.setFillMode(RasterizerState::FillMode::Wireframe);
    wireframeDesc.setCullMode(RasterizerState::CullMode::None);
    mpWireframeRS = RasterizerState::create(wireframeDesc);

    // Back Cull Raster State
    RasterizerState::Desc solidDesc;
    solidDesc.setCullMode(RasterizerState::CullMode::None);
    mpCullRastState = RasterizerState::create(solidDesc);

    // Start Raster State (Wireframe or Raster)
    mpState->setRasterizerState(mbWireFrame ? mpWireframeRS : mpCullRastState);

    // Sampler
    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mpLinearSampler = Sampler::create(samplerDesc);

    // Depth Test Grid Renderer
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthTest(true);
    dsDesc.setDepthFunc(DepthStencilState::Func::Less);
    dsDesc.setStencilTest(true);
    dsDesc.setStencilWriteMask(1);
    dsDesc.setStencilRef(0);
    dsDesc.setStencilFunc(DepthStencilState::Face::Front, DepthStencilState::Func::LessEqual);
    dsDesc.setStencilOp(DepthStencilState::Face::Front, DepthStencilState::StencilOp::Keep, DepthStencilState::StencilOp::Keep, DepthStencilState::StencilOp::Increase);
    mpDepthTestDS = DepthStencilState::create(dsDesc);
    mpState->setDepthStencilState(mpDepthTestDS);

    // Reprojection Debug Camera (Third Person Camera)
    mpThirdPersonCam = Camera::create();
    mpThirdPersonCam->move(glm::vec3(0, 0, 3.5f), glm::vec3(0, 0, 1.75f), glm::vec3(0, 1.f, 0));
    mpThirdPersonCam->setDepthRange(0.000005f, 10.f);
    mThirdPersonCamController.attachCamera(mpThirdPersonCam);
    mThirdPersonCamController.setCameraSpeed(0.75f);

    // Re-Raster Program G-Buffer
    GraphicsProgram::Desc reRasterProgDesc;
    reRasterProgDesc
        .addShaderLibrary("StereoVS.slang").vsEntry("main")
        .addShaderLibrary("RasterPrimary.slang").psEntry("main");

    mpReRasterProgram = GraphicsProgram::create(reRasterProgDesc);
    mpReRasterVars = GraphicsVars::create(mpReRasterProgram->getReflector());
    mpReRasterGraphicsState = GraphicsState::create();
    mpReRasterGraphicsState->setProgram(mpReRasterProgram);

    // Cull Mode
    RasterizerState::Desc reRasterStateDesc;
    reRasterStateDesc.setCullMode(RasterizerState::CullMode::Back);
    RasterizerState::SharedPtr reRasterState = RasterizerState::create(reRasterStateDesc);
    mpReRasterGraphicsState->setRasterizerState(reRasterState);

    // Depth Stencil State
    DepthStencilState::Desc reDepthStencilState;
    reDepthStencilState.setDepthTest(true);
    reDepthStencilState.setStencilTest(true);
    reDepthStencilState.setStencilReadMask(1);
    reDepthStencilState.setStencilWriteMask(2);
    reDepthStencilState.setStencilRef(0);
    reDepthStencilState.setStencilFunc(DepthStencilState::Face::Front, DepthStencilState::Func::Equal);
    reDepthStencilState.setStencilOp(DepthStencilState::Face::Front, DepthStencilState::StencilOp::Increase, DepthStencilState::StencilOp::Increase, DepthStencilState::StencilOp::Increase);
    DepthStencilState::SharedPtr reRasterDepthStencil = DepthStencilState::create(reDepthStencilState);
    mpReRasterGraphicsState->setDepthStencilState(reRasterDepthStencil);

    // Re-Raster Program Lighting
    mpReRasterLightingState = GraphicsState::create();
    mpReRasterLightingPass = FullScreenPass::create("Lighting.slang");
    mpReRasterLightingVars = GraphicsVars::create(mpReRasterLightingPass->getProgram()->getReflector());
    mpReRasterLightingFbo = Fbo::create();

    // Depth Stencil State
    DepthStencilState::Desc reLightingDepthStencilState;
    reLightingDepthStencilState.setDepthTest(false);
    reLightingDepthStencilState.setDepthWriteMask(false);
    reLightingDepthStencilState.setStencilTest(true);
    reLightingDepthStencilState.setStencilReadMask(2);
    reLightingDepthStencilState.setStencilRef(1);
    reLightingDepthStencilState.setStencilFunc(DepthStencilState::Face::Front, DepthStencilState::Func::Equal);
    DepthStencilState::SharedPtr reRasterLightingDepthStencil = DepthStencilState::create(reLightingDepthStencilState);
    mpReRasterLightingState->setDepthStencilState(reRasterLightingDepthStencil);

    mIsInitialized = true;
}

void Reprojection::execute(RenderContext * pContext, const RenderData * pRenderData)
{
    // On first execution, run some initialization
    if (!mIsInitialized) initialize(pRenderData);

    mThirdPersonCamController.update();

    // Get our output buffer and clear it
    const auto& pDisTex = pRenderData->getTexture("out");
    mpFbo->attachColorTarget(pDisTex, 0);
    mpFbo->attachDepthStencilTarget(pRenderData->getTexture("internalDepth"));
    pContext->clearFbo(mpFbo.get(), glm::vec4(mClearColor, 0), 1.0f, 0, FboAttachmentType::All);

    // Compute Program - Adaptive Grid Pre-Pass ##################
    Profiler::startEvent("compute_tess");
    mpComputeProgVars->setTexture("gDepthTex", pRenderData->getTexture("depth"));
    mpComputeProgVars->setTexture("gNormalTex", pRenderData->getTexture("gbufferNormal"));
    mpComputeProgVars->setTexture("gPositionTex", pRenderData->getTexture("gbufferPosition"));
    mpComputeProgVars->setStructuredBuffer("gDiffResult", mpDepthDiffResultBuffer);
    mpComputeProgVars["ComputeCB"]["gQuadSizeX"] = pRenderData->getTexture("leftIn")->getWidth() / mQuadDivideFactor;
    mpComputeProgVars["ComputeCB"]["gNearZ"] = mpScene->getActiveCamera()->getNearPlane();
    mpComputeProgVars["ComputeCB"]["gFarZ"] = mpScene->getActiveCamera()->getFarPlane();
    mpComputeProgVars["ComputeCB"]["gCamPos"] = mpScene->getActiveCamera()->getPosition();
    pContext->setComputeState(mpComputeState);
    pContext->setComputeVars(mpComputeProgVars);

    uint32_t w = (pRenderData->getTexture("leftIn")->getWidth() / mQuadDivideFactor);
    uint32_t h = (pRenderData->getTexture("leftIn")->getHeight() / mQuadDivideFactor);
    pContext->dispatch(w, h, 1);
    Profiler::endEvent("compute_tess");

    // Reprojection Program ##########################
    Profiler::startEvent("render_grid");

    // Hull Shader
    mpVars["PerImageCBHull"]["gThreshold"] = mHullZThreshold;
    mpVars["PerImageCBHull"]["gTessFactor"] = (float)mTessFactor;
    mpVars["PerImageCBHull"]["gQuadCountX"] = pRenderData->getTexture("leftIn")->getWidth() / mQuadDivideFactor;
    mpVars->setStructuredBuffer("gDiffResult", mpDepthDiffResultBuffer);

    // Domain Shader
    mpVars["PerImageCBDomain"]["gThirdPersonViewProj"] = mpThirdPersonCam->getViewProjMatrix();
    if (mbUseThirdPersonCam)
        mpVars["PerImageCBDomain"]["gInvRightEyeViewProj"] = glm::inverse(mpScene->getActiveCamera()->getRightEyeViewProjMatrix());
    mpVars->setSampler("gLinearSampler", mpLinearSampler);
    mpVars->setTexture("gDepthTex", pRenderData->getTexture("depth"));

#if _USEGEOSHADER
    // Geometry Shader
    mpVars["PerImageCBGeo"]["gThreshold"] = mGeoZThreshold;
#endif

#if _USETRIANGLECOUNTSHADER
    mpTriangleCountBuffer = StructuredBuffer::create(mpProgram, "gTriangleCount", 1); // quick solution to clear value
    mpVars->setStructuredBuffer("gTriangleCount", mpTriangleCountBuffer);
#endif

    // Pixel Shader
    mpVars["PerImageCBPixel"]["gThreshold"] = mThreshold;
    mpVars["PerImageCBPixel"]["gClearColor"] = mClearColor;
    mpVars->setTexture("gLeftEyeTex", pRenderData->getTexture("leftIn"));

    // Set State Properties
    mpState->setFbo(mpFbo);
    mpState->setProgram(mpProgram);
    pContext->setGraphicsState(mpState);
    pContext->setGraphicsVars(mpVars);

    // Render Screen-Quad only
    if (!mbRayTraceOnly)
    {
        mpGridSceneRenderer->toggleMeshCulling(false);
        mpGridSceneRenderer->renderScene(pContext, mpScene->getActiveCamera().get());
#if _USETRIANGLECOUNTSHADER
        mpTriangleCountBuffer->getVariable(0, 0, mNumTriangles);
        if (mbWriteTriangleCount)
        {
            mTriangleCountData[mTriangleCountDataSteps++] = mNumTriangles;
            if (mTriangleCountDataSteps == _PROFILING_LOG_BATCH_SIZE)
            {
                std::ostringstream logOss, fileOss;
                logOss << "dumping " << "profile_" << "triangleCount" << "_" << mTriangleCountDataFilesWritten;
                logInfo(logOss.str());
                fileOss << "profile_" << "triangleCount" << "_" << mTriangleCountDataFilesWritten++ << ".txt";
                std::ofstream out(fileOss.str().c_str());
                for (int i = 0; i < _PROFILING_LOG_BATCH_SIZE; ++i)
                {
                    out << mTriangleCountData[i] << "\n";
                }
                mTriangleCountDataSteps = 0;
            }
        }
#endif
    }


    Profiler::endEvent("render_grid");

    if (mbFillHoles)
    {
        switch (mHoleFillingMode)
        {
        case Reprojection::RayTrace:
            fillHolesRT(pContext, pRenderData, pDisTex);
            break;
        case Reprojection::ReRaster:
            fillHolesRaster(pContext, pRenderData, pDisTex);
            break;
        default:
            break;
        }
    }

    if (mbCHCEnable)
    {
        computeHoleCount(pContext, pRenderData);
    }
}

inline void Reprojection::fillHolesRT(RenderContext * pContext, const RenderData * pRenderData, const Texture::SharedPtr& pTexture)
{
    Profiler::startEvent("fillholes_rt");

    GraphicsVars* pVars = mpRtVars->getGlobalVars().get();
    ConstantBuffer::SharedPtr pCB = pVars->getConstantBuffer("PerFrameCBRayTrace");

    pCB["gInvView"] = glm::inverse(mpScene->getActiveCamera()->getRightEyeViewMatrix());
    pCB["gInvViewProj"] = glm::inverse(mpScene->getActiveCamera()->getRightEyeViewProjMatrix());
    pCB["gViewportDims"] = vec2(mpFbo->getWidth(), mpFbo->getHeight());
    pCB["gClearColor"] = mClearColor;

    // Shadow
    pCB["gLightViewProj"] = mpLightPass->mpLightCamera->getViewProjMatrix();
    pCB["gBias"] = mpLightPass->mBias;
    pCB["gKernelSize"] = (uint32_t)mpLightPass->mPCFKernelSize;

    // Hit Shader Vars (Shadow Map)
    for (auto pVars : mpRtVars->getHitVars(0))
    {
        pVars->setSampler("gPCFCompSampler", mpLightPass->mpLinearComparisonSampler);
        pVars->setTexture("gShadowMap", pRenderData->getTexture("shadowDepth"));
    }

    if (mpSkyBox != nullptr && mbRenderEnvMap)
    {
        // Miss Shader (Skybox)
        GraphicsVars::SharedPtr missVars = mpRtVars->getMissVars(0);
        missVars->setTexture("gSkybox", mpSkyBox->getTexture());
    }

    // speadangle for cone trace - thesis p. 48
    float fov = 2.f * glm::atan(2.f * glm::atan(1 / mpScene->getActiveCamera()->getRightEyeProjMatrix()[1][1]) * 180 / (float)M_PI);
    float angle = glm::atan((2.f*glm::tan(fov / 2.f)) / mpFbo->getHeight());
    pCB["gSpreadAngle"] = angle;

    mpRtVars->getRayGenVars()->setTexture("gOutput", pTexture);
    mpRtRenderer->renderScene(pContext, mpRtVars, mpRtState, uvec3(mpFbo->getWidth(), mpFbo->getHeight(), 1));

    Profiler::endEvent("fillholes_rt");
}

inline void Reprojection::fillHolesRaster(RenderContext * pContext, const RenderData * pRenderData, const Texture::SharedPtr& pTexture)
{
    Profiler::startEvent("fillholes_raster");

    // G-Buffer with Stencil Mask
    mpReRasterFbo->attachDepthStencilTarget(pRenderData->getTexture("internalDepth"));
    pContext->clearFbo(mpReRasterFbo.get(), vec4(0), 1.f, 0, FboAttachmentType::Color | FboAttachmentType::Depth);
    mpReRasterVars["PerImageCB"]["gStereoTarget"] = (uint32_t)1;
    mpReRasterGraphicsState->setFbo(mpReRasterFbo);
    pContext->setGraphicsState(mpReRasterGraphicsState);
    pContext->setGraphicsVars(mpReRasterVars);
    mpReRasterSceneRenderer->renderScene(pContext, mpScene->getActiveCamera().get());

    // Lighting Hole Fill Pass
    mpReRasterLightingFbo->attachColorTarget(pTexture, 0);
    mpReRasterLightingFbo->attachDepthStencilTarget(pRenderData->getTexture("internalDepth"));
    if (mbDebugClear)
    {
        pContext->clearFbo(mpReRasterLightingFbo.get(), vec4(0), 1.f, 0, FboAttachmentType::Color | FboAttachmentType::Depth);
    }
    updateVariableOffsets(pContext->getGraphicsVars()->getReflection().get());
    setPerFrameData(mpReRasterLightingVars.get());

    mpReRasterLightingVars["PerImageCB"]["gLightViewProj"] = mpLightPass->mpLightCamera->getViewProjMatrix();
    mpReRasterLightingVars["PerImageCB"]["gStereoTarget"] = (uint32_t)1;
    mpReRasterLightingVars["PerImageCB"]["gBias"] = mpLightPass->mBias;
    mpReRasterLightingVars["PerImageCB"]["gKernelSize"] = (uint32_t)mpLightPass->mPCFKernelSize;
    mpReRasterLightingVars->setSampler("gPCFCompSampler", mpLightPass->mpLinearComparisonSampler);

    mpReRasterLightingVars->setTexture("gPos", mpReRasterFbo->getColorTexture(0));
    mpReRasterLightingVars->setTexture("gNorm", mpReRasterFbo->getColorTexture(1));
    mpReRasterLightingVars->setTexture("gDiffuseMatl", mpReRasterFbo->getColorTexture(2));
    mpReRasterLightingVars->setTexture("gSpecMatl", mpReRasterFbo->getColorTexture(3));
    mpReRasterLightingVars->setTexture("gShadowMap", pRenderData->getTexture("shadowDepth"));

    mpReRasterLightingState->setFbo(mpReRasterLightingFbo);
    pContext->pushGraphicsState(mpReRasterLightingState);
    pContext->pushGraphicsVars(mpReRasterLightingVars);

    mpReRasterLightingPass->execute(pContext, mpReRasterLightingState->getDepthStencilState());
    pContext->popGraphicsVars();
    pContext->popGraphicsState();

    Profiler::endEvent("fillholes_raster");
}

void Reprojection::updateVariableOffsets(const ProgramReflection * pReflector)
{
    const ParameterBlockReflection* pBlock = pReflector->getDefaultParameterBlock().get();
    const ReflectionVar* pVar = pBlock->getResource("InternalPerFrameCB").get();
    const ReflectionType* pType = pVar->getType().get();

    sCameraDataOffset = pType->findMember("gCamera.viewMat")->getOffset();
    const auto& pCountOffset = pType->findMember("gLightsCount");
    sLightCountOffset = pCountOffset ? pCountOffset->getOffset() : ConstantBuffer::kInvalidOffset;
    const auto& pLightOffset = pType->findMember("gLights");
    sLightArrayOffset = pLightOffset ? pLightOffset->getOffset() : ConstantBuffer::kInvalidOffset;
}

void Reprojection::setPerFrameData(const GraphicsVars * pVars)
{
    ConstantBuffer* pCB = pVars->getConstantBuffer("InternalPerFrameCB").get();

    // Set camera
    if (mpScene->getActiveCamera())
    {
        mpScene->getActiveCamera()->setIntoConstantBuffer(pCB, sCameraDataOffset);
    }

    // Set light data
    if (sLightArrayOffset != ConstantBuffer::kInvalidOffset)
    {
        for (uint32_t i = 0; i < mpScene->getLightCount(); i++)
        {
            mpScene->getLight(i)->setIntoProgramVars(&*mpReRasterLightingVars, pCB, sLightArrayOffset + (i * Light::getShaderStructSize()));
        }
    }

    // Set light count value
    if (sLightCountOffset != ConstantBuffer::kInvalidOffset)
    {
        pCB->setVariable(sLightCountOffset, mpScene->getLightCount());
    }

    // Set light probe
    if (mpScene->getLightProbeCount() > 0)
    {
        LightProbe::setCommonIntoProgramVars(&*mpReRasterLightingVars, "gProbeShared");
        mpScene->getLightProbe(0)->setIntoProgramVars(&*mpReRasterLightingVars, pCB, "gLightProbe");
    }
}

void Reprojection::computeHoleCount(RenderContext * pContext, const RenderData * pRenderData)
{
    if (!mbCHCInitialized) initializeCHC();

    mpCHCVars->setTexture("gInputTex", pRenderData->getTexture("out"));
    mpCHCVars->setStructuredBuffer("gCHCBuffer", mpCHCBuffer);

    pContext->setComputeState(mpCHCState);
    pContext->setComputeVars(mpCHCVars);

    uint32_t w = (pRenderData->getTexture("out")->getWidth() / 16) + 1;
    uint32_t h = (pRenderData->getTexture("out")->getHeight() / 16) + 1;
    pContext->dispatch(w, h, 1);

    mpCHCBuffer->getVariable(0, 0, mNumHoles);
    mNumHolesPercentage = (float)mNumHoles / (pRenderData->getTexture("out")->getWidth() * pRenderData->getTexture("out")->getHeight()) * 100.0f;

    if (mbCHCWriteToFile)
    {
        mCHCData[mCHCDataSteps++] = mNumHoles;
        if (mCHCDataSteps == _PROFILING_LOG_BATCH_SIZE)
        {
            std::ostringstream logOss, fileOss;
            logOss << "dumping " << "profile_" << "holecount" << "_" << mCHCDataFilesWritten;
            logInfo(logOss.str());
            fileOss << "profile_" << "holecount" << "_" << mCHCDataFilesWritten++ << ".txt";
            std::ofstream out(fileOss.str().c_str());
            for (int i = 0; i < _PROFILING_LOG_BATCH_SIZE; ++i)
            {
                out << mCHCData[i] << "\n";
            }
            mCHCDataSteps = 0;
        }
    }
}

void Reprojection::initializeCHC()
{
    mpCHCProgram = ComputeProgram::createFromFile("HoleCountCompute.slang", "main");
    mpCHCState = ComputeState::create();
    mpCHCState->setProgram(mpCHCProgram);
    mpCHCVars = ComputeVars::create(mpCHCProgram->getReflector());
    mpCHCBuffer = StructuredBuffer::create(mpCHCProgram, "gCHCBuffer", 1);
}

void Reprojection::renderUI(Gui * pGui, const char * uiGroup)
{
    // Tessellation Settings
    pGui->addFloatVar("Hull Threshold", mHullZThreshold);
    if (pGui->addCheckBox("Use Binocular Metric", mbUseBinocularMetric))
    {
        setDefine("_BINOCULAR_METRIC", mbUseBinocularMetric);
        if(mbUseBinocularMetric)
            mpComputeProgram->addDefine("_BINOCULAR_METRIC");
        else
            mpComputeProgram->removeDefine("_BINOCULAR_METRIC");
    }
    pGui->addIntVar("Tessellation", mTessFactor, 1);
    if (pGui->addCheckBox("Use Eight Neighbor", mbUseEightNeighbor))
    {
        setDefine("_EIGHT_NEIGHBOR", mbUseEightNeighbor);
    }

#if _USETRIANGLECOUNTSHADER
    pGui->addIntVar("Number of Grid Triangles", mNumTriangles);
    if (pGui->addCheckBox("Write To File", mbWriteTriangleCount))
    {
        mpMainRenderObject->resetFixedTime();
    }
#endif

    pGui->addSeparator();

    // Quad/Grid Properties
    pGui->addIntVar("Each n Pixel", mQuadDivideFactor, 1);
    pGui->addCheckBox("Half Pixel Offset", mbAddHalfPixelOffset);
    if (pGui->addButton("Update Grid"))
    {
        mpMainRenderObject->onClickResize();
    }
    pGui->addSeparator();

    pGui->addRgbColor("Clear Color", mClearColor);
    if (pGui->addCheckBox("Colorize Disocclusion", mbShowDisocclusion))
    {
        setDefine("_SHOWDISOCCLUSION", mbShowDisocclusion);
    }
    if (mbShowDisocclusion)
    {
        pGui->addFloatSlider("Threshold", mThreshold, 0.f, 0.1f);
    }

#if _USEGEOSHADER
    if (pGui->addCheckBox("Use Geo Shader", mbUseGeoShader))
    {
        setDefine("_DISCARD_TRIANGLES", mbUseGeoShader);
    }
    if (mbUseGeoShader)
    {
        pGui->addFloatSlider("Geo Z Threshold", mGeoZThreshold, 0.f, 0.3f);
    }
#endif

    // Debug Options
    if (pGui->addCheckBox("Third Person Camera", mbUseThirdPersonCam))
    {
        setDefine("_DEBUG_THIRDPERSON", mbUseThirdPersonCam);
    }
    if (pGui->addCheckBox("Wireframe", mbWireFrame))
    {
        mpState->setRasterizerState(mbWireFrame ? mpWireframeRS : mpCullRastState);
    }

    Gui::DropdownList shadingModeList;
    shadingModeList.push_back({ 1, "Per Fragment" });
    shadingModeList.push_back({ 2, "UVs" });
    if (pGui->addDropdown("Shading Mode", shadingModeList, (uint32_t&)mShadingMode))
    {
        mpProgram->removeDefine("_PERFRAGMENT");
        mpProgram->removeDefine("_SHOWUVS");
        switch (mShadingMode)
        {
        case Reprojection::PerFragment:
            mpProgram->addDefine("_PERFRAGMENT");
            break;
        case Reprojection::UVS:
            mpProgram->addDefine("_SHOWUVS");
            break;
        default:
            break;
        }
    }

    pGui->addSeparator();
    pGui->addCheckBox("Hole Filling", mbFillHoles);

    if (mbFillHoles)
    {
        Gui::DropdownList holeFillingModeList;
        holeFillingModeList.push_back({ 1, "Ray Trace" });
        holeFillingModeList.push_back({ 2, "Re-Raster" });
        pGui->addDropdown("Hole Filling Mode", holeFillingModeList, (uint32_t&)mHoleFillingMode);
        switch (mHoleFillingMode)
        {
        case Reprojection::RayTrace:
            if(pGui->addCheckBox("Render Env Map", mbRenderEnvMap))
            {
                if (mbRenderEnvMap)
                {
                    mpRtRenderer->mUpdateShaderState = true;
                    mpRaytraceProgram->addDefine("_RENDER_ENV_MAP");
                }
                else
                {
                    mpRtRenderer->mUpdateShaderState = true;
                    mpRaytraceProgram->removeDefine("_RENDER_ENV_MAP");
                }
            }
            pGui->addCheckBox("Ray Trace Only", mbRayTraceOnly);
            break;
        case Reprojection::ReRaster:
            pGui->addCheckBox("Debug Clear", mbDebugClear);
            break;
        default:
            break;
        }
    }
    else
    {
        pGui->addCheckBox("Compute Hole Count", mbCHCEnable);
        if (pGui->addCheckBox("Write To File", mbCHCWriteToFile))
        {
            mpMainRenderObject->resetFixedTime();
        }
        if (mbCHCEnable)
        {
            pGui->addIntVar("Number of Holes", mNumHoles);
            pGui->addFloatVar("%", mNumHolesPercentage);
        }
    }
}

void Reprojection::onResize(uint32_t width, uint32_t height)
{
    if(mpComputeProgram != nullptr)
        mpDepthDiffResultBuffer = StructuredBuffer::create(mpComputeProgram, "gDiffResult", (width / mQuadDivideFactor)*(height / mQuadDivideFactor));

    generateGrid(width, height);

    Fbo::Desc reRasterFboDesc;
    reRasterFboDesc.
        setColorTarget(0, Falcor::ResourceFormat::RGBA16Float).
        setColorTarget(1, Falcor::ResourceFormat::RGBA16Float).
        setColorTarget(2, Falcor::ResourceFormat::RGBA16Float).
        setColorTarget(3, Falcor::ResourceFormat::RGBA16Float);

    mpReRasterFbo = FboHelper::create2D(width, height, reRasterFboDesc);

    if (mpRtRenderer != nullptr)
    {
        mpRtRenderer->mUpdateShaderState = true;
    }
}

bool Reprojection::onMouseEvent(const MouseEvent & mouseEvent)
{
    bool handled = false;
    if (mbAltPressed)
    {
        handled ? true : mThirdPersonCamController.onMouseEvent(mouseEvent);
    }
    return handled;
}

bool Reprojection::onKeyEvent(const KeyboardEvent & keyEvent)
{
    bool handled = false;
    if (keyEvent.mods.isAltDown) // enable debug cam movment on Alt press
    {
        mbAltPressed = true;
        handled ? true : mThirdPersonCamController.onKeyEvent(keyEvent);
    }
    else
    {
        mbAltPressed = false;
    }
    return handled;
}

void Reprojection::setScene(const std::shared_ptr<Scene>& pScene)
{
    mpScene = std::dynamic_pointer_cast<RtScene>(pScene);

    if (mpRtRenderer != nullptr)
    {
        mpRtVars = RtProgramVars::create(mpRaytraceProgram, mpScene);
        mpRtRenderer = RtStaticSceneRenderer::create(mpScene);
    }

    if (mpScene != nullptr && mpScene->getEnvironmentMap() != nullptr)
    {
        mpSkyBox = SkyBox::create(mpScene->getEnvironmentMap());
    }
    else
    {
        mpSkyBox = nullptr;
    }

    mpReRasterSceneRenderer = SceneRenderer::create(mpScene);
}

void Reprojection::generateGrid(uint32_t width, uint32_t height)
{
    mQuadSizeX = width / mQuadDivideFactor;
    mQuadSizeY = height / mQuadDivideFactor;

    mpGrid = Model::create();

    uint32_t vertexCount = (mQuadSizeX + 1) * (mQuadSizeY + 1);
    uint32_t indexCount = mQuadSizeX * mQuadSizeY * 4;

    float qID = -1.f; // Quad ID Counter
    glm::vec3* vertices = new glm::vec3[vertexCount];
    glm::vec2* uvs = new glm::vec2[vertexCount];
    glm::vec1* quadIDs = new glm::vec1[vertexCount];
    for (uint32_t i = 0, y = mQuadSizeY; y >= 0 && y != 4294967295; y--) {
        for (uint32_t x = 0; x <= mQuadSizeX; x++, i++) {
            vertices[i] = glm::vec3((float)x / (float)mQuadSizeX * 2.f - 1, (float)y / (float)mQuadSizeY * 2.f - 1, 0);
            uvs[i] = glm::vec2((float)x / (float)mQuadSizeX, 1 - (float)y / (float)mQuadSizeY);
            quadIDs[i] = glm::vec1(qID += 1.f);

            // Move vertex position to pixel center
            if (mbAddHalfPixelOffset)
            {
                vertices[i] += glm::vec3(0.5f / (float)width, -0.5f / (float)height, 0);
                uvs[i] += glm::vec2(0.5f / (float)width, 0.5f / (float)height);
            }
        }
        qID -= 1.f; // Jump to next row (here might be a bug)
    }

    uint32_t* quads = new uint32_t[indexCount];
    for (uint32_t qu = 0, vi = 0, y = 0; y < mQuadSizeY; y++, vi++) {
        for (uint32_t x = 0; x < mQuadSizeX; x++, qu += 4, vi++) {
            quads[qu + 3] = vi;
            quads[qu + 2] = vi + 1;
            quads[qu + 1] = vi + mQuadSizeX + 2;
            quads[qu] = vi + mQuadSizeX + 1;
        }
    }

    VertexLayout::SharedPtr pLayout = VertexLayout::create();

    // Vertex Layout
    VertexBufferLayout::SharedPtr pVbLayout = VertexBufferLayout::create();
    pVbLayout->addElement(kLayoutData[0].name, 0, kLayoutData[0].format, 1, 0);
    pLayout->addBufferLayout(0, pVbLayout);

    // UV Layout
    VertexBufferLayout::SharedPtr pUVbLayout = VertexBufferLayout::create();
    pUVbLayout->addElement(kLayoutData[3].name, 0, kLayoutData[3].format, 1, 3);
    pLayout->addBufferLayout(1, pUVbLayout);

    // QuadID Layout
    VertexBufferLayout::SharedPtr pQuadIDLayout = VertexBufferLayout::create();
    pQuadIDLayout->addElement(kLayoutData[8].name, 0, kLayoutData[8].format, 1, 10);
    pLayout->addBufferLayout(2, pQuadIDLayout);

    // Setup index buffer
    std::vector<uint32_t>* indices = new std::vector<uint32_t>;
    indices->reserve(indexCount);
    for (uint32_t i = 0; i < indexCount; i++) {
        indices->push_back(quads[i]);
    }

    const uint32_t size = (uint32_t)(sizeof(uint32_t) * indices->size());
    Buffer::BindFlags bindFlags = Buffer::BindFlags::Index;
    Buffer::SharedPtr pIndexBuffer = Buffer::create(size, bindFlags, Buffer::CpuAccess::None, indices->data());

    // Setup vertex buffer
    std::vector<float>* vertexPosBufferData = new std::vector<float>;
    vertexPosBufferData->reserve(vertexCount * 3);
    for (uint32_t i = 0; i < vertexCount; i++) {
        vertexPosBufferData->push_back(vertices[i].x);
        vertexPosBufferData->push_back(vertices[i].y);
        vertexPosBufferData->push_back(vertices[i].z);
    }

    const uint32_t sizeVertexPos = (uint32_t)(sizeof(uint32_t) * vertexPosBufferData->size());
    Buffer::BindFlags bindFlagsVertexPos = Buffer::BindFlags::Index;
    Buffer::SharedPtr pVertexBuffer = Buffer::create(sizeVertexPos, bindFlagsVertexPos, Buffer::CpuAccess::None, vertexPosBufferData->data());

    // Setup UV buffer
    std::vector<float>* uvBufferData = new std::vector<float>;
    uvBufferData->reserve(vertexCount * 3);
    for (uint32_t i = 0; i < vertexCount; i++) {
        uvBufferData->push_back(uvs[i].x);
        uvBufferData->push_back(uvs[i].y);
        uvBufferData->push_back(0); // third 0 needed because of comment "//for some reason this is rgb"
    }

    const uint32_t sizeUVs = (uint32_t)(sizeof(uint32_t) * uvBufferData->size());
    Buffer::BindFlags bindFlagsUVs = Buffer::BindFlags::Index;
    Buffer::SharedPtr pUVBuffer = Buffer::create(sizeUVs, bindFlagsUVs, Buffer::CpuAccess::None, uvBufferData->data());

    // Setup QuadID Buffer
    std::vector<float>* quadIDBufferData = new std::vector<float>;
    quadIDBufferData->reserve(vertexCount);
    for (uint32_t i = 0; i < vertexCount; i++) {
        quadIDBufferData->push_back(quadIDs[i].x);
    }

    const uint32_t sizeQuadIDs = (uint32_t)(sizeof(uint32_t) * quadIDBufferData->size());
    Buffer::BindFlags bindFlagsQuadIDs = Buffer::BindFlags::Index;
    Buffer::SharedPtr pQuadIDBuffer = Buffer::create(sizeQuadIDs, bindFlagsQuadIDs, Buffer::CpuAccess::None, quadIDBufferData->data());

    // Set buffers
    std::vector<Buffer::SharedPtr> pVBs(3);
    pVBs[0] = pVertexBuffer;
    pVBs[1] = pUVBuffer;
    pVBs[2] = pQuadIDBuffer;

    // Set a bounding box - is not needed -> can be empty at the end
    BoundingBox* box = new BoundingBox();
    box->center = glm::vec3(0);
    box->extent = glm::vec3(50);

    // Create Mesh and set as mesh instance into mpGrid
    Mesh::SharedPtr pMesh = Mesh::create(pVBs, vertexCount, pIndexBuffer, indexCount, pLayout, Vao::Topology::Quad4Patch, Material::create("Empty"), *box, false);
    mpGrid->addMeshInstance(pMesh, glm::mat4());

    // Create Scene and SceneRenderer for Grid to render
    mpGridScene = Scene::create();
    mpGridScene->addModelInstance(mpGrid, "Grid");
    mpGridSceneRenderer = SceneRenderer::create(mpGridScene);

    delete[] vertices;
    delete[] uvs;
    delete[] quads;
    delete[] quadIDs;
    delete indices;
    delete vertexPosBufferData;
    delete uvBufferData;
}

void Reprojection::setDefine(std::string pName, bool flag)
{
    if (flag)
    {
        mpProgram->addDefine(pName);
    }
    else
    {
        mpProgram->removeDefine(pName);
    }
}

Dictionary Reprojection::getScriptingDictionary() const
{
    return Dictionary();
}
