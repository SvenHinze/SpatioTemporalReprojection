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
#include "SpatioTemporalReprojection.h"

uint32_t mSampleGuiWidth = 250;
uint32_t mSampleGuiHeight = 200;
uint32_t mSampleGuiPositionX = 20;
uint32_t mSampleGuiPositionY = 40;

const glm::vec4 clearColor = glm::vec4(1.0f, 0, 0, 1.0f);
const bool initOpenVR = true;

void SpatioTemporalReprojection::onLoad(RenderContext* renderContext)
{
    if(gpDevice->isFeatureSupported(Device::SupportedFeatures::Raytracing) == false)
    {
        logError("Device does not support raytracing!"); //originally this was logErrorAndExit but that function does not exist anymore
    }

    renderGraph = RenderGraph::create("SpatioTemporalReprojection");

    //G-Buffer
    renderGraph->addPass(GBufferRaster::create(), "GBuffer");

    //Simple Shadow Pre-Pass
    SimpleShadowPass::SharedPtr shadowPass = SimpleShadowPass::create();
    shadowPass->mpMainRenderObject = this;
    renderGraph->addPass(shadowPass, "SimpleShadowPass");

    //Main Lighting/Shading Pass
    Lighting::SharedPtr lightPass = Lighting::create();
    lightPass->mpLightCamera = shadowPass->mpLightCamera;
    renderGraph->addPass(lightPass, "Light");

    //Reprojection Pass (Raster and Ray Trace)
    Reprojection::SharedPtr reprojectionPass = Reprojection::create();
    reprojectionPass->mpMainRenderObject = this;
    reprojectionPass->mpLightPass = lightPass;
    renderGraph->addPass(reprojectionPass, "Reprojection");

    //@fix FXAA and ToneMapping no longer exist, might not be relevant when switching to forward pipeline though
    //FXAA Pass Left
    //FXAA::SharedPtr fxaaPassLeft = FXAA::create();
    //renderGraph->addPass(fxaaPassLeft, "FXAA_Left");

    //FXAA Pass Right
    //FXAA::SharedPtr fxaaPassRight = FXAA:create();
    //renderGraph->addPass(fxaaPassRight, "FXAA_Right");

    //Tone Mapping Left
    //ToneMapping::SharedPtr toneMappingLeft = ToneMapping::create();
    //renderGraph->addPass(toneMappingLeft, "ToneMapping_Left");

    //Tone Mapping Right
    //ToneMapping::SharedPtr toneMappingRight = ToneMapping::create();
    //renderGraph->addPass(toneMappingRight, "ToneMapping_Right");

    //Links for Lighting Pass
    renderGraph->addEdge("GBuffer.posW", "Light.posW");
    renderGraph->addEdge("GBuffer.normW", "Light.normW");
    renderGraph->addEdge("GBuffer.diffuseOpacity", "Light.diffuseOpacity");
    renderGraph->addEdge("GBuffer.specRough", "Light.specRough");
    renderGraph->addEdge("SimpleShadowPass.depthStencil", "Light.ShadowDepth");

    //Links for Reprojection Pass
    renderGraph->addEdge("GBuffer.depthStencil", "Reprojection.depth");
    renderGraph->addEdge("GBuffer.normW", "Reprojection.gbufferNormal");
    renderGraph->addEdge("GBuffer.posW", "Reprojection.gbufferPosition");
    renderGraph->addEdge("Light.out", "Reprojection.leftIn");
    renderGraph->addEdge("SimpleShadowPass.depthStencil", "Reprojection.shadowDepth");

    //FXAA Links
    //renderGraph->addEdge("Light.out", "FXAA_Left.src");
    //renderGraph->addEdge("Reprojection.out", "FXAA_Right.src");

    //Tone Mapping Links
    //renderGraph->addEdge("Light.out", "ToneMapping_Left.src");
    //renderGraph->addEdge("Reprojection.out", "ToneMapping_Right.src");

    renderGraph->markOutput("Light.out");

    if(useReprojection)
    {
        renderGraph->markOutput("Reprojection.out");
    }

    //if(useFXAA)
    //{
    //    renderGraph->markOutput("FXAA_Left.dst");
    //    renderGraph->markOutput("FXAA_Right.dst");
    //}

    renderGraph->onResize(gpFramework->getTargetFbo().get()); //TODO try if this does the same

#if USERAINBOW
    rainbowTex = createTextureFromFile(getSolutionDirectory() + "TestData\\rainbow_tex.dds", false, true, Resource::BindFlags::ShaderResource);
#endif

    assert(renderGraph != nullptr);

    std::string filename;
    if(openFileDialog(Scene::kFileExtensionFilters, filename))
    {
        loadScene(filename);
    }
}

