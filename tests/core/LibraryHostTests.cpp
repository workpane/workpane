#include "app/CorePreferences.h"
#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "files/DirectoryListing.h"
#include "http/StaticFileResolver.h"
#include "localization/Localization.h"
#include "persistence/Database.h"
#include "persistence/InstanceLock.h"
#include "platform/PathTextHelper.h"
#include "scripting/PluginRegistry.h"
#include "scripting/ScriptRuntime.h"
#include "support/Resources.h"
#include "support/TemporaryDirectory.h"
#include "ui/Fonts.h"
#include "ui/TextureCache.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <openssl/crypto.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <thread>

namespace workpane::tests {

TEST(LibraryHost, UsesTheConfiguredTlsSecurityRelease) {
    EXPECT_STREQ(OpenSSL_version(OPENSSL_VERSION_STRING), WORKPANE_TEST_OPENSSL_VERSION);
}

TEST(LibraryHost, ReadsUnicodePluginFoldersFromPreferences) {
    TemporaryDirectory temporary;
    const std::filesystem::path folder = temporary.path() / u8"插件-ação";
    const app::CorePreferences preferences({{"pluginFolders", nlohmann::json::array({platform::PathTextHelper::utf8(folder)})}});
    const auto folders = preferences.pluginFolders();
    ASSERT_EQ(folders.size(), 1U);
    EXPECT_EQ(folders.front(), folder);
}

TEST(LibraryHost, RegistersAPluginInsideAUnicodeRoot) {
    TemporaryDirectory temporary;
    const std::filesystem::path root = temporary.path() / u8"插件-ação";
    const std::filesystem::path folder = root / "sample";
    std::filesystem::create_directories(folder);
    localization::Localization localization;
    ASSERT_TRUE(localization.registerCatalog("sample", {{"en", {{"sample.plugin.title", "Sample"}}}, {"pt", {{"sample.plugin.title", "Exemplo"}}}}).hasValue());
    const auto parsed = scripting::PluginRegistry::parse({{"id", "sample"}, {"titleKey", "sample.plugin.title"}, {"directory", platform::PathTextHelper::utf8(folder)}}, {root}, localization);
    ASSERT_TRUE(parsed.hasValue()) << parsed.error().detail;
    EXPECT_EQ(parsed.value().directory, std::filesystem::canonical(folder));
}

TEST(LibraryHost, LoadsUnicodeLuaSourceFilesWithABomAndShebang) {
    TemporaryDirectory temporary;
    const std::filesystem::path file = temporary.path() / u8"脚本-ação.lua";
    std::ofstream(file, std::ios::binary) << "\xEF\xBB\xBF#!/usr/bin/env lua\n_G.workpane_unicode_source_loaded = true\n";
    auto runtime = scripting::ScriptRuntime::create();
    ASSERT_TRUE(runtime.hasValue());
    const auto loaded = runtime.value()->loadFile(file);
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().detail;
    EXPECT_TRUE(runtime.value()->loadString("assert(workpane_unicode_source_loaded == true)", "=unicode-source-check").hasValue());
}

TEST(LibraryHost, LoadsBundledFacesFromAUnicodeDirectory) {
    const tests::TemporaryDirectory temporary;
    const std::filesystem::path folder = temporary.path() / u8"字体-ação";
    std::filesystem::create_directory(folder);

    for (const auto& entry : std::filesystem::directory_iterator(tests::Resources::fonts())) {
        std::filesystem::copy_file(entry.path(), folder / entry.path().filename());
    }

    std::set<const void*> buffers;
    ui::Fonts fonts;
    ImFontAtlas atlas;
    const auto loaded = fonts.load(atlas, folder);
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().detail;

    for (const ui::FontFace face : {ui::FontFace::Regular, ui::FontFace::SemiBold, ui::FontFace::Italic, ui::FontFace::SemiBoldItalic, ui::FontFace::Monospace, ui::FontFace::MonospaceBold, ui::FontFace::MonospaceItalic, ui::FontFace::MonospaceBoldItalic, ui::FontFace::Icon}) {
        ASSERT_NE(fonts.face(face), nullptr);
        EXPECT_GT(fonts.size(face, 14.0F), 0.0F);
        EXPECT_TRUE(fonts.face(face)->IsGlyphInFont(face == ui::FontFace::Icon ? 0xE3C5 : 'A'));
    }

    for (const ImFontConfig& source : atlas.Sources) {
        buffers.insert(source.FontData);
    }

    EXPECT_EQ(buffers.size(), 4U);
}

TEST(LibraryHost, ReportsTheUnicodeDirectoryOfAnExistingInstance) {
    TemporaryDirectory directory;
    const std::filesystem::path folder = directory.path() / u8"数据-ação";
    std::filesystem::create_directory(folder);
    auto first = persistence::InstanceLock::acquire(folder);
    ASSERT_TRUE(first.hasValue());
    const auto second = persistence::InstanceLock::acquire(folder);
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.error().code, "instance_already_running");
    EXPECT_EQ(second.error().detail, platform::PathTextHelper::utf8(folder / "workpane.lock"));
}

