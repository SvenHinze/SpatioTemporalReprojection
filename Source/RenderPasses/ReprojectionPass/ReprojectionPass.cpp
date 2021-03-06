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
#include "ReprojectionPass.h"

struct layoutsData
{
    uint32_t pos;
    std::string name;
    ResourceFormat format;
};

static const layoutsData layoutData[VERTEX_LOCATION_COUNT] =
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

size_t ReprojectionPass::lightArrayOffset = ConstantBuffer::invalidOffset;

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib)
{
    lib.registerClass("ReprojectionPass", "Render Pass Template", ReprojectionPass::create);
}

ReprojectionPass::SharedPtr ReprojectionPass::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new ReprojectionPass);
    return pPass;
}

Dictionary ReprojectionPass::getScriptingDictionary()
{
    return Dictionary();
}

RenderPassReflection ReprojectionPass::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    //reflector.addOutput("dst");
    //reflector.addInput("src");
    return reflector;
}

void ReprojectionPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // renderData holds the requested resources
    // auto& pTexture = renderData["src"]->asTexture();
}

void ReprojectionPass::renderUI(Gui::Widgets& widget)
{
}
