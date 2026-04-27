#include <catch2/catch_test_macros.hpp>

#include "MockBackends.h"

using namespace sonnet::api;

TEST_CASE("MockGpuBuffer: records bind/update/bindBase calls", "[mock][gpu]") {
    test::MockGpuBuffer buf;

    buf.bind();
    const std::array<std::byte, 4> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    buf.update(payload.data(), payload.size());
    buf.bindBase(7);

    REQUIRE(buf.bindCalls     == 1);
    REQUIRE(buf.updateCalls   == 1);
    REQUIRE(buf.bindBaseCalls == 1);
    REQUIRE(buf.lastUpload    == std::vector(payload.begin(), payload.end()));
    REQUIRE(buf.lastBindingPoint == 7);
}

TEST_CASE("MockShaderCompiler: returns shader carrying the supplied sources", "[mock][shader]") {
    test::MockShaderCompiler compiler;
    auto shader = compiler("VS_SRC", "FS_SRC");

    REQUIRE(compiler.calls == 1);
    REQUIRE(shader != nullptr);
    REQUIRE(shader->getVertexSource()   == "VS_SRC");
    REQUIRE(shader->getFragmentSource() == "FS_SRC");
}

TEST_CASE("MockTextureFactory: each overload increments its own counter", "[mock][texture]") {
    test::MockTextureFactory factory;
    render::TextureDesc desc{};
    render::SamplerDesc sampler{};
    sonnet::core::Texels emptyTexels{std::vector<std::byte>{}};
    render::CPUTextureBuffer buf{1, 1, 4, emptyTexels};
    render::CubeMapFaces faces{
        emptyTexels, emptyTexels, emptyTexels,
        emptyTexels, emptyTexels, emptyTexels};

    auto t1 = factory.create(desc, sampler, buf);
    auto t2 = factory.create(desc, sampler, faces);
    auto t3 = factory.create(desc, sampler);

    REQUIRE(factory.fromBufferCalls == 1);
    REQUIRE(factory.fromCubeCalls   == 1);
    REQUIRE(factory.emptyCalls      == 1);
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    REQUIRE(t3 != nullptr);
}

TEST_CASE("MockRenderTargetFactory: propagates width/height into target", "[mock][rendertarget]") {
    test::MockRenderTargetFactory factory;
    render::RenderTargetDesc desc{};
    desc.width  = 800;
    desc.height = 600;

    auto rt = factory.create(desc);

    REQUIRE(factory.calls == 1);
    REQUIRE(rt->width()  == 800);
    REQUIRE(rt->height() == 600);
}

TEST_CASE("MockGpuMeshFactory: builds GpuMesh with mock buffers", "[mock][mesh]") {
    test::MockGpuMeshFactory factory;
    render::CPUMesh mesh{
        render::VertexLayout{{render::PositionAttribute{}}},
        {0, 1, 2}};
    render::PositionAttribute p{};
    mesh.addVertex({p}).addVertex({p}).addVertex({p});

    auto gpu = factory(mesh);
    REQUIRE(factory.calls == 1);
    REQUIRE(gpu != nullptr);
    REQUIRE(gpu->indexCount() == 3);
}

TEST_CASE("MockRendererBackend: records lifecycle and pipeline state changes",
         "[mock][backend]") {
    test::MockRendererBackend backend;

    backend.initialize();
    backend.beginFrame();

    backend.setViewport(1280, 720);
    backend.setDepthTest(false);
    backend.setDepthWrite(true);
    backend.setDepthFunc(render::DepthFunction::LessEqual);
    backend.setCull(render::CullMode::Front);
    backend.setBlend(true);
    backend.setFillMode(render::FillMode::Wireframe);

    render::ClearOptions clear{};
    clear.depth = 1.0f;
    backend.clear(clear);

    backend.setUniform(7, 2.5f);
    backend.drawIndexed(36);

    backend.endFrame();

    REQUIRE(backend.initializeCalls == 1);
    REQUIRE(backend.beginFrameCalls == 1);
    REQUIRE(backend.endFrameCalls   == 1);

    REQUIRE(backend.viewports    == std::vector<glm::uvec2>{{1280, 720}});
    REQUIRE(backend.depthTests   == std::vector<bool>{false});
    REQUIRE(backend.depthWrites  == std::vector<bool>{true});
    REQUIRE(backend.depthFuncs   == std::vector{render::DepthFunction::LessEqual});
    REQUIRE(backend.cullModes    == std::vector{render::CullMode::Front});
    REQUIRE(backend.blendToggles == std::vector<bool>{true});
    REQUIRE(backend.fillModes    == std::vector{render::FillMode::Wireframe});
    REQUIRE(backend.clears.size() == 1);
    REQUIRE(backend.clears[0].depth == 1.0f);

    REQUIRE(backend.uniformSets.size() == 1);
    REQUIRE(backend.uniformSets[0].location == 7);
    REQUIRE(std::get<float>(backend.uniformSets[0].value) == 2.5f);

    REQUIRE(backend.draws.size() == 1);
    REQUIRE(backend.draws[0].indexCount == 36);
}

