# Render graph (demo)

`apps/demo/RenderGraph.{h,cpp}` is a small declarative frame graph used by the demo's post-process pipeline. It is **not** part of the engine — it lives in the demo because it is specific to the demo's pass mix. The frontend `Renderer` does not depend on it.

## Model

A graph is a list of *passes*. Each pass declares:

- A name (for debugging).
- A list of `GPUTextureHandle`s it reads from.
- One `RenderTargetHandle` it writes to.
- A `RGClearDesc` (optional per-attachment colour clears + optional depth clear).
- An `isOutput` flag that prevents culling (terminal sinks).
- An execute callback that takes the scene `FrameContext` and a fullscreen-quad `FrameContext`.

A texture-to-RT mapping is established up front via `registerTexSource(rt, tex)` — this lets the graph translate "this pass reads texture X" into "this pass depends on the RT that produced X". The first writer wins.

## Build → compile → execute

```cpp
RenderGraph g{renderer, backend};
g.registerTexSource(gbufRT, gbufAlbedoTex);
g.registerTexSource(gbufRT, gbufDepthTex);
// ...

g.addPass("gbuffer", /*reads=*/{}, gbufRT,
          /*clear=*/{ .colors = {{0,{0,0,0,0}}, {1,{0,0,0,0}}, {2,{0,0,0,0}}}, .depth = 1.0f },
          /*isOutput=*/false, [&](const auto &ctx, const auto &) {
    // submit the scene queue to the bound g-buffer RT
});

g.addPass("ssao", /*reads=*/{gbufNormalTex, gbufDepthTex}, ssaoRT,
          {}, false, [&](auto&, auto &ppCtx) { fullscreenQuad(ssaoMat, ppCtx); });

// ... more passes ...

g.compile();                                   // topological sort + dead-pass cull
g.execute(frameContext, {fbWidth, fbHeight});  // run each frame
```

`compile()` runs Kahn's algorithm on the dependency graph, then prunes any pass whose results are not transitively reachable from a pass with `isOutput = true`. A cycle throws `std::runtime_error`.

`execute()` walks the live passes in dependency order. For each pass it binds the target RT, applies `clear` (skipped when neither `colors` nor `depth` is set), sets the viewport, and invokes the callback. The second `FrameContext` argument is a fullscreen-quad context — identity matrices, viewport sized to the frame — used by post-process passes that don't need the scene camera.

## Why it exists

The demo composes ten or more passes (G-buffer, SSAO, deferred lighting, sky, SSR, bloom bright, two bloom blurs, tonemap, FXAA, outline mask, outline, picking) and many are conditional on UI toggles. Hardcoding the order in `PostProcess::execute` is brittle: toggling SSAO off has to skip both the SSAO pass and its blur, but leave deferred lighting reading the unblurred-but-existing texture. The graph encodes the dependencies once and lets `compile()` decide which passes are actually live based on which ones produce textures other passes consume.

`PostProcess::buildGraph()` rebuilds and recompiles the graph whenever a *structural* toggle (SSAO, FXAA, SSR, outline-active, SSAO-show-mode) flips. Numeric tunables (bias, exposure, kernel size) do not require a rebuild — they are uploaded into the existing material instances each frame.
