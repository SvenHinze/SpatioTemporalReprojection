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

  Authors: Niko Wissmann
 */

#pragma once
#include "Falcor.h"
#include "FalcorExperimental.h"
#include "../DeferredRenderer.h"
#include "Lighting.h"
#include "../RtStaticSceneRenderer.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>

// Use geometry shader to discard triangles that cover regions with high depth discontinuity - but GS slows down whole rendering pipeline
#define _USEGEOSHADER 0
// Enables function to count all present triangles in the warping grid per frame
#define _USETRIANGLECOUNTSHADER 0

using namespace Falcor;

// Responsible for the reprojection main feature including ray traced hole-filling
class Reprojection : public RenderPass, inherit_shared_from_this<RenderPass, Reprojection>
{
public:

    using SharedPtr = std::shared_ptr<Reprojection>;
    std::string getDesc(void) override { return "Reprojection Pass"; }

    static SharedPtr create(const Dictionary& params = {});

    RenderPassReflection reflect() const override;
    void execute(RenderContext* pContext, const RenderData* pRenderData) override;
    void renderUI(Gui* pGui, const char* uiGroup) override;
    Dictionary getScriptingDictionary() const override;
    void onResize(uint32_t width, uint32_t height) override;
    void setScene(const std::shared_ptr<Scene>& pScene) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;

    DeferredRenderer* mpMainRenderObject;
    Lighting::SharedPtr mpLightPass;

    // Re-Raster Lighting
    static size_t sLightArrayOffset;
    static size_t sLightCountOffset;
    static size_t sCameraDataOffset;

private:
    Reprojection() : RenderPass("Reprojection") {}

    enum : uint32_t
    {
        PerFragment = 1,
        UVS
    } mShadingMode = PerFragment;

    enum : uint32_t
    {
        RayTrace = 1,
        ReRaster
    } mHoleFillingMode = RayTrace;

    void initialize(const RenderData * pRenderData);

    // Genreates the full screen grid defined by window size and mQuadDivideFactor (16x)
    void generateGrid(uint32_t width, uint32_t height);

    // Helper function to set different shader defines
    void setDefine(std::string pName, bool flag);

    // Invokes RT hole-filling after reprojection
    inline void fillHolesRT(RenderContext * pContext, const RenderData * pRenderData, const Texture::SharedPtr& pTexture);

    // Invokes stencil raster hole-filling after reprojection
    inline void fillHolesRaster(RenderContext * pContext, const RenderData * pRenderData, const Texture::SharedPtr& pTexture);

    // Re-Raster Lighting
    void updateVariableOffsets(const ProgramReflection* pReflector);
    void setPerFrameData(const GraphicsVars* pVars);

    // Compute Hole Count
    void computeHoleCount(RenderContext * pContext, const RenderData * pRenderData);
    void initializeCHC();

    RtScene::SharedPtr          mpScene;
    SkyBox::SharedPtr           mpSkyBox;

    // Reprojection - Grid Warp
    uint32_t                    mQuadSizeX = 0, mQuadSizeY = 0;
    Model::SharedPtr            mpGrid;
    Scene::SharedPtr            mpGridScene;
    SceneRenderer::SharedPtr    mpGridSceneRenderer;
    Fbo::SharedPtr              mpFbo;
    GraphicsVars::SharedPtr     mpVars;
    GraphicsProgram::SharedPtr  mpProgram;
    GraphicsState::SharedPtr    mpState;
    RasterizerState::SharedPtr  mpWireframeRS = nullptr;
    RasterizerState::SharedPtr  mpCullRastState = nullptr;
    Sampler::SharedPtr          mpLinearSampler = nullptr;
    DepthStencilState::SharedPtr mpNoDepthDS;
    glm::vec3                   mClearColor = glm::vec3(0, 0, 0);
    DepthStencilState::SharedPtr mpDepthTestDS;

    // Compute Program (depth factor calculation)
    ComputeProgram::SharedPtr mpComputeProgram;
    ComputeState::SharedPtr mpComputeState;
    ComputeVars::SharedPtr mpComputeProgVars;
    StructuredBuffer::SharedPtr mpDepthDiffResultBuffer;
    float mHullZThreshold = 0.997f; // 0.992 maybe equal to geo threshold
    int32_t mTessFactor = 16;
    bool mbUseEightNeighbor = false;

    // Ray Tracing
    RtProgram::SharedPtr mpRaytraceProgram = nullptr;
    RtProgramVars::SharedPtr mpRtVars;
    RtState::SharedPtr mpRtState;
    RtStaticSceneRenderer::SharedPtr mpRtRenderer;

    // Re-Raster G-Buffer
    SceneRenderer::SharedPtr                mpReRasterSceneRenderer;
    Fbo::SharedPtr                          mpReRasterFbo;
    GraphicsProgram::SharedPtr              mpReRasterProgram;
    GraphicsVars::SharedPtr                 mpReRasterVars;
    GraphicsState::SharedPtr                mpReRasterGraphicsState;

    // Re-Raster Lighting
    Fbo::SharedPtr              mpReRasterLightingFbo;
    GraphicsVars::SharedPtr     mpReRasterLightingVars;
    GraphicsState::SharedPtr    mpReRasterLightingState;
    FullScreenPass::UniquePtr   mpReRasterLightingPass;

#if _USETRIANGLECOUNTSHADER
    // Buffer to count processed triangles
    StructuredBuffer::SharedPtr mpTriangleCountBuffer;
    int32_t mNumTriangles = 0;
    bool mbWriteTriangleCount = false;
    int32_t mTriangleCountData[_PROFILING_LOG_BATCH_SIZE];
    int mTriangleCountDataSteps = 0;
    int mTriangleCountDataFilesWritten = 0;
#endif

    // Compute Program for Hole Count
    ComputeProgram::SharedPtr mpCHCProgram;
    ComputeState::SharedPtr mpCHCState;
    ComputeVars::SharedPtr mpCHCVars;
    StructuredBuffer::SharedPtr mpCHCBuffer;
    bool mbCHCInitialized = false;
    bool mbCHCEnable = false;
    int32_t mNumHoles = 0;
    float mNumHolesPercentage = 0.0f;
    bool mbCHCWriteToFile = false;
    int32_t mCHCData[_PROFILING_LOG_BATCH_SIZE];
    int mCHCDataSteps = 0;
    int mCHCDataFilesWritten = 0;

    // Third Person Debug Camera
    Camera::SharedPtr           mpThirdPersonCam = nullptr;
    FirstPersonCameraController mThirdPersonCamController;
    bool mbAltPressed = false; // control cam only while Alt is pressed

    // GUI vars
    bool mbShowDisocclusion = false;
    float mThreshold = 0.008f;
    bool mbUseGeoShader = true;
    float mGeoZThreshold = 0.010f;
    bool mIsInitialized = false;
    bool mbWireFrame = false;
    int32_t mQuadDivideFactor = 16;
    bool mbAddHalfPixelOffset = true;
    bool mbUseThirdPersonCam = false;
    bool mbFillHoles = true;
    bool mbRayTraceOnly = false;
    bool mbRenderEnvMap = false;
    bool mbDebugClear = false;
    bool mbUseBinocularMetric = true;
};

