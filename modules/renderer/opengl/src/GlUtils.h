#pragma once

#include <sonnet/api/render/ITexture.h>
#include <sonnet/api/render/RenderState.h>

#include <glad/glad.h>

#include <stdexcept>

namespace sonnet::renderer::opengl {

struct GlTextureFormat {
    GLint  internalFormat;
    GLenum format;
    GLenum type;
};

[[nodiscard]] inline GlTextureFormat getGlTextureFormat(const api::render::TextureDesc &desc) {
    using namespace api::render;
    const bool srgb = desc.colorSpace == ColorSpace::sRGB;

    switch (desc.format) {
        case TextureFormat::RGB8:    return {srgb ? GL_SRGB8       : GL_RGB8,  GL_RGB,  GL_UNSIGNED_BYTE};
        case TextureFormat::RGBA8:   return {srgb ? GL_SRGB8_ALPHA8: GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case TextureFormat::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
        case TextureFormat::RGBA32F: return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
        case TextureFormat::R32F:    return {GL_R32F,    GL_RED,  GL_FLOAT};
        case TextureFormat::RG16F:   return {GL_RG16F,   GL_RG,   GL_HALF_FLOAT};
        case TextureFormat::RGB16F:  return {GL_RGB16F,  GL_RGB,  GL_HALF_FLOAT};
        case TextureFormat::Depth24: return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT};
    }
    throw std::runtime_error("GlUtils: unsupported texture format");
}

[[nodiscard]] inline GLenum toGlWrap(api::render::TextureWrap wrap) {
    switch (wrap) {
        case api::render::TextureWrap::Repeat:      return GL_REPEAT;
        case api::render::TextureWrap::ClampToEdge: return GL_CLAMP_TO_EDGE;
    }
    throw std::runtime_error("GlUtils: unsupported texture wrap");
}

[[nodiscard]] inline GLenum toGlMinFilter(api::render::MinFilter f) {
    switch (f) {
        case api::render::MinFilter::Nearest:              return GL_NEAREST;
        case api::render::MinFilter::Linear:               return GL_LINEAR;
        case api::render::MinFilter::NearestMipmapNearest: return GL_NEAREST_MIPMAP_NEAREST;
        case api::render::MinFilter::LinearMipmapNearest:  return GL_LINEAR_MIPMAP_NEAREST;
        case api::render::MinFilter::NearestMipmapLinear:  return GL_NEAREST_MIPMAP_LINEAR;
        case api::render::MinFilter::LinearMipmapLinear:   return GL_LINEAR_MIPMAP_LINEAR;
    }
    throw std::runtime_error("GlUtils: unsupported min filter");
}

[[nodiscard]] inline GLenum toGlMagFilter(api::render::MagFilter f) {
    switch (f) {
        case api::render::MagFilter::Nearest: return GL_NEAREST;
        case api::render::MagFilter::Linear:  return GL_LINEAR;
    }
    throw std::runtime_error("GlUtils: unsupported mag filter");
}

[[nodiscard]] inline GLenum toGlBlendFactor(api::render::BlendFactor f) {
    using api::render::BlendFactor;
    switch (f) {
        case BlendFactor::Zero:             return GL_ZERO;
        case BlendFactor::One:              return GL_ONE;
        case BlendFactor::SrcAlpha:         return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:         return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    }
    throw std::runtime_error("GlUtils: unsupported blend factor");
}

} // namespace sonnet::renderer::opengl
