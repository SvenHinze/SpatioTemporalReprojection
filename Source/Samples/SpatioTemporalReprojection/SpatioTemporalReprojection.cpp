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

uint32_t SpatioTemporalReprojection::stereoTarget = 0;

void SpatioTemporalReprojection::onLoad(RenderContext* pRenderContext)
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

    //renderGraph->onResize(sample->getCurrentFbo().get()); @fix find out what samplecallbacks are and how to substitute

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

void SpatioTemporalReprojection::onFrameRender(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    const float4 clearColor(0.38f, 0.52f, 0.10f, 1);
    pRenderContext->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
}

void SpatioTemporalReprojection::onGuiRender(Gui* pGui)
{
    Gui::Window w(pGui, "Falcor", { 250, 200 });
    gpFramework->renderGlobalUI(pGui);
    w.text("Hello from SpatioTemporalReprojection");
    if (w.button("Click Here"))
    {
        msgBox("Now why would you do that?");
    }
}

void SpatioTemporalReprojection::onShutdown()
{
}

bool SpatioTemporalReprojection::onKeyEvent(const KeyboardEvent& keyEvent)
{
    return false;
}

bool SpatioTemporalReprojection::onMouseEvent(const MouseEvent& mouseEvent)
{
    return false;
}

void SpatioTemporalReprojection::onHotReload(HotReloadFlags reloaded)
{
}

void SpatioTemporalReprojection::onResizeSwapChain(uint32_t width, uint32_t height)
{
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
