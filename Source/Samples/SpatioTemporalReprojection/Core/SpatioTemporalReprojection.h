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
#include "../RenderPasses/RenderPasses.h";
#include "StereoCameraController.h"

// Override all base texture with rainbow test texture to visualize Mip-Levels
#define USERAINBOW 0

using namespace Falcor;

class SpatioTemporalReprojection : public IRenderer
{
public:

    enum StereoTarget : uint32_t
    {
        Left = 1,
        Right
    } stereoTarget = Left;

    bool sideBySide = true;

private:

    static const std::string startupScene;

    enum RenderMode : uint32_t
    {
        RenderToScreen = 1,
        RenderToHMD
    } renderMode = RenderToScreen;

    enum OutputImage: uint32_t
    {
        Both = 1,
        Left,
        Right
    } outputImage = Both;

    //SampleCallbacks* sample; @fix see if needed
    RenderGraph::SharedPtr renderGraph;
    StereoCameraController camController; //@fix
    bool altPressed = false;
    Fbo::SharedPtr hmdFbo;
    Texture::SharedPtr rainbowTexture;

    //@fix VR support doesn't exist anymore, consult with martin
    //HmdCameraController hmdCamController;
    //VRSystem* vrSystem;

    uint32_t stereoCamIndex = 0;

    bool useFXAA         = false;
    bool vrRunning       = false;
    bool useReprojection = false;
    bool cropOutput      = false;
    bool useCameraPath   = false;

    std::string leftOutput  = "ToneMapping_Left.dst";
    std::string rightOutput = "ToneMapping_Right.dst";

    //fixed upadte and helper vars for performance measurement
    bool useFixedUpdate     = false;
    bool fixedRunning       = true;
    bool measurementRunning = false;
    float fixedSpeed        = 1.0f;
    double fixedFrameTime   = 0.0;
    int32_t frameCount      = 0;
    int32_t numCyclesToRun  = 5;
    int32_t numCycles       = 0;
    int32_t lightCount      = 0;

public:

    void onLoad(RenderContext* renderContext) override;
    void onFrameRender(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo) override;
    void onShutdown() override;
    void onResizeSwapChain(uint32_t width, uint32_t height) override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;
    void onHotReload(HotReloadFlags reloaded) override;
    void onGuiRender(Gui* gui) override;

    void onClickResize();
    void resetFixedTime();

private:

    void loadScene(const std::string& filename);
    void updateValues();
    void initVR(Fbo* targetFbo);
    void applyCameraPathState();

    //Plain Stereo
    void renderToScreenSimple(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);
    void renderToHMDSimple(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);

    //Reprojection Pass
    void RenderToScreenReprojected(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);
    void renderToHMDReprojected(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);

    std::string getSolutionDirectory();

    void startMeasurement();
    void stopMeasurement();

};