//TODO
void SpatioTemporalReprojection::onFrameRender(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    if(measurementRunning)
    {
        if(frameCount >= _PROFILING_LOG_BATCH_SIZE)
        {
            resetFixedTime();
            numCycles++;
        }

        if(numCycles > numCyclesToRun)
        {
            stopMeasurement();
        }
    }

    switch(renderMode)
    {
    case RenderMode::RenderToScreen:
        updateValues();
        renderContext->clearFbo(targetFbo.get(), glm::vec4(0), 1.0f, 0, FboAttachmentType::Color);

        if(renderGraph->getScene() != nullptr)
        {
            stereoTarget = StereoTarget::Left;
            camController.update();

            if(useFixedUpdate)
            {
                renderGraph->getScene()->update(renderContext, fixedFrameTime);

                if(fixedRunning)
                {
                    frameCount++;
                }

                fixedFrameTime = frameCount * (double)fixedSpeed;
            }
            else
            {
                //renderGraph->getScene()->update(pRenderContext, pSample->getCurrentTime()); @fix find out how to substitute (maybe global clock)
            }

            renderGraph->execute(renderContext);

            if(useReprojection)
            {
                renderToScreenReprojected(renderContext, targetFbo);
            }
            else
            {
                renderToScreenSimple(renderContext, targetFbo);
            }
        }
        break;

    case RenderMode::RenderToHMD:
        if(useReprojection)
        {
            renderToHMDReprojected(renderContext, targetFbo);
        }
        else
        {
            renderToHMDSimple(renderContext, targetFbo);
        }
        break;

    default:
        break;
    }
}

void SpatioTemporalReprojection::renderToScreenSimple(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    if(sideBySide)
    {
        switch(outputImage)
        {
        case OutputImage::Both:
            glm::uvec4 rectSrc     = glm::uvec4(currentFboWidth() / 4, 0, currentFboWidth() * 0.75f, currentFboHeight());
            glm::uvec4 leftRectDst = glm::uvec4(0, 0, currentFboWidth() / 2, currentFboHeight());
            renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0), cropOutput ? rectSrc : glm::uvec4(-1), leftRectDst);

            stereoTarget = StereoTarget::Right;
            renderGraph->execute(renderContext);                                              //adding 1/2 + 1/2 should be redundant
            glm::uvec4 rightRect = glm::uvec4(currentFboWidth() / 2, 0, currentFboWidth() / 2 + currentFboWidth() / 2, currentFboHeight());
            renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0), cropOutput ? rectSrc : glm::uvec4(-1), rightRect);

            break;

        case OutputImage::Left:
            renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0));
            stereoTarget = StereoTarget::Right;
            renderGraph->execute(renderContext);
            break;

        case OutputImage::Right:
            stereoTarget = StereoTarget::Right;
            renderGraph->execute(renderContext);
            renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0));
            break;
        }
    }
    else
    {
        renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0));
    }
}

