/***************************************************************************
 # Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "FalcorExperimental.h"

 // Use geometry shader to discard triangles that cover regions with high depth discontinuity - but GS slows down whole rendering pipeline
#define _USEGEOSHADER 0
// Enables function to count all present triangles in the warping grid per frame
#define _USETRIANGLECOUNTSHADER 0

using namespace Falcor;

class ReprojectionPass : public RenderPass, public inherit_shared_from_this<RenderPass, ReprojectionPass>
{
public:
    using SharedPtr = std::shared_ptr<ReprojectionPass>;
    using inherit_shared_from_this::shared_from_this;

    /** Create a new render pass object.
        \param[in] pRenderContext The render context.
        \param[in] dict Dictionary of serialized parameters.
        \return A new object, or an exception is thrown if creation failed.
    */
    static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});

    virtual std::string getDesc() override { return "Reprojection Pass"; }
    virtual Dictionary getScriptingDictionary() override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pContext, const CompileData& compileData) override {}
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override {}
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

    //Re-Raster Lighting
    static size_t lightArrayOffset;
    static size_t lightCountOffset;
    static size_t cameraDataOffset;

private:
    ReprojectionPass() = default;
    //ReprojectionPass() : RenderPass("Reprojection") {}

    enum : uint32_t
    {
        PerFragment = 1,
        UVS
    } shadingMode = PerFragment;

    enum : uint32_t
    {
        RayTrace = 1,
        ReRaster
    } holeFillingMode = RayTrace;

    void initialize(const RenderData* pRenderData);

    //Generates the full screen grid defined by window size and mQuadDivideFactor (16x)
    void generateGrid(uint32_t width, uint32_t height);

    //Helper function to set different shader defines
    void setDefine(std::string name, bool flag);

    //Invokes RT hole-filling after reprojection
    inline void fillHolesRT(RenderContext* pContext, const RenderData* pRenderData, const Texture::SharedPtr& pTexture);

    //Invokes stencil raster hole-filling after reprojection
    inline void fillHolesRaster(RenderContext* pContext, const RenderData* pRenderData, const Texture::SharedPtr& pTexture);

    //Re-Raster Lighting
    void updateVariableOffsets(const ProgramReflection* pReflector);
    void setPerFrameData(const GraphicsVars* pGraphicsVars);

    //Compute Hole Count
    void computeHoleCount(RenderContext* pContext, const RenderData* pRenderData);
    void initializeCHC();

    Scene::SharedPtr pScene;
    //SkyBox::SharedPtr pSkyBox; @fix

    //Reprojection - Grid Warp
    uint32_t                     quadSizeX = 0;
    uint32_t                     quadSizeY = 0;
    //Model::SharedPtr             grid; @fix
    Scene::SharedPtr             pGridScene;
    //SceneRenderer::SharedPtr     pGridSceneRenderer; @fix
    Fbo::SharedPtr               pFbo;
    GraphicsVars::SharedPtr      pGridWarpVars;
    GraphicsProgram::SharedPtr   pGridWarpProgram;
    GraphicsState::SharedPtr     pGridWarpState;
    RasterizerState::SharedPtr   pWireFrameRS = nullptr;
    RasterizerState::SharedPtr   pCullRasterState = nullptr;
    Sampler::SharedPtr           pLinearSampler = nullptr;
    DepthStencilState::SharedPtr pNoDepthDS;
    float3                       clearColor = float3(0, 0, 0);
    DepthStencilState::SharedPtr pDepthTestDS;

    //Compute Programe (depth factor calculation)
    ComputeProgram::SharedPtr   pComputeProgram;
    ComputeState::SharedPtr     pComputeState;
    ComputeVars::SharedPtr      pComputeVars;
    //StructuredBuffer::SharedPtr pDepthDiffResultBuffer; @fix
    float                       hullZThreshold = 0.997f; //0.992 maybe equal to geo threshold
    int32_t                     tessFactor = 16;
    bool                        useEightNeighbor = false;

    //Ray Tracing
    RtProgram::SharedPtr             pRaytraceProgram = nullptr;
    RtProgramVars::SharedPtr         pRtProgramVars;
    //RtState::SharedPtr               pRtState; @fix
    //RtStaticSceneRenderer::SharedPtr pRtRenderer; @fix

    //Re-Raster G-Buffer
    //SceneRenderer::SharedPtr   pReRasterSceneRenderer; @fix
    Fbo::SharedPtr             pReRasterFbo;
    GraphicsProgram::SharedPtr pReRasterProgram;
    GraphicsVars::SharedPtr    pReRasterVars;
    GraphicsState::SharedPtr   pReRasterState;

    //Re-Raster Lighting
    Fbo::SharedPtr            pReRasterLightingFbo;
    GraphicsVars::SharedPtr   pReRasterLightingVars;
    GraphicsState::SharedPtr  pReRasterLightingState;
    FullScreenPass::SharedPtr pReRasterLightingPass; //@fix was UniquePtr before

#if _USETRIANGLECOUNTSHADER
    //Buffer to count processed triangles
    StructuredBuffer::SharedPtr pTriangleCountBuffer;
    int32_t                     numtriangles = 0;
    int32_t                     triangleCountData[_PROFILING_LOG_BATCH_SIZE];
    bool                        writeTriangleCount = false;
    int                         triangleCountDataSteps = 0;
    int                         triangleCountDataFilesWritten = 0;
#endif

    //Compute Program for Hole Count
    ComputeProgram::SharedPtr   pCHCProgram;
    ComputeState::SharedPtr     pCHCState;
    ComputeVars::SharedPtr      pCHCVars;
    //StructuredBuffer::SharedPtr pCHCBuffer; @fix
    bool                        CHCInitialized = false;
    bool                        CHCEnable = false;
    bool                        CHCWriteToFile = false;
    int32_t                     numHoles = 0;
    int32_t                     CHCData[_PROFILING_LOG_BATCH_SIZE];
    float                       numHolesPercentage = 0.0f;
    int                         CHCDataSteps = 0;
    int                         CHCDataFilesWritten = 0;

    //Third Person Debug Camera
    Camera::SharedPtr           pThirdPersonCam = nullptr;
    FirstPersonCameraController thirdPersoncamController;
    bool                        altPressed = false;

    //GUI vars
    bool    showDisocclusion = false;
    bool    useGeoShader = true;
    bool    isInitialized = false;
    bool    wireFrame = false;
    bool    addHalfPixelOffset = true;
    bool    useThirdpersonCam = false;
    bool    fillHoles = true;
    bool    rayTraceOnly = false;
    bool    renderEnvMap = false;
    bool    debugClear = false;
    bool    useBinocularMetric = true;
    float   threshold = 0.008f;
    float   geoZThreshold = 0.01f;
    int32_t quadDivideFactor = 16;
    
};
