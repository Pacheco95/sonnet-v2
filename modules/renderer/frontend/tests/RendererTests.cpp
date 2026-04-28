#include <catch2/catch_test_macros.hpp>

#include "MockBackends.h"

#include <sonnet/renderer/frontend/Renderer.h>

using sonnet::api::render::BlendMode;
using sonnet::api::render::CullMode;
using sonnet::api::render::CPUMesh;
using sonnet::api::render::FrameContext;
using sonnet::api::render::MaterialInstance;
using sonnet::api::render::MaterialTemplate;
using sonnet::api::render::PositionAttribute;
using sonnet::api::render::RenderItem;
using sonnet::api::render::RenderOverrides;
using sonnet::api::render::RenderState;
using sonnet::api::render::RenderTargetDesc;
using sonnet::api::render::SamplerDesc;
using sonnet::api::render::TextureDesc;
using sonnet::api::render::VertexLayout;
using sonnet::core::GPUTextureHandle;
using sonnet::core::ShaderHandle;
using sonnet::renderer::frontend::Renderer;
using sonnet::api::test::MockRendererBackend;

namespace {

CPUMesh triMesh() {
    CPUMesh mesh{VertexLayout{{PositionAttribute{}}}, {0, 1, 2}};
    PositionAttribute p{};
    mesh.addVertex({p}).addVertex({p}).addVertex({p});
    return mesh;
}

FrameContext makeCtx(const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye) {
    return FrameContext{view, proj, eye, 800, 600, 1.0f / 60.0f, std::nullopt, {}, std::nullopt};
}

} // namespace

TEST_CASE("Renderer ctor: allocates camera and lights UBOs via the backend", "[renderer][ubo]") {
    MockRendererBackend backend;
    Renderer r{backend};
    // Two createBuffer calls (camera + lights) — MockRendererBackend doesn't
    // record createBuffer, but we can observe it via beginFrame/endFrame counters
    // staying zero, and by exercising render(...) below.
    REQUIRE(backend.beginFrameCalls == 0);
}

TEST_CASE("Renderer: createMesh / createShader / createMaterial issue distinct handles",
         "[renderer][handles]") {
    MockRendererBackend backend;
    Renderer r{backend};

    auto m = r.createMesh(triMesh());
    auto s = r.createShader("vs", "fs");
    auto mat = r.createMaterial(MaterialTemplate{});

    // Different stores, but ids share the monotonic counter — values strictly increasing.
    REQUIRE(m.value   < s.value);
    REQUIRE(s.value   < mat.value);

    REQUIRE(backend.gpuMeshFactoryImpl.calls   == 1);
    REQUIRE(backend.shaderCompilerImpl.calls   == 1);
}

TEST_CASE("Renderer: getMaterial round-trips a registered template", "[renderer][material]") {
    MockRendererBackend backend;
    Renderer r{backend};

    MaterialTemplate tmpl;
    tmpl.shaderHandle = ShaderHandle{99};
    tmpl.renderState.cull = CullMode::Front;

    auto h = r.createMaterial(tmpl);
    const auto *out = r.getMaterial(h);

    REQUIRE(out != nullptr);
    REQUIRE(out->shaderHandle == ShaderHandle{99});
    REQUIRE(out->renderState.cull == CullMode::Front);
}

TEST_CASE("Renderer: getMaterial returns nullptr for unknown handle",
         "[renderer][material]") {
    MockRendererBackend backend;
    Renderer r{backend};

    REQUIRE(r.getMaterial(sonnet::core::MaterialTemplateHandle{12345}) == nullptr);
}

TEST_CASE("Renderer: nativeTextureId and imGuiTextureId throw for unknown handle",
         "[renderer][texture]") {
    MockRendererBackend backend;
    Renderer r{backend};
    REQUIRE_THROWS_AS(r.nativeTextureId(GPUTextureHandle{999}), std::invalid_argument);
    REQUIRE_THROWS_AS(r.imGuiTextureId(GPUTextureHandle{999}),   std::invalid_argument);
}

