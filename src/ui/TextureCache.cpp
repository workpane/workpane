#include "ui/TextureCache.h"

#include "text/Base64Helper.h"

#include <imgui_internal.h>
#include <stb_image.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace workpane::ui {

TextureCache::TextureCache(execution::WorkerPool& workers, execution::MainThreadQueue& mainThread) : m_workers(workers), m_mainThread(mainThread) {}

void TextureCache::setChangeHandler(ChangeHandler handler) {
    m_changed = std::move(handler);
}

const Texture& TextureCache::request(const std::filesystem::path& file) {
    const std::string key = file.string();

    if (const auto found = m_textures.find(key); found != m_textures.end()) {
        found->second.used = m_frame;
        return *found->second.texture;
    }

    // clang-format off
    return start(key, [key]() { return decodeFile(key); });
    // clang-format on
}

const Texture& TextureCache::requestData(const std::string& address) {
    if (const auto found = m_textures.find(address); found != m_textures.end()) {
        found->second.used = m_frame;
        return *found->second.texture;
    }

    // clang-format off
    return start(address, [address]() { return decodeData(address); });
    // clang-format on
}

// Keeps a picture decoded for as long as something holds it, and starts decoding it at once, so it is ready before the first frame that draws it.
void TextureCache::hold(const std::filesystem::path& file) {
    const std::string key = file.string();
    auto found = m_textures.find(key);

    if (found == m_textures.end()) {
        // clang-format off
        std::ignore = start(key, [key]() { return decodeFile(key); });
        // clang-format on
        found = m_textures.find(key);
    }

    ++found->second.holds;
}

// Lets go of a held picture, which leaves the cache with the next frame once nothing holds it, unless that frame draws it.
void TextureCache::drop(const std::filesystem::path& file) {
    const auto found = m_textures.find(file.string());

    if (found == m_textures.end() || found->second.holds == 0) {
        return;
    }

    --found->second.holds;

    if (found->second.holds == 0) {
        found->second.used = m_frame - unusedFrames;
    }
}

// A picture declaring more pixels than the product draws is refused from its header, before a single pixel is decoded.
TextureCache::Decoded TextureCache::decodeFile(const std::string& file) {
    Decoded decoded;
    int channels = 0;

    if (stbi_info(file.c_str(), &decoded.width, &decoded.height, &channels) == 0) {
        decoded.failure = stbi_failure_reason();
        return decoded;
    }

    if (decoded.width > largestSide || decoded.height > largestSide) {
        decoded.failure = "The picture is larger than the product draws";
        return decoded;
    }

    decoded.pixels.reset(stbi_load(file.c_str(), &decoded.width, &decoded.height, &channels, 4));

    if (decoded.pixels == nullptr) {
        decoded.failure = stbi_failure_reason();
    }

    return decoded;
}

// A data address names an image type and carries the bytes of the image in Base64 after the marker, and a picture there is as small as the icon of a page.
TextureCache::Decoded TextureCache::decodeData(const std::string& address) {
    Decoded decoded;
    const std::size_t marker = address.find(base64Marker);
    const auto bytes = address.starts_with(dataPrefix) && marker != std::string::npos ? text::Base64Helper::decode(std::string_view(address).substr(marker + base64Marker.size())) : std::nullopt;

    if (!bytes.has_value() || bytes->empty() || bytes->size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        decoded.failure = "The data address carries no image";
        return decoded;
    }

    int channels = 0;
    const int length = static_cast<int>(bytes->size());

    if (stbi_info_from_memory(bytes->data(), length, &decoded.width, &decoded.height, &channels) == 0) {
        decoded.failure = stbi_failure_reason();
        return decoded;
    }

    if (decoded.width > largestDataSide || decoded.height > largestDataSide) {
        decoded.failure = "The picture is larger than the product draws";
        return decoded;
    }

    decoded.pixels.reset(stbi_load_from_memory(bytes->data(), length, &decoded.width, &decoded.height, &channels, 4));

    if (decoded.pixels == nullptr) {
        decoded.failure = stbi_failure_reason();
    }

    return decoded;
}

