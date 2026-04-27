#pragma once

// Minimal recording mocks for the api/ interfaces. Test-only; not exported.
//
// Each mock satisfies one of the api/ interfaces with the smallest viable
// behaviour, and records its method invocations into a public vector so test
// code can assert against the call sequence. These are designed to unblock
// downstream tests (renderer/, scene/, demo) without standing up a real GL/VK
// context.

#include <sonnet/api/input/IInput.h>
#include <sonnet/api/input/IInputSink.h>
#include <sonnet/api/render/IGpuBuffer.h>
#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/api/render/IRenderer.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/api/render/IShader.h>
#include <sonnet/api/render/ITexture.h>
#include <sonnet/api/render/IVertexInputState.h>
#include <sonnet/api/window/IWindow.h>
#include <sonnet/core/RendererTraits.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace sonnet::api::test {

// ── Resource mocks ────────────────────────────────────────────────────────────

class MockGpuBuffer final : public render::IGpuBuffer {
public:
    mutable std::size_t bindCalls{0};
    mutable std::size_t bindBaseCalls{0};
    std::size_t         updateCalls{0};
    std::vector<std::byte> lastUpload{};
    mutable std::uint32_t lastBindingPoint{0};

    void bind() const override { ++bindCalls; }
    void update(const void *data, std::size_t size) override {
        ++updateCalls;
        const auto *p = reinterpret_cast<const std::byte *>(data);
        lastUpload.assign(p, p + size);
    }
    void bindBase(std::uint32_t bindingPoint) const override {
        ++bindBaseCalls;
        lastBindingPoint = bindingPoint;
    }
};

class MockVertexInputState final : public render::IVertexInputState {
public:
    mutable std::size_t bindCalls{0};
    mutable std::size_t unbindCalls{0};
    void bind()   const override { ++bindCalls; }
    void unbind() const override { ++unbindCalls; }
};

class MockShader final : public render::IShader {
public:
    std::string vertex{};
    std::string fragment{};
    core::ShaderProgram program{0};
    core::UniformDescriptorMap uniforms{};
    mutable std::size_t bindCalls{0};
    mutable std::size_t unbindCalls{0};

    [[nodiscard]] const std::string              &getVertexSource()   const override { return vertex; }
    [[nodiscard]] const std::string              &getFragmentSource() const override { return fragment; }
    [[nodiscard]] const core::ShaderProgram      &getProgram()        const override { return program; }
    [[nodiscard]] const core::UniformDescriptorMap &getUniforms()     const override { return uniforms; }
    void bind()   const override { ++bindCalls; }
    void unbind() const override { ++unbindCalls; }
};

class MockTexture final : public render::ITexture {
public:
    render::TextureDesc tex{};
    render::SamplerDesc sampler{};
    unsigned native{0};
    std::uintptr_t imguiId{0};
    mutable std::vector<std::uint8_t> bindSlots{};
    mutable std::vector<std::uint8_t> unbindSlots{};

    void bind(std::uint8_t slot)   const override { bindSlots.push_back(slot); }
    void unbind(std::uint8_t slot) const override { unbindSlots.push_back(slot); }
    [[nodiscard]] const render::TextureDesc  &textureDesc() const override { return tex; }
    [[nodiscard]] const render::SamplerDesc  &samplerDesc() const override { return sampler; }
    [[nodiscard]] unsigned                    getNativeHandle() const override { return native; }
    [[nodiscard]] std::uintptr_t              getImGuiTextureId() override { return imguiId; }
};

class MockRenderTarget final : public render::IRenderTarget {
public:
    std::uint32_t w{0};
    std::uint32_t h{0};
    mutable std::size_t bindCalls{0};
    std::array<std::uint8_t, 4> stubPixel{0, 0, 0, 0};

    [[nodiscard]] std::uint32_t width()  const override { return w; }
    [[nodiscard]] std::uint32_t height() const override { return h; }
    void                         bind()   const override { ++bindCalls; }
    [[nodiscard]] const render::ITexture *colorTexture(std::size_t) const override { return nullptr; }
    [[nodiscard]] const render::ITexture *depthTexture()            const override { return nullptr; }
    [[nodiscard]] std::array<std::uint8_t, 4> readPixelRGBA8(
        std::uint32_t, std::uint32_t, std::uint32_t) const override { return stubPixel; }
};

// ── Factory mocks ─────────────────────────────────────────────────────────────

class MockShaderCompiler final : public render::IShaderCompiler {
public:
    mutable std::size_t calls{0};
    [[nodiscard]] std::unique_ptr<render::IShader> operator()(
        const std::string &vs, const std::string &fs) const override {
        ++calls;
        auto s = std::make_unique<MockShader>();
        s->vertex   = vs;
        s->fragment = fs;
        return s;
    }
};

