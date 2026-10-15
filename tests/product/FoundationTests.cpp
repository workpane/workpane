#include "app/ApplicationIdentity.h"
#include "app/Product.h"
#include "persistence/DatabaseBootstrap.h"
#include "scripting/PluginRegistry.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/Resources.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "ui/shell/Shell.h"
#include "ui/shell/ToastOverlay.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace workpane::tests {

TEST(ApplicationIdentity, RejectsInvalidMetadataBeforeOpeningTheDatabase) {
    TemporaryDirectory data;
    app::ApplicationIdentity identity{"Studio", "1.2.0", "7", "Example", "org.example.studio", "studio", "org.example.studio"};
    identity.identifier = "../another-application";
    HeadlessProduct product(data.path());
    product.setIdentity(identity);
    const auto opened = product.boot();

    ASSERT_FALSE(opened.hasValue());
    EXPECT_EQ(opened.error().code, "application_identity_invalid");
    EXPECT_EQ(opened.error().detail, "identifier");
    EXPECT_FALSE(std::filesystem::exists(data.path() / persistence::DatabaseBootstrap::databaseName));

    identity.identifier = "org.example.studio";
    identity.name = "Studio\nOther";
    EXPECT_FALSE(identity.validate().hasValue());
    identity.name = "Studio ";
    identity.name.push_back(static_cast<char>(0xFF));
    EXPECT_FALSE(identity.validate().hasValue());
}

TEST(Foundation, StartsWithoutBundledPluginsAndReportsItsOwnIdentity) {
    TemporaryDirectory data;
    const auto resources = data.path() / "resources";
    std::filesystem::create_directory(resources);
    std::filesystem::copy(Resources::staged() / "lua", resources / "lua", std::filesystem::copy_options::recursive);
    std::filesystem::copy(Resources::fonts(), resources / "fonts", std::filesystem::copy_options::recursive);
    std::filesystem::create_directory(resources / "plugins");
    HeadlessProduct product(data.path() / "data", "en_US", resources);
    product.setIdentity({"Plain studio", "3.1.0", "19", "Example", "org.example.plain", "plainstudio", "org.example.plain"});
    ASSERT_TRUE(product.boot().hasValue());
    EXPECT_TRUE(product.product().plugins().navigation().empty());
    EXPECT_EQ(product.product().plugins().find("terminal"), nullptr);
    EXPECT_EQ(product.product().plugins().find("ai"), nullptr);
    EXPECT_FALSE(product.product().plugins().settingsGroups().empty());

    const auto checked = product.product().runtime().loadString(R"lua(
        local info = require("workpane.bridge").call("workpane_app_info", {})
        local api = require("workpane.api").create("workpane", "", info)
        assert(api.app.name == "Plain studio")
        assert(api.app.organization == "Example")
        assert(api.app.identifier == "org.example.plain")
        assert(api.app.version == "3.1.0" and api.app.buildNumber == "19")
        assert(api.app.engineVersion ~= api.app.version)
        assert(#api.app.plugins() == 0)
    )lua",
                                                                "foundation-identity");

    ASSERT_TRUE(checked.hasValue()) << checked.error().message;
    product.stop();
}

TEST(Foundation, LoadsTheSdkFromAResourcePathContainingLuaStringDelimiters) {
    TemporaryDirectory data;
    const auto resources = data.path() / "resources ]==] ' [=[ ]=]";
    std::filesystem::create_directory(resources);
    std::filesystem::copy(Resources::staged() / "lua", resources / "lua", std::filesystem::copy_options::recursive);
    std::filesystem::copy(Resources::fonts(), resources / "fonts", std::filesystem::copy_options::recursive);
    std::filesystem::create_directory(resources / "plugins");
    HeadlessProduct product(data.path() / "data", "en_US", resources);
    const auto booted = product.boot();

    ASSERT_TRUE(booted.hasValue()) << booted.error().detail;
    EXPECT_FALSE(product.product().plugins().settingsGroups().empty());
    product.stop();
}

TEST(Foundation, LoadsTheSdkFromAResourcePathContainingModuleSearchSeparators) {
    TemporaryDirectory data;
    const auto resources = data.path() / "resources with spaces and a semicolon;";
    std::filesystem::create_directory(resources);
    std::filesystem::copy(Resources::staged() / "lua", resources / "lua", std::filesystem::copy_options::recursive);
    std::filesystem::copy(Resources::fonts(), resources / "fonts", std::filesystem::copy_options::recursive);
    std::filesystem::create_directory(resources / "plugins");
    HeadlessProduct product(data.path() / "data", "en_US", resources);
    const auto booted = product.boot();

    ASSERT_TRUE(booted.hasValue()) << booted.error().detail;
    EXPECT_FALSE(product.product().plugins().settingsGroups().empty());
    product.stop();
}

TEST(Foundation, RefusesAMissingShippedSdkModuleDespiteAnExternalCopy) {
    TemporaryDirectory data;
    const auto resources = data.path() / "resources";
    std::filesystem::create_directory(resources);
    std::filesystem::copy(Resources::staged() / "lua", resources / "lua", std::filesystem::copy_options::recursive);
    std::filesystem::copy(Resources::fonts(), resources / "fonts", std::filesystem::copy_options::recursive);
    std::filesystem::create_directory(resources / "plugins");
    std::filesystem::copy(resources / "lua" / "workpane", data.path() / "workpane", std::filesystem::copy_options::recursive);
    ASSERT_TRUE(std::filesystem::remove(resources / "lua" / "workpane" / "application.lua"));
    HeadlessProduct product(data.path() / "data", "en_US", resources);
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(data.path());
    const auto booted = product.boot();
    std::filesystem::current_path(previous);

    ASSERT_FALSE(booted.hasValue());
    EXPECT_EQ(booted.error().code, "script_load_failed");
    product.stop();
}

