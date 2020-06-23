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

using namespace Falcor;

// Main lighting pass after the G-buffer
class Lighting : public RenderPass, inherit_shared_from_this<RenderPass, Lighting>
{
public:

    using SharedPtr = std::shared_ptr<Lighting>;
    std::string getDesc(void) override { return "Deferred Light Pass"; }

    static SharedPtr create(const Dictionary& params = {});

    RenderPassReflection reflect() const override;
    void execute(RenderContext* pContext, const RenderData* pRenderData) override;
    void renderUI(Gui* pGui, const char* uiGroup) override;
    Dictionary getScriptingDictionary() const override;
    void onResize(uint32_t width, uint32_t height) override;
    void setScene(const std::shared_ptr<Scene>& pScene) override;

    static size_t sLightArrayOffset;
    static size_t sLightCountOffset;
    static size_t sCameraDataOffset;

    Camera::SharedPtr mpLightCamera;
    Sampler::SharedPtr          mpLinearComparisonSampler;
    float                       mBias = 0.01f;
    int32_t                     mPCFKernelSize = 4;

private:
    Lighting() : RenderPass("Lighting") {}

    void initialize(const Dictionary& dict);
    void updateVariableOffsets(const ProgramReflection* pReflector);
    void setPerFrameData(const GraphicsVars* pVars);
    void setDefine(std::string pName, bool flag);

    Scene::SharedPtr          mpScene;
    SkyBox::SharedPtr         mpSkyBox;

    Fbo::SharedPtr              mpFbo;
    GraphicsVars::SharedPtr     mpVars;
    GraphicsState::SharedPtr    mpState;
    FullScreenPass::UniquePtr   mpPass;

    bool mIsInitialized = false;
    bool mbShowBRDF = false;
    bool mbRenderSkybox = false;
};

