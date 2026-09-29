#pragma once

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "ui/Texture.h"

#include <stb_image.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// Images are decoded on a worker and uploaded by the renderer, so asking for one never blocks the frame that draws it, and an image keeps its pixels only until its texture exists.
// An image is a file or a data address carrying its bytes in Base64, such as the icon a web page draws, its size is bounded before it is decoded, and one no frame draws for a while is released.
// An image something holds is decoded at once and kept for as long as it is held, and it is released as soon as nothing holds it and no frame draws it.
class TextureCache final {
  public:
    using ChangeHandler = std::function<void()>;

    TextureCache(execution::WorkerPool& workers, execution::MainThreadQueue& mainThread);

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    void setChangeHandler(ChangeHandler handler);
    [[nodiscard]] const Texture& request(const std::filesystem::path& file);
    [[nodiscard]] const Texture& requestData(const std::string& address);
    void hold(const std::filesystem::path& file);
    void drop(const std::filesystem::path& file);
    void update();
    void release();

  private:
    static constexpr std::string_view dataPrefix{"data:image/"};
    static constexpr std::string_view base64Marker{";base64,"};
    static constexpr int largestSide{8192};
    static constexpr int largestDataSide{512};
    static constexpr std::uint64_t unusedFrames{600};

    struct Decoded final {
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{nullptr, &stbi_image_free};
        int width{0};
        int height{0};
        std::string failure;
    };

    struct Entry final {
        std::unique_ptr<Texture> texture;
        std::uint64_t used{0};
        int holds{0};
    };

    [[nodiscard]] static Decoded decodeFile(const std::string& file);
    [[nodiscard]] static Decoded decodeData(const std::string& address);
    [[nodiscard]] Texture& start(const std::string& key, std::function<Decoded()> decode);
    void finish(const std::string& key, Decoded decoded);
    void retireUnused();

    execution::WorkerPool& m_workers;
    execution::MainThreadQueue& m_mainThread;
    std::map<std::string, Entry, std::less<>> m_textures;
    std::vector<ImTextureData*> m_uploading;
    std::vector<std::unique_ptr<ImTextureData>> m_retiring;
    std::uint64_t m_frame{0};
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    ChangeHandler m_changed;
};

} // namespace workpane::ui
