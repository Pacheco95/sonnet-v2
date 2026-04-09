#include <sonnet/loaders/TextureLoader.h>

#include <stb_image.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace sonnet::loaders {

api::render::CPUTextureBuffer TextureLoader::load(const std::filesystem::path &path,
                                                   const TextureLoadOptions &options) {
    stbi_set_flip_vertically_on_load(options.flipVertically ? 1 : 0);

    int w = 0, h = 0, channels = 0;
    unsigned char *data = stbi_load(path.string().c_str(), &w, &h, &channels, 0);
    if (!data) {
        throw std::runtime_error("TextureLoader: failed to load '" + path.string() +
                                 "': " + stbi_failure_reason());
    }

    const std::size_t byteCount = static_cast<std::size_t>(w) *
                                  static_cast<std::size_t>(h) *
                                  static_cast<std::size_t>(channels);

    std::vector<std::byte> bytes(byteCount);
    std::memcpy(bytes.data(), data, byteCount);
    stbi_image_free(data);

    return api::render::CPUTextureBuffer{
        .width    = static_cast<std::uint32_t>(w),
        .height   = static_cast<std::uint32_t>(h),
        .channels = static_cast<std::uint32_t>(channels),
        .texels   = core::Texels{std::move(bytes)},
    };
}

} // namespace sonnet::loaders