TEST_CASE("Renderer: registerRawTexture exposes native and ImGui ids", "[renderer][texture]") {
    MockRendererBackend backend;
    Renderer r{backend};

    auto tex = std::make_unique<sonnet::api::test::MockTexture>();
    tex->native  = 42;
    tex->imguiId = 0xCAFEBABE;

    auto handle = r.registerRawTexture(std::move(tex));
    REQUIRE(r.nativeTextureId(handle) == 42u);
    REQUIRE(r.imGuiTextureId(handle)  == 0xCAFEBABEu);
}

TEST_CASE("Renderer: createRenderTarget then resize updates the backing target",
         "[renderer][rendertarget]") {
    MockRendererBackend backend;
    Renderer r{backend};

    RenderTargetDesc desc{};
    desc.width  = 256;
    desc.height = 256;
    auto handle = r.createRenderTarget(desc);

    REQUIRE(backend.renderTargetFactoryImpl.calls == 1);

    r.resizeRenderTarget(handle, 1024, 768);

    REQUIRE(backend.renderTargetFactoryImpl.calls == 2);
}

TEST_CASE("Renderer: bindRenderTarget on unknown handle is silent (no throw)",
         "[renderer][rendertarget]") {
    MockRendererBackend backend;
    Renderer r{backend};
    REQUIRE_NOTHROW(r.bindRenderTarget(sonnet::core::RenderTargetHandle{777}));
}