Texture& TextureCache::start(const std::string& key, std::function<Decoded()> decode) {
    Entry& entry = m_textures.emplace(key, Entry{std::make_unique<Texture>(), m_frame}).first->second;
    const std::weak_ptr<bool> alive = m_alive;

    // clang-format off
    m_workers.post([this, alive, key, decode = std::move(decode), mainThread = &m_mainThread]() {
        auto decoded = std::make_shared<Decoded>(decode());
        mainThread->post([this, alive, key, decoded]() {
            if (alive.expired()) {
                return;
            }

            finish(key, std::move(*decoded));
        });
    });
    // clang-format on

    return *entry.texture;
}

// A picture keeps its pixels only until the renderer made its texture from them, so every picture costs its texture alone.
void TextureCache::update() {
    ++m_frame;
    // clang-format off
    std::erase_if(m_uploading, [](ImTextureData* data) {
        if (data->Status != ImTextureStatus_OK) {
            return false;
        }

        data->DestroyPixels();

        return true;
    });
    // clang-format on

    retireUnused();
}

// A picture nothing holds and no drawn frame asked for in a while leaves the cache, and its texture is let go only once the renderer destroyed it, so a picture nobody shows costs nothing.
void TextureCache::retireUnused() {
    for (auto entry = m_textures.begin(); entry != m_textures.end();) {
        Texture& texture = *entry->second.texture;
        const bool uploading = texture.data != nullptr && texture.data->Status != ImTextureStatus_OK;

        if (entry->second.holds > 0 || m_frame - entry->second.used < unusedFrames || texture.state == TextureState::Loading || uploading) {
            ++entry;
            continue;
        }

        if (texture.data != nullptr) {
            texture.data->WantDestroyNextFrame = true;
            texture.data->SetStatus(ImTextureStatus_WantDestroy);
            m_retiring.push_back(std::move(texture.data));
        }

        entry = m_textures.erase(entry);
    }

    // clang-format off
    std::erase_if(m_retiring, [](const std::unique_ptr<ImTextureData>& data) {
        if (data->Status != ImTextureStatus_Destroyed) {
            return false;
        }

        ImGui::UnregisterUserTexture(data.get());

        return true;
    });
    // clang-format on
}

// Releases every texture while the ImGui context that registered them still exists.
void TextureCache::release() {
    for (auto& [key, entry] : m_textures) {
        if (entry.texture->data != nullptr) {
            ImGui::UnregisterUserTexture(entry.texture->data.get());
        }
    }

    for (const auto& data : m_retiring) {
        ImGui::UnregisterUserTexture(data.get());
    }

    m_uploading.clear();
    m_retiring.clear();
    m_textures.clear();
}

void TextureCache::finish(const std::string& key, Decoded decoded) {
    const auto found = m_textures.find(key);

    if (found == m_textures.end()) {
        return;
    }

    Texture& texture = *found->second.texture;

    if (!decoded.failure.empty()) {
        texture.state = TextureState::Failed;
        texture.failure = std::move(decoded.failure);
    } else {
        // The renderer creates the texture on its next frame, because it is registered with the ImGui context it serves.
        texture.data = std::make_unique<ImTextureData>();
        texture.data->Create(ImTextureFormat_RGBA32, decoded.width, decoded.height);
        std::memcpy(texture.data->GetPixels(), decoded.pixels.get(), static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.height) * 4U);
        ImGui::RegisterUserTexture(texture.data.get());
        m_uploading.push_back(texture.data.get());
        texture.width = decoded.width;
        texture.height = decoded.height;
        texture.state = TextureState::Ready;
    }

    if (m_changed) {
        m_changed();
    }
}

} // namespace workpane::ui