TEST_CASE("MockRendererBackend: factories are accessible via the IRendererBackend API",
         "[mock][backend][factory]") {
    test::MockRendererBackend backend;

    auto buf = backend.createBuffer(render::BufferType::Vertex, nullptr, 0);
    REQUIRE(buf != nullptr);

    auto &tf = backend.textureFactory();
    auto t = tf.create(render::TextureDesc{}, render::SamplerDesc{});
    REQUIRE(t != nullptr);
    REQUIRE(backend.textureFactoryImpl.emptyCalls == 1);

    REQUIRE(backend.traits().apiName == "OpenGL");
}

TEST_CASE("MockWindow: state changes round-trip through the IWindow interface",
         "[mock][window]") {
    test::MockWindow w;
    w.setTitle("hello");
    w.setVisible(false);
    w.requestClose();
    w.pollEvents();
    w.swapBuffers();

    REQUIRE(w.getTitle()   == "hello");
    REQUIRE_FALSE(w.isVisible());
    REQUIRE(w.shouldClose());
    REQUIRE(w.pollCalls    == 1);
    REQUIRE(w.swapCalls    == 1);
}

TEST_CASE("MockInput: queries return whatever the test has set", "[mock][input]") {
    test::MockInput in;
    in.keyDown        = true;
    in.mouseJustPressed = true;
    in.delta          = glm::vec2{2.0f, -1.0f};

    REQUIRE(in.isKeyDown(input::Key::A));
    REQUIRE(in.isMouseJustPressed(input::MouseButton::Left));
    REQUIRE(in.mouseDelta() == glm::vec2{2.0f, -1.0f});

    in.nextFrame();
    in.nextFrame();
    REQUIRE(in.nextFrameCalls == 2);
}

TEST_CASE("MockInputSink: records key and mouse events in arrival order",
         "[mock][input][sink]") {
    test::MockInputSink sink;
    sink.onKeyEvent({input::Key::A, true});
    sink.onMouseEvent(input::MouseButtonEvent{input::MouseButton::Left, true});
    sink.onMouseEvent(input::MouseMovedEvent{{10.0f, 20.0f}});

    REQUIRE(sink.keyEvents.size()   == 1);
    REQUIRE(sink.keyEvents[0].key   == input::Key::A);
    REQUIRE(sink.keyEvents[0].pressed);
    REQUIRE(sink.mouseEvents.size() == 2);
    REQUIRE(std::holds_alternative<input::MouseButtonEvent>(sink.mouseEvents[0]));
    REQUIRE(std::holds_alternative<input::MouseMovedEvent>(sink.mouseEvents[1]));
}

TEST_CASE("MockRenderer: forwards through the IRenderer interface", "[mock][renderer]") {
    test::MockRenderer r;
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 eye{0.0f};
    render::FrameContext ctx{view, proj, eye, 800, 600, 1.0f / 60.0f, std::nullopt, {}, std::nullopt};
    std::vector<render::RenderItem> queue{};

    r.beginFrame();
    r.render(ctx, queue);
    r.render(ctx, queue);
    r.endFrame();

    REQUIRE(r.beginFrameCalls == 1);
    REQUIRE(r.renderCalls     == 2);
    REQUIRE(r.endFrameCalls   == 1);

    render::RenderOverrides o{};
    r.setOverrides(&o);
    REQUIRE(r.lastOverrides == &o);
}