void SpatioTemporalReprojection::renderToHMDSimple(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    if(!vrRunning)
    {
        initVR(gpFramework->getTargetFbo().get());
    }

    //VRSystem::instance()->refresh(); @fix VRSystem no longer exists
    updateValues();

    renderContext->clearFbo(hmdFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::Color);
    renderContext->clearFbo(targetFbo.get(), glm::vec4(0), 1.0f, 0, FboAttachmentType::Color);

    if(renderGraph->getScene() != nullptr)
    {
        //renderContext->getGraphicsState()->setFbo(hmdFbo); @fix getGraphicsState() no longer exists

        //left eye
        stereoTarget = StereoTarget::Left;

        //hmdCamController.update(); @fix

        //renderGraph->getScene()->update(renderContext, pSample->getCurrentTime()); @fix
        renderGraph->execute(renderContext);

        glm::uvec4 rectSrc = glm::uvec4(currentFboWidth() / 4, 0, currentFboWidth() * 0.75f, currentFboHeight());
        renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), hmdFbo->getRenderTargetView(0));

        //right eye
        stereoTarget = StereoTarget::Right;
        renderGraph->execute(renderContext);

        renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), hmdFbo->getRenderTargetView(1));

        //@fix
        //vrSystem->submit(VRDisplay::Eye::Left, hmdFbo->getColorTexture(0), renderContext);
        //vrSystem->submit(VRDisplay::Eye::Right, hmdFbo->getColorTexture(1), renderContext);

        

        glm::uvec4 rectTarget = glm::uvec4(0, 0, hmdFbo->getWidth(), hmdFbo->getHeight());
        renderContext->blit(hmdFbo->getColorTexture(0)->getSRV(), targetFbo->getRenderTargetView(0), glm::uvec4(-1), rectTarget);
    }
}

void SpatioTemporalReprojection::renderToScreenReprojected(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    if(sideBySide)
    {
        switch(outputImage)
        {
        case OutputImage::Both:
            //left eye
            glm::uvec4 rectSrc = glm::uvec4(currentFboWidth() / 4, 0, currentFboWidth() * 0.75f, currentFboHeight());
            glm::uvec4 leftRectDst = glm::uvec4(0, 0, currentFboWidth() / 2, currentFboHeight());
            renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0), cropOutput ? rectSrc : glm::uvec4(-1), leftRectDst);

            //right eye                                                 should be redundant
            glm::uvec4 rightRect = glm::uvec4(currentFboWidth() / 2, 0, currentFboWidth() / 2 + currentFboWidth() / 2, currentFboHeight());
            renderContext->blit(renderGraph->getOutput(rightOutput)->getSRV(), targetFbo->getRenderTargetView(0), cropOutput ? rectSrc : glm::uvec4(-1), rightRect);
            break;

        case OutputImage::Left:
            renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0));
            break;

        case OutputImage::Right:
            renderContext->blit(renderGraph->getOutput(rightOutput)->getSRV(), targetFbo->getRenderTargetView(0));
            break;
        }
    }
    else
    {
        renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), targetFbo->getRenderTargetView(0));
    }
}

void SpatioTemporalReprojection::renderToHMDReprojected(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    if(!vrRunning)
    {
        initVR(gpFramework->getTargetFbo().get());
    }

    //VRSystem::instance()->refresh(); @fix
    updateValues();

    renderContext->clearFbo(hmdFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::Color);
    renderContext->clearFbo(targetFbo.get(), glm::vec4(0), 1.0f, 0, FboAttachmentType::Color);

    if(renderGraph->getScene() != nullptr)
    {
        //renderContext->getGraphicsState()->setFbo(hmdFbo); @fix

        //left eye
        stereoTarget = StereoTarget::Left;

        //hmdCamController.update(); @fix
        //renderGraph->getScene()->update(renderContext, pSample->getCurrentTime()); @fix
        renderGraph->execute(renderContext);

        glm::uvec4 rectSrc = glm::uvec4(currentFboWidth() / 4, 0, currentFboWidth() * 0.75f, currentFboHeight());
        renderContext->blit(renderGraph->getOutput(leftOutput)->getSRV(), hmdFbo->getRenderTargetView(0));

        //right eye?
        renderContext->blit(renderGraph->getOutput(rightOutput)->getSRV(), hmdFbo->getRenderTargetView(1));

        //@fix
        //vrSystem->submit(VRDisplay::Eye::Left, hmdFbo->getColorTexture(0), renderContext);
        //vrSystem->submit(VRDisplay::Eye::Right, hmdFbo->getColorTexture(1), renderContext);

        glm::uvec4 rectTarget = glm::uvec4(0, 0, hmdFbo->getWidth(), hmdFbo->getHeight());
        renderContext->blit(hmdFbo->getColorTexture(1)->getSRV(), targetFbo->getRenderTargetView(0), glm::uvec4(-1), rectTarget);
    }
}