class MockTextureFactory final : public render::ITextureFactory {
public:
    mutable std::size_t fromBufferCalls{0};
    mutable std::size_t fromCubeCalls{0};
    mutable std::size_t emptyCalls{0};
    [[nodiscard]] std::unique_ptr<render::ITexture> create(
        const render::TextureDesc &desc, const render::SamplerDesc &sampler,
        const render::CPUTextureBuffer &) const override {
        ++fromBufferCalls;
        auto t = std::make_unique<MockTexture>();
        t->tex     = desc;
        t->sampler = sampler;
        return t;
    }
    [[nodiscard]] std::unique_ptr<render::ITexture> create(
        const render::TextureDesc &desc, const render::SamplerDesc &sampler,
        const render::CubeMapFaces &) const override {
        ++fromCubeCalls;
        auto t = std::make_unique<MockTexture>();
        t->tex     = desc;
        t->sampler = sampler;
        return t;
    }
    [[nodiscard]] std::unique_ptr<render::ITexture> create(
        const render::TextureDesc &desc, const render::SamplerDesc &sampler) const override {
        ++emptyCalls;
        auto t = std::make_unique<MockTexture>();
        t->tex     = desc;
        t->sampler = sampler;
        return t;
    }
};

class MockRenderTargetFactory final : public render::IRenderTargetFactory {
public:
    mutable std::size_t calls{0};
    [[nodiscard]] std::unique_ptr<render::IRenderTarget> create(
        const render::RenderTargetDesc &desc) const override {
        ++calls;
        auto r = std::make_unique<MockRenderTarget>();
        r->w = desc.width;
        r->h = desc.height;
        return r;
    }
};

class MockGpuMeshFactory final : public render::IGpuMeshFactory {
public:
    mutable std::size_t calls{0};
    [[nodiscard]] std::unique_ptr<render::GpuMesh> operator()(const render::CPUMesh &mesh) const override {
        ++calls;
        return std::make_unique<render::GpuMesh>(
            mesh.layout(),
            std::make_unique<MockGpuBuffer>(),
            std::make_unique<MockGpuBuffer>(),
            std::make_unique<MockVertexInputState>(),
            mesh.indices().size());
    }
};

// ── Backend mock ──────────────────────────────────────────────────────────────

class MockRendererBackend final : public render::IRendererBackend {
public:
    struct DrawCall {
        std::size_t indexCount;
    };
    struct UniformSet {
        UniformLocation       location;
        core::UniformValue    value;
    };

    // Counters / recorders.
    std::size_t initializeCalls{0};
    std::size_t beginFrameCalls{0};
    std::size_t endFrameCalls{0};
    std::vector<render::ClearOptions>      clears{};
    std::vector<glm::uvec2>                viewports{};
    std::vector<DrawCall>                  draws{};
    std::vector<UniformSet>                uniformSets{};
    std::vector<render::FillMode>          fillModes{};
    std::vector<bool>                      depthTests{};
    std::vector<bool>                      depthWrites{};
    std::vector<render::DepthFunction>     depthFuncs{};
    std::vector<render::CullMode>          cullModes{};
    std::vector<bool>                      blendToggles{};

    // Owned factories (constructed lazily on first reference).
    MockShaderCompiler       shaderCompilerImpl{};
    MockTextureFactory       textureFactoryImpl{};
    MockRenderTargetFactory  renderTargetFactoryImpl{};
    MockGpuMeshFactory       gpuMeshFactoryImpl{};
    core::RendererTraits     traitsImpl{core::presets::OpenGL};

    void initialize() override                { ++initializeCalls; }
    void beginFrame() override                { ++beginFrameCalls; }
    void endFrame() override                  { ++endFrameCalls; }
    void clear(const render::ClearOptions &o) override { clears.push_back(o); }
    void bindDefaultRenderTarget() override   {}
    void bindRenderTarget(const render::IRenderTarget &) override {}
    void setViewport(std::uint32_t w, std::uint32_t h) override { viewports.emplace_back(w, h); }

    void setFillMode(render::FillMode m)      override { fillModes.push_back(m); }
    void setDepthTest(bool e)                 override { depthTests.push_back(e); }
    void setDepthWrite(bool e)                override { depthWrites.push_back(e); }
    void setDepthFunc(render::DepthFunction f) override { depthFuncs.push_back(f); }
    void setCull(render::CullMode m)          override { cullModes.push_back(m); }
    void setBlend(bool e)                     override { blendToggles.push_back(e); }
    void setBlendFunc(render::BlendFactor, render::BlendFactor) override {}
    void setSRGB(bool) override               {}

