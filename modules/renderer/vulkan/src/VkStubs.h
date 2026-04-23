#pragma once

// Phase 1 stubs for the IRendererBackend factory interfaces. These compile
// so the Vulkan backend can return factory references from its virtual
// accessors; every entry point throws until the corresponding phase fills
// them in. Phase 2 replaces these with real implementations.

#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/api/render/IShader.h>
#include <sonnet/api/render/GpuMesh.h>

#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

class ShaderCompiler final : public api::render::IShaderCompiler {
public:
    std::unique_ptr<api::render::IShader> operator()(
        const std::string &, const std::string &) const override {
        SN_VK_TODO("ShaderCompiler::operator() — Phase 2");
    }
};

class TextureFactory final : public api::render::ITextureFactory {
public:
    std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &, const api::render::SamplerDesc &,
        const api::render::CPUTextureBuffer &) const override {
        SN_VK_TODO("TextureFactory::create(CPUTextureBuffer) — Phase 2");
    }
    std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &, const api::render::SamplerDesc &,
        const api::render::CubeMapFaces &) const override {
        SN_VK_TODO("TextureFactory::create(CubeMapFaces) — Phase 2");
    }
    std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &, const api::render::SamplerDesc &) const override {
        SN_VK_TODO("TextureFactory::create(desc-only) — Phase 2");
    }
};

class RenderTargetFactory final : public api::render::IRenderTargetFactory {
public:
    std::unique_ptr<api::render::IRenderTarget> create(
        const api::render::RenderTargetDesc &) const override {
        SN_VK_TODO("RenderTargetFactory::create — Phase 2");
    }
};

class GpuMeshFactory final : public api::render::IGpuMeshFactory {
public:
    std::unique_ptr<api::render::GpuMesh> operator()(const api::render::CPUMesh &) const override {
        SN_VK_TODO("GpuMeshFactory::operator() — Phase 2");
    }
};

} // namespace sonnet::renderer::vulkan