void SpatioTemporalReprojection::onShutdown()
{
}

void SpatioTemporalReprojection::onResizeSwapChain(uint32_t width, uint32_t height)
{
if(renderGraph)
{
    switch(renderMode)
    {
    case RenderMode::RenderToScreen:
        if(renderGraph->getScene() != nullptr)
        {
            renderGraph->getScene()->setCameraAspectRatio((float)width / (float)height);
        }

        renderGraph->onResize(gpFramework->getTargetFbo().get());
        break;

    case RenderMode::RenderToHMD:
        initVR(gpFramework->getTargetFbo().get());
        break;

    default:
        break;
    }
}
}

bool SpatioTemporalReprojection::onKeyEvent(const KeyboardEvent& keyEvent)
{
    bool handled = false;

    if(renderGraph->getScene() != nullptr)
    {
        handled = renderGraph->onKeyEvent(keyEvent);
    }

    if(!keyEvent.mods.isAltDown)
    {
        altPressed = false;

        switch(renderMode)
        {
        case RenderMode::RenderToScreen:
            //TODO there are no returns in nikos code, but the same structure can be seen in mouseEvent
            return handled ? true : camController.onKeyEvent(keyEvent);

        case RenderMode::RenderToHMD:
            //return handled ? true : hmdCamController.onKeyEvent(keyEvent); @fix
        }
    }
    else
    {
        altPressed = true;
    }

    return handled;
}

bool SpatioTemporalReprojection::onMouseEvent(const MouseEvent& mouseEvent)
{
    bool handled = false;

    if(renderGraph->getScene() != nullptr)
    {
        handled = renderGraph->onMouseEvent(mouseEvent);
    }

    if(!altPressed)
    {
        switch(renderMode)
        {
        case RenderMode::RenderToScreen:
            return handled ? true : camController.onMouseEvent(mouseEvent);
        case RenderMode::RenderToHMD:
            //return handled ? true : hmdCamController.onMouseEvent(mouseEvent); @fix
        }
    }

    return handled;
}

void SpatioTemporalReprojection::onHotReload(HotReloadFlags reloaded)
{
    onClickResize();
}