    [[nodiscard]] std::unique_ptr<render::IGpuBuffer> createBuffer(
        render::BufferType, const void *, std::size_t) override {
        return std::make_unique<MockGpuBuffer>();
    }
    [[nodiscard]] std::unique_ptr<render::IVertexInputState> createVertexInputState(
        const render::VertexLayout &, const render::IGpuBuffer &, const render::IGpuBuffer &) override {
        return std::make_unique<MockVertexInputState>();
    }

    void setUniform(UniformLocation loc, const core::UniformValue &v) override {
        uniformSets.push_back({loc, v});
    }
    void drawIndexed(std::size_t count) override { draws.push_back({count}); }

    [[nodiscard]] render::IShaderCompiler       &shaderCompiler()      override { return shaderCompilerImpl; }
    [[nodiscard]] render::ITextureFactory       &textureFactory()      override { return textureFactoryImpl; }
    [[nodiscard]] render::IRenderTargetFactory  &renderTargetFactory() override { return renderTargetFactoryImpl; }
    [[nodiscard]] render::IGpuMeshFactory       &gpuMeshFactory()      override { return gpuMeshFactoryImpl; }
    [[nodiscard]] const core::RendererTraits    &traits() const        override { return traitsImpl; }
};

// ── Frontend / window / input mocks ──────────────────────────────────────────

class MockRenderer final : public render::IRenderer {
public:
    std::size_t beginFrameCalls{0};
    std::size_t endFrameCalls{0};
    std::size_t renderCalls{0};
    std::size_t lastQueueSize{0};
    render::RenderOverrides *lastOverrides{nullptr};

    void beginFrame() override { ++beginFrameCalls; }
    void render(const render::FrameContext &, std::vector<render::RenderItem> &queue) override {
        ++renderCalls;
        lastQueueSize = queue.size();
    }
    void endFrame() override { ++endFrameCalls; }
    void setOverrides(render::RenderOverrides *o) override { lastOverrides = o; }
};

class MockWindow final : public window::IWindow {
public:
    std::string title{};
    glm::uvec2  framebuffer{0, 0};
    bool        visible{true};
    bool        closeRequested{false};
    window::WindowState state{window::WindowState::Normal};
    std::size_t pollCalls{0};
    std::size_t swapCalls{0};
    std::size_t toggleFullscreenCalls{0};
    std::size_t captureCursorCalls{0};
    std::size_t releaseCursorCalls{0};

    void setTitle(const std::string &t) override { title = t; }
    [[nodiscard]] const std::string &getTitle() const override { return title; }
    [[nodiscard]] glm::uvec2 getFrameBufferSize() const override { return framebuffer; }
    void setVisible(bool v) override { visible = v; }
    [[nodiscard]] bool isVisible() const override { return visible; }
    void requestClose() override { closeRequested = true; }
    [[nodiscard]] bool shouldClose() const override { return closeRequested; }
    void pollEvents() override { ++pollCalls; }
    void swapBuffers() override { ++swapCalls; }
    void toggleFullscreen() override { ++toggleFullscreenCalls; }
    void captureCursor() override { ++captureCursorCalls; }
    void releaseCursor() override { ++releaseCursorCalls; }
    [[nodiscard]] window::WindowState getState() const override { return state; }
};

class MockInput final : public input::IInput {
public:
    // Tests set these to drive the queries below.
    bool      keyDown{false};
    bool      keyJustPressed{false};
    bool      keyJustReleased{false};
    bool      mouseDown{false};
    bool      mouseJustPressed{false};
    bool      mouseJustReleased{false};
    glm::vec2 delta{0.0f};
    std::size_t nextFrameCalls{0};

    [[nodiscard]] bool isKeyDown(input::Key)         const override { return keyDown; }
    [[nodiscard]] bool isKeyJustPressed(input::Key)  const override { return keyJustPressed; }
    [[nodiscard]] bool isKeyJustReleased(input::Key) const override { return keyJustReleased; }
    [[nodiscard]] bool isMouseDown(input::MouseButton)         const override { return mouseDown; }
    [[nodiscard]] bool isMouseJustPressed(input::MouseButton)  const override { return mouseJustPressed; }
    [[nodiscard]] bool isMouseJustReleased(input::MouseButton) const override { return mouseJustReleased; }
    [[nodiscard]] glm::vec2 mouseDelta() const override { return delta; }
    void nextFrame() override { ++nextFrameCalls; }
};

class MockInputSink final : public input::IInputSink {
public:
    std::vector<input::KeyEvent>   keyEvents{};
    std::vector<input::MouseEvent> mouseEvents{};

    void onKeyEvent(const input::KeyEvent &e)     override { keyEvents.push_back(e); }
    void onMouseEvent(const input::MouseEvent &e) override { mouseEvents.push_back(e); }
};

} // namespace sonnet::api::test