TEST(Foundation, LoadsOnlyTheSuppliedPluginAndRendersTheConsumerIdentity) {
    TemporaryDirectory data;
    const auto resources = data.path() / "resources";
    std::filesystem::create_directory(resources);
    std::filesystem::copy(Resources::staged() / "lua", resources / "lua", std::filesystem::copy_options::recursive);
    std::filesystem::copy(Resources::fonts(), resources / "fonts", std::filesystem::copy_options::recursive);
    std::filesystem::copy(std::filesystem::path(WORKPANE_TEST_EXAMPLE_PLUGINS), resources / "plugins", std::filesystem::copy_options::recursive);
    HeadlessProduct product(data.path() / "data", "en_US", resources);
    product.setIdentity({"Independent studio", "1.2.0", "7", "Example", "org.example.studio", "examplestudio", "org.example.studio"});
    ASSERT_TRUE(product.boot().hasValue());
    EXPECT_NE(product.product().plugins().find("studio"), nullptr);
    EXPECT_EQ(product.product().plugins().find("donate"), nullptr);
    EXPECT_EQ(product.product().plugins().navigation().size(), 1U);
    ASSERT_TRUE(product.navigate("studio:home").hasValue());
    // clang-format off
    const auto mounted = [&product]() { return SurfaceReader(product.declared("view:studio:home")).mounted(); };
    // clang-format on
    ASSERT_TRUE(product.frameUntil(mounted));
    const SurfaceReader reader(product.declared("view:studio:home"));
    const auto header = reader.nodes("pageHeader")[0];
    const auto label = reader.nodes("label")[0];
    EXPECT_EQ(reader.properties(header)["title"]["args"], nlohmann::json::array({"Independent studio"}));
    EXPECT_EQ(reader.properties(label)["text"]["args"], nlohmann::json::array({"1.2.0"}));
    product.stop();
}

TEST(Foundation, KeepsNativeCredentialsSeparateForApplicationsUsingTheSamePlugin) {
    TemporaryDirectory data;
    const auto resources = data.path() / "resources";
    std::filesystem::create_directory(resources);
    std::filesystem::copy(Resources::staged() / "lua", resources / "lua", std::filesystem::copy_options::recursive);
    std::filesystem::copy(Resources::fonts(), resources / "fonts", std::filesystem::copy_options::recursive);
    std::filesystem::copy(std::filesystem::path(WORKPANE_TEST_EXAMPLE_PLUGINS), resources / "plugins", std::filesystem::copy_options::recursive);
    std::map<std::pair<std::string, std::string>, std::string> sharedCredentials;

    {
        HeadlessProduct first(data.path() / "first", "en_US", resources);
        first.setIdentity({"First studio", "1.0.0", "1", "Example", "org.example.first", "first", "org.example.first"});
        ASSERT_TRUE(first.boot().hasValue());
        ASSERT_TRUE(first.product()
                        .runtime()
                        .loadString(R"lua(
            local info = require("workpane.bridge").call("workpane_app_info", {})
            local api = require("workpane.api").create("studio", "", info)
            require("workpane.task").run("studio", "fixture", function()
                require("workpane.task").await(api.credentials.write("token", "Private fixture credential"))
                api.notify.success("Credential fixture", "Stored")
            end)
        )lua",
                                    "first-application-credential")
                        .hasValue());
        // clang-format off
        const auto stored = [&first]() { return first.product().shell().toasts().showing("Credential fixture", "Stored"); };
        // clang-format on
        ASSERT_TRUE(first.frameUntil(stored));

        {
            const std::lock_guard lock(first.system().credentialMutex);
            sharedCredentials = first.system().credentials;
        }

        ASSERT_EQ(sharedCredentials.size(), 1U);
        EXPECT_EQ(sharedCredentials.begin()->first.first, "org.example.first.studio");
        first.stop();
    }

    HeadlessProduct second(data.path() / "second", "en_US", resources);
    second.setIdentity({"Second studio", "2.0.0", "1", "Example", "org.example.second", "second", "org.example.second"});
    second.system().credentials = sharedCredentials;
    ASSERT_TRUE(second.boot().hasValue());
    ASSERT_TRUE(second.product()
                    .runtime()
                    .loadString(R"lua(
        local info = require("workpane.bridge").call("workpane_app_info", {})
        local api = require("workpane.api").create("studio", "", info)
        require("workpane.task").run("studio", "fixture", function()
            assert(require("workpane.task").await(api.credentials.read("token")) == nil)
            api.notify.success("Credential fixture", "Isolated")
        end)
    )lua",
                                "second-application-credential")
                    .hasValue());
    // clang-format off
    const auto isolated = [&second]() { return second.product().shell().toasts().showing("Credential fixture", "Isolated"); };
    // clang-format on
    ASSERT_TRUE(second.frameUntil(isolated));
    second.stop();
}

} // namespace workpane::tests