void SpatioTemporalReprojection::onGuiRender(Gui* gui)
{
    Gui::Window guiWindow(gui, "Falcor", { 250, 200 });
    gpFramework->renderGlobalUI(gui);

    if(guiWindow.button("Load Scene"))
    {
        assert(renderGraph != nullptr);

        std::string filename;
        if(openFileDialog(Scene::kFileExtensionFilters, filename))
        {
            loadScene(filename);
        }
    }

    //guiWindow.var("Light Count", lightCount);

    if(guiWindow.checkbox("Use Camera Path", useCameraPath))
    {
        applyCameraPathState();
    }

    if(renderMode == RenderMode::RenderToScreen)
    {
        guiWindow.checkbox("Use Fixed Update", useFixedUpdate);

        if(useFixedUpdate)
        {
            guiWindow.var("Frame Count", frameCount, 0);
            guiWindow.var("Fixed Speed", fixedSpeed, 0.0f, 2.0f);
            float fixTime = (float)fixedFrameTime;
            guiWindow.var("Current Fixed Time", fixTime);

            if(guiWindow.button("Reset"))
            {
                resetFixedTime();
            }

            if(guiWindow.button("Stop", true))
            {
                fixedRunning = false;
            }

            if(guiWindow.button("Start", true))
            {
                fixedRunning = true;
            }

            std::string sampleText = std::to_string(_PROFILING_LOG_BATCH_SIZE) + " Samples";
            guiWindow.text(sampleText.c_str());
            guiWindow.var("Measure Cycles", numCyclesToRun);

            if(guiWindow.button("Start Measurement"))
            {
                startMeasurement();
            }

            if(guiWindow.button("Stop Measurement", true))
            {
                stopMeasurement();
            }
        }
    }

    guiWindow.separator();

    if(guiWindow.checkbox("Reprojection Pass", useReprojection))
    {
        if(useReprojection)
        {
            renderGraph->markOutput("Reprojection.out");
        }
        else
        {
            renderGraph->unmarkOutput("Reprojection.out");
        }

        onClickResize();
    }

    Gui::DropdownList renderModeList;
    renderModeList.push_back({ 1, "Render To Screen" });

    if(initOpenVR)
    {
        renderModeList.push_back({ 2, "Render To HMD" });
    }

    guiWindow.dropdown("Render Mode", renderModeList, (uint32_t&)renderMode);

    switch(renderMode)
    {
    case RenderMode::RenderToScreen:
        guiWindow.checkbox("Side-by-Side Stereo", sideBySide);

        if(sideBySide)
        {
            Gui::DropdownList outputImageList;
            outputImageList.push_back({ 1, "Both" });
            outputImageList.push_back({ 2, "Left" });
            outputImageList.push_back({ 3, "Right" });
            guiWindow.dropdown("Output Image", outputImageList, (uint32_t&)outputImage);

            guiWindow.checkbox("Crop Output", cropOutput);
            //guiWindow.slider("IPD", camController.ipd, 0.05f, 0.08f); @fix
            //guiWindow.slider("Z0", camController.z0, 0.0f, 20.0f); @fix
        }

        break;

    case RenderMode::RenderToHMD:
        break;

    default:
        break;
    }

    //@fix only include if FXAA needed
    /*if(guiWindow.checkbox("Use FXAA", useFXAA))
    {
        if(useFXAA)
        {
            renderGraph->markOutput("FXAA_Left.dst");
            renderGraph->markOutput("FXAA_Right.dst");
        }
        else
        {
            renderGraph->unmarkOutput("FXAA_Left.dst");
            renderGraph->unmarkOutput("FXAA_Right.dst");
        }

        leftOutput = useFXAA ? "FXAA_Left.dst" : "Light.out";
        rightOutput = useFXAA ? "FXAA_Right.dst" : "Reprojection.out";

        onClickResize();
    }*/

    if(guiWindow.button("4K Resolution"))
    {
        Fbo::Desc fboDesc;
        fboDesc.setColorTarget(0, gpFramework->getTargetFbo()->getColorTexture(0)->getFormat()).setDepthStencilTarget(gpFramework->getTargetFbo()->getDepthStencilTexture()->getFormat());

        hmdFbo = Fbo::create2D(3840, 2160, fboDesc);
        renderGraph->onResize(hmdFbo.get());
    }

    guiWindow.separator();

    //@fix might make problems
    if(renderGraph != nullptr)
    {
        renderGraph->getScene()->renderUI(guiWindow);
    }
}

void SpatioTemporalReprojection::onClickResize()
{
    renderGraph->onResize(gpFramework->getTargetFbo().get());
}

void SpatioTemporalReprojection::loadScene(const std::string& filename)
{
    //TODO
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    SpatioTemporalReprojection::UniquePtr pRenderer = std::make_unique<SpatioTemporalReprojection>();
    SampleConfig config;
    config.windowDesc.title = "Falcor Project Template";
    config.windowDesc.resizableWindow = true;
    Sample::run(config, pRenderer);
    return 0;
}
