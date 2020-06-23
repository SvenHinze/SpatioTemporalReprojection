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

#include "Lighting.h"
#include "../DeferredRenderer.h"

namespace {
    const char* kFileOutput = "Lighting.slang";
};

size_t Lighting::sLightArrayOffset = ConstantBuffer::kInvalidOffset;
size_t Lighting::sLightCountOffset = ConstantBuffer::kInvalidOffset;
size_t Lighting::sCameraDataOffset = ConstantBuffer::kInvalidOffset;

Lighting::SharedPtr Lighting::create(const Dictionary & params)
{
    Lighting::SharedPtr ptr(new Lighting());
    return ptr;
}

RenderPassReflection Lighting::reflect() const
{
    RenderPassReflection r;
    r.addInput("posW", "");
    r.addInput("normW", "");
    r.addInput("diffuseOpacity", "");
    r.addInput("specRough", "");
    r.addInput("shadowDepth", "");

    r.addOutput("out", "").format(ResourceFormat::RGBA32Float).bindFlags(Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::RenderTarget);
    return r;
}

void Lighting::initialize(const Dictionary & dict)
{
    mpState = GraphicsState::create();
    mpPass = FullScreenPass::create(kFileOutput);
    mpVars = GraphicsVars::create(mpPass->getProgram()->getReflector());
    mpFbo = Fbo::create();

    Sampler::Desc samplerDesc;
    samplerDesc.setLodParams(0.f, 0.f, 0.f);
    samplerDesc.setComparisonMode(Sampler::ComparisonMode::LessEqual);
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mpLinearComparisonSampler = Sampler::create(samplerDesc);

    mIsInitialized = true;
}

void Lighting::updateVariableOffsets(const ProgramReflection * pReflector)
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

void Lighting::setPerFrameData(const GraphicsVars * currentData)
{
    ConstantBuffer* pCB = currentData->getConstantBuffer("InternalPerFrameCB").get();

    // Set camera
    if (mpScene->getActiveCamera())
    {
        mpScene->getActiveCamera()->setIntoConstantBuffer(pCB, sCameraDataOffset);
    }

    // Set lights data
    if (sLightArrayOffset != ConstantBuffer::kInvalidOffset)
    {
        for (uint32_t i = 0; i < mpScene->getLightCount(); i++)
        {
            mpScene->getLight(i)->setIntoProgramVars(&*mpVars, pCB, sLightArrayOffset + (i * Light::getShaderStructSize()));
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
        // #TODO Support multiple light probes
        LightProbe::setCommonIntoProgramVars(&*mpVars, "gProbeShared");
        mpScene->getLightProbe(0)->setIntoProgramVars(&*mpVars, pCB, "gLightProbe");
    }
}

void Lighting::execute(RenderContext * pContext, const RenderData * pRenderData)
{
    // On first execution, run some initialization
    if (!mIsInitialized) initialize(pRenderData->getDictionary());

    // Get our output buffer and clear it
    const auto& pDisTex = pRenderData->getTexture("out");
    pContext->clearUAV(pDisTex->getUAV().get(), vec4(0));
    mpFbo->attachColorTarget(pDisTex, 0);

    if (pDisTex == nullptr) return;

    // set scene lights (not auto by FullScreenPass program -> adapted from SceneRenderer.cpp)
    updateVariableOffsets(pContext->getGraphicsVars()->getReflection().get());
    setPerFrameData(mpVars.get());

    mpVars["PerImageCB"]["gLightViewProj"] = mpLightCamera->getViewProjMatrix();
    mpVars["PerImageCB"]["gStereoTarget"] = DeferredRenderer::gStereoTarget;
    mpVars["PerImageCB"]["gBias"] = mBias;
    mpVars["PerImageCB"]["gKernelSize"] = (uint32_t)mPCFKernelSize;
    mpVars->setSampler("gPCFCompSampler", mpLinearComparisonSampler);

    mpVars->setTexture("gPos", pRenderData->getTexture("posW"));
    mpVars->setTexture("gNorm", pRenderData->getTexture("normW"));
    mpVars->setTexture("gDiffuseMatl", pRenderData->getTexture("diffuseOpacity"));
    mpVars->setTexture("gSpecMatl", pRenderData->getTexture("specRough"));
    mpVars->setTexture("gShadowMap", pRenderData->getTexture("shadowDepth"));

    mpState->setFbo(mpFbo);
    pContext->pushGraphicsState(mpState);
    pContext->pushGraphicsVars(mpVars);

    if (mpSkyBox != nullptr && mbRenderSkybox)
    {
        mpSkyBox->render(pContext, mpScene->getActiveCamera().get());
    }

    mpPass->execute(pContext);
    pContext->popGraphicsVars();
    pContext->popGraphicsState();
}

void Lighting::renderUI(Gui * pGui, const char * uiGroup)
{
    pGui->addFloatSlider("Shadow Bias", mBias, 0.001f, 0.1f);
    pGui->addIntVar("PCF Kernel Size", mPCFKernelSize, 1, 32);
    pGui->addSeparator();
    if (pGui->addCheckBox("Show BRDF", mbShowBRDF))
    {
        setDefine("_SHOWBRDF", mbShowBRDF);
    }
    pGui->addCheckBox("Render Skybox", mbRenderSkybox);
    if (pGui->beginGroup("Light Probes"))
    {
        if (mpScene->getLightProbeCount() > 0)
        {
            pGui->addSeparator();
            mpScene->getLightProbe(0)->renderUI(pGui);
        }
    }
}

void Lighting::setDefine(std::string pName, bool flag)
{
    if (flag)
    {
        mpPass->getProgram()->addDefine(pName);
    }
    else
    {
        mpPass->getProgram()->removeDefine(pName);
    }
}

Dictionary Lighting::getScriptingDictionary() const
{
	return Dictionary();
}

void Lighting::onResize(uint32_t width, uint32_t height)
{
}

void Lighting::setScene(const std::shared_ptr<Scene>& pScene)
{
    mpScene = pScene;

    if (mpScene != nullptr && mpScene->getEnvironmentMap() != nullptr)
    {
        mpSkyBox = SkyBox::create(mpScene->getEnvironmentMap());
    }
    else
    {
        mpSkyBox = nullptr;
    }
}