TEST(LibraryHost, ReportsAnUnreadableUnicodeFileAsAStructuredError) {
    TemporaryDirectory directory;
    const std::filesystem::path file = directory.path() / u8"数据库-ação" / "missing.sqlite3";
    const auto opened = persistence::Database::open(file);
    ASSERT_FALSE(opened.hasValue());
    EXPECT_EQ(opened.error().code, "database_open_failed");
    EXPECT_TRUE(opened.error().detail.starts_with(platform::PathTextHelper::utf8(file)));
}

TEST(LibraryHost, ResolvesEncodedFilesInAUnicodeDocumentRoot) {
    TemporaryDirectory temporary;
    const std::filesystem::path folder = temporary.path() / u8"网站-ação";
    const std::filesystem::path file = folder / u8"资料.txt";
    std::filesystem::create_directory(folder);
    std::ofstream(file, std::ios::binary) << "contents";
    const auto root = http::StaticFileResolver::canonicalRoot(folder);
    ASSERT_TRUE(root.hasValue()) << root.error().detail;
    const auto resolved = http::StaticFileResolver::resolve(root.value(), "/%E8%B5%84%E6%96%99.txt");
    ASSERT_TRUE(resolved.hasValue()) << resolved.error().detail;
    EXPECT_EQ(resolved.value().path, std::filesystem::canonical(file));
    EXPECT_EQ(resolved.value().size, 8U);
    EXPECT_EQ(resolved.value().mimeType, "text/plain");
}

TEST(LibraryHost, ReportsAnUnreadableUnicodeDirectoryAsAStructuredError) {
    TemporaryDirectory temporary;
    const std::filesystem::path folder = temporary.path() / u8"目录-ação";
    const std::atomic<bool> stop{false};
    const auto listed = files::DirectoryListing::list(folder, 100, stop);
    ASSERT_FALSE(listed.hasValue());
    EXPECT_EQ(listed.error().code, "files_directory_unavailable");
    EXPECT_EQ(listed.error().detail, platform::PathTextHelper::utf8(folder));
}

class LibraryHostTextureTest : public ::testing::Test {
  protected:
    ~LibraryHostTextureTest() override {
        m_workers.shutdown();
        m_mainThread.drain();
        m_textures.release();
    }

    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> m_context{ImGui::CreateContext(), &ImGui::DestroyContext};
    TemporaryDirectory m_directory;
    execution::MainThreadQueue m_mainThread;
    execution::WorkerPool m_workers{1};
    ui::TextureCache m_textures{m_workers, m_mainThread};
};

TEST_F(LibraryHostTextureTest, DecodesAnImageWithAUnicodeFilename) {
    const std::filesystem::path picture = m_directory.path() / u8"图像-ação.png";
    std::filesystem::copy_file(Resources::staged() / "plugins/task-hero/assets/terrain/sky.png", picture);
    const ui::Texture& texture = m_textures.request(picture);

    for (int attempt = 0; attempt < 200 && texture.state == ui::TextureState::Loading; ++attempt) {
        m_mainThread.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_EQ(texture.state, ui::TextureState::Ready) << texture.failure;
    EXPECT_GT(texture.width, 0);
    EXPECT_GT(texture.height, 0);
}

} // namespace workpane::tests