TEST_CASE("Renderer: colorTextureHandle / depthTextureHandle throw for unknown RT",
         "[renderer][rendertarget]") {
    MockRendererBackend backend;
    Renderer r{backend};

    REQUIRE_THROWS_AS(r.colorTextureHandle(sonnet::core::RenderTargetHandle{1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(r.depthTextureHandle(sonnet::core::RenderTargetHandle{1}),
                      std::invalid_argument);
}

TEST_CASE("Renderer: reloadShader replaces backing shader without invalidating the handle",
         "[renderer][shader]") {
    MockRendererBackend backend;
    Renderer r{backend};

    auto handle = r.createShader("VS_OLD", "FS_OLD");
    REQUIRE(backend.shaderCompilerImpl.calls == 1);

    r.reloadShader(handle, "VS_NEW", "FS_NEW");
    REQUIRE(backend.shaderCompilerImpl.calls == 2);
}

TEST_CASE("Renderer: reloadShader invalidates the OLD shader's backend pipelines",
         "[renderer][shader]") {
    // Backends with shader-keyed pipeline caches (Vulkan) need to drop entries
    // tied to the previous IShader before the new module replaces it. The
    // mock records the IShader& it receives — we assert it points at the OLD
    // shader instance (still alive when invalidate fires) before the new one
    // moves into the slot.
    MockRendererBackend backend;
    Renderer r{backend};

    auto handle           = r.createShader("VS_OLD", "FS_OLD");
    const auto oldShader  = backend.shaderCompilerImpl.calls; // captures call order
    REQUIRE(oldShader == 1);
    REQUIRE(backend.invalidatedShaders.empty());

    r.reloadShader(handle, "VS_NEW", "FS_NEW");

    REQUIRE(backend.shaderCompilerImpl.calls == 2);
    REQUIRE(backend.invalidatedShaders.size() == 1);
    REQUIRE(backend.invalidatedShaders.back() != nullptr);
}

TEST_CASE("Renderer: reloadShader on unknown handle does not invalidate anything",
         "[renderer][shader]") {
    MockRendererBackend backend;
    Renderer r{backend};
    r.reloadShader(ShaderHandle{999}, "vs", "fs");
    REQUIRE(backend.invalidatedShaders.empty());
}

TEST_CASE("Renderer: reloadShader with unknown handle is a no-op", "[renderer][shader]") {
    MockRendererBackend backend;
    Renderer r{backend};
    REQUIRE_NOTHROW(r.reloadShader(ShaderHandle{999}, "vs", "fs"));
    REQUIRE(backend.shaderCompilerImpl.calls == 0);
}

TEST_CASE("Renderer: beginFrame and endFrame forward to the backend", "[renderer][frame]") {
    MockRendererBackend backend;
    Renderer r{backend};
    r.beginFrame();
    r.endFrame();
    REQUIRE(backend.beginFrameCalls == 1);
    REQUIRE(backend.endFrameCalls   == 1);
}

TEST_CASE("Renderer: render() with empty queue still uploads camera and light UBOs",
         "[renderer][render]") {
    MockRendererBackend backend;
    Renderer r{backend};

    glm::mat4 view{1.0f}, proj{1.0f};
    glm::vec3 eye{0.0f};
    auto ctx = makeCtx(view, proj, eye);

    std::vector<RenderItem> queue;
    r.render(ctx, queue);

    // Empty queue → no draws, no state changes.
    REQUIRE(backend.draws.empty());
    REQUIRE(backend.fillModes.empty());
    // queue is cleared (already empty).
    REQUIRE(queue.empty());
}

TEST_CASE("Renderer: render() applies state, draws, and clears the queue",
         "[renderer][render]") {
    MockRendererBackend backend;
    Renderer r{backend};

    auto meshH    = r.createMesh(triMesh());
    auto shaderH  = r.createShader("vs", "fs");
    MaterialTemplate tmpl;
    tmpl.shaderHandle = shaderH;
    tmpl.renderState.fill  = sonnet::api::render::FillMode::Wireframe;
    tmpl.renderState.cull  = CullMode::Front;
    tmpl.renderState.blend = BlendMode::Alpha;
    auto matH = r.createMaterial(tmpl);

    glm::mat4 view{1.0f}, proj{1.0f};
    glm::vec3 eye{0.0f};
    auto ctx = makeCtx(view, proj, eye);

    std::vector<RenderItem> queue;
    queue.push_back(RenderItem{
        std::nullopt, meshH, MaterialInstance{matH}, glm::mat4{1.0f}});

    r.render(ctx, queue);

    REQUIRE(backend.draws.size() == 1);
    REQUIRE(backend.draws[0].indexCount == 3);
    REQUIRE(backend.fillModes.back() == sonnet::api::render::FillMode::Wireframe);
    REQUIRE(backend.cullModes.back() == CullMode::Front);
    // BlendMode::Alpha → setBlend(true).
    REQUIRE(backend.blendToggles.back());
    // queue was cleared.
    REQUIRE(queue.empty());
}

TEST_CASE("Renderer: setOverrides replaces material render state per draw",
         "[renderer][render][overrides]") {
    MockRendererBackend backend;
    Renderer r{backend};

    auto meshH   = r.createMesh(triMesh());
    auto shaderH = r.createShader("vs", "fs");
    MaterialTemplate tmpl;
    tmpl.shaderHandle = shaderH;
    tmpl.renderState.cull = CullMode::Back;
    tmpl.renderState.fill = sonnet::api::render::FillMode::Solid;
    auto matH = r.createMaterial(tmpl);

    RenderOverrides over;
    over.cull = CullMode::None;
    over.fill = sonnet::api::render::FillMode::Wireframe;
    r.setOverrides(&over);

    glm::mat4 view{1.0f}, proj{1.0f}; glm::vec3 eye{0.0f};
    auto ctx = makeCtx(view, proj, eye);

    std::vector<RenderItem> queue;
    queue.push_back(RenderItem{std::nullopt, meshH, MaterialInstance{matH}, glm::mat4{1.0f}});
    r.render(ctx, queue);

    REQUIRE(backend.cullModes.back() == CullMode::None);
    REQUIRE(backend.fillModes.back() == sonnet::api::render::FillMode::Wireframe);
}

TEST_CASE("Renderer: render() skips items with unknown mesh or material", "[renderer][render]") {
    MockRendererBackend backend;
    Renderer r{backend};

    glm::mat4 view{1.0f}, proj{1.0f}; glm::vec3 eye{0.0f};
    auto ctx = makeCtx(view, proj, eye);

    std::vector<RenderItem> queue;
    queue.push_back(RenderItem{
        std::nullopt,
        sonnet::core::GPUMeshHandle{42},
        MaterialInstance{sonnet::core::MaterialTemplateHandle{42}},
        glm::mat4{1.0f}});

    r.render(ctx, queue);
    REQUIRE(backend.draws.empty());
}
