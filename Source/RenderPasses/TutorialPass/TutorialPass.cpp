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
#include "TutorialPass.h"

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib)
{
    lib.registerClass("TutorialPass", "Renders a scene as a wireframe", TutorialPass::create);
}

TutorialPass::TutorialPass()
{
    pProgram = GraphicsProgram::createFromFile("RenderPasses/TutorialPass/TutorialShader.slang", "", "main");

    RasterizerState::Desc desc;
    desc.setFillMode(RasterizerState::FillMode::Wireframe);
    desc.setCullMode(RasterizerState::CullMode::None);
    pRasterState = RasterizerState::create(desc);

    pGraphicsState = GraphicsState::create();
    pGraphicsState->setProgram(pProgram);
    pGraphicsState->setRasterizerState(pRasterState);
}

TutorialPass::SharedPtr TutorialPass::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new TutorialPass);
    return pPass;
}

Dictionary TutorialPass::getScriptingDictionary()
{
    return Dictionary();
}

RenderPassReflection TutorialPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    reflector.addOutput("output", "this is the output");

    return reflector;
}

void TutorialPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto pTargetFbo = Fbo::create({ renderData["output"]->asTexture() });
    pRenderContext->clearFbo(pTargetFbo.get(), float4(0, 0, 0, 1), 1.0f, 0, FboAttachmentType::All);
    pGraphicsState->setFbo(pTargetFbo);

    Scene::RenderFlags renderFlags = Scene::RenderFlags::UserRasterizerState;
    pGraphicsVars["PerFrameCB"]["gColor"] = float4(0, 1, 0, 1);

    pScene->render(pRenderContext, pGraphicsState.get(), pGraphicsVars.get(), renderFlags);
}

void TutorialPass::renderUI(Gui::Widgets& widget)
{
}

void TutorialPass::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    this->pScene = pScene;
    pProgram->addDefines(pScene->getSceneDefines());
    pGraphicsVars = GraphicsVars::create(pProgram->getReflector());
}
