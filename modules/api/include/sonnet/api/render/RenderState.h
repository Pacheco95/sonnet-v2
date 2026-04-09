#pragma once

#include <sonnet/core/Hash.h>

namespace sonnet::api::render {

enum class BlendMode    { Opaque, Alpha, Additive };
enum class CullMode     { None, Back, Front };
enum class FillMode     { Wireframe, Solid };
enum class DepthFunction{ Less, LessEqual };
enum class BlendFactor  { Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha };

struct RenderState {
    bool         depthTest  = true;
    bool         depthWrite = true;
    DepthFunction depthFunc = DepthFunction::Less;
    BlendMode    blend      = BlendMode::Opaque;
    CullMode     cull       = CullMode::Back;
    FillMode     fill       = FillMode::Solid;

    auto operator<=>(const RenderState &) const = default;

    struct Hasher {
        std::size_t operator()(const RenderState &s) const noexcept {
            std::size_t seed = 0;
            core::hashCombine(seed, s.depthTest);
            core::hashCombine(seed, s.depthWrite);
            core::hashCombine(seed, static_cast<int>(s.depthFunc));
            core::hashCombine(seed, static_cast<int>(s.blend));
            core::hashCombine(seed, static_cast<int>(s.cull));
            core::hashCombine(seed, static_cast<int>(s.fill));
            return seed;
        }
    };
};

// Per-frame overrides applied on top of material render state.
struct RenderOverrides {
    std::optional<FillMode>  fill;
    std::optional<bool>      depthTest;
    std::optional<CullMode>  cull;
};

} // namespace sonnet::api::render
