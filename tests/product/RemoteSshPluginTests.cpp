#include "app/Product.h"
#include "persistence/PreferenceStore.h"
#include "support/HeadlessProduct.h"
#include "support/ProductDatabase.h"
#include "support/RootedPath.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "ui/model/SurfaceStore.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::tests {

class RemoteSshPluginTest : public ::testing::Test {
  protected:
    static constexpr std::string_view page{"view:remote-ssh:connections"};

    [[nodiscard]] const std::filesystem::path& data() const {
        return m_data.path();
    }

    static void boot(HeadlessProduct& product) {
        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("remote-ssh:connections").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(page)).mounted(); }));
        // clang-format on
    }

    [[nodiscard]] static std::string dialog(HeadlessProduct& product) {
        std::string found;

        for (int number = 1; number < 32; ++number) {
            const std::string candidate = "dialog:remote-ssh:" + std::to_string(number);

            if (SurfaceReader(product.declared(candidate)).mounted()) {
                found = candidate;
            }
        }

        return found;
    }

    [[nodiscard]] static nlohmann::json profiles(HeadlessProduct& product) {
        return product.product().preferences().document("remote-ssh").value("profiles", nlohmann::json::array());
    }

    void storeProfiles(nlohmann::json saved) const {
        auto database = ProductDatabase::open(m_data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "remote-ssh", {{"profiles", std::move(saved)}}).hasValue());
    }

    void refusePreferenceWrites() const {
        auto database = ProductDatabase::open(m_data);
        ASSERT_TRUE(database.run("CREATE TRIGGER refuse_ssh_settings BEFORE INSERT ON core_settings WHEN NEW.owner = 'remote-ssh' BEGIN SELECT RAISE(ABORT, 'refused'); END").hasValue());
    }

    void allowPreferenceWrites() const {
        auto database = ProductDatabase::open(m_data);
        ASSERT_TRUE(database.run("DROP TRIGGER refuse_ssh_settings").hasValue());
    }

    static void emit(HeadlessProduct& product, std::string_view surface, std::string_view kind, std::size_t index, std::string_view event, nlohmann::json value) {
        const auto nodes = SurfaceReader(product.declared(surface)).nodes(kind);
        ASSERT_GT(nodes.size(), index);
        product.emit(surface, nodes[index], event, std::move(value));
    }

    [[nodiscard]] static std::string newProfile(HeadlessProduct& product) {
        const auto button = SurfaceReader(product.declared(page)).nodeShowing("remote-ssh.dialog.new");
        EXPECT_TRUE(button.has_value());

        if (!button.has_value()) {
            return "";
        }

        product.emit(page, *button, "click", nlohmann::json::object());
        // clang-format off
        EXPECT_TRUE(product.frameUntil([&]() { return !dialog(product).empty(); }));
        // clang-format on
        return dialog(product);
    }

  private:
    TemporaryDirectory m_data;
};

TEST_F(RemoteSshPluginTest, SavesAndRestoresNamedConnectionsWithoutCredentials) {
    std::string id;

    {
        HeadlessProduct product(data());
        boot(product);
        const std::string form = newProfile(product);
        ASSERT_FALSE(form.empty());
        emit(product, form, "textField", 0, "change", {{"value", "Development"}});
        emit(product, form, "textField", 1, "change", {{"value", "configured-alias"}});
        emit(product, form, "pathField", 2, "change", {{"value", "/project"}});
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product).size() == 1U; }));
        // clang-format on
        const auto saved = profiles(product)[0];
        id = saved["id"];
        EXPECT_EQ(saved["name"], "Development");
        EXPECT_EQ(saved["host"], "configured-alias");
        EXPECT_EQ(saved["user"], "");
        EXPECT_EQ(saved["port"], 0);
        EXPECT_EQ(saved["identity"], "");
        EXPECT_EQ(saved["configuration"], "");
        EXPECT_EQ(saved["root"], "/project");
        EXPECT_FALSE(saved.contains("platform"));
        EXPECT_EQ(saved["timeoutSeconds"], 60);
        EXPECT_EQ(saved["savePassword"], false);
        EXPECT_EQ(saved.size(), 10U);
        EXPECT_FALSE(saved.contains("password"));
        EXPECT_FALSE(saved.contains("token"));
        const SurfaceReader reader(product.declared(page));
        ASSERT_EQ(reader.nodes("table").size(), 1U);
        EXPECT_EQ(reader.properties(reader.nodes("table")[0])["rows"][0]["id"], id);
        product.stop();
    }

    HeadlessProduct restored(data());
    boot(restored);
    EXPECT_EQ(profiles(restored)[0]["id"], id);
    EXPECT_EQ(profiles(restored)[0]["name"], "Development");
    const SurfaceReader reader(restored.declared(page));
    ASSERT_EQ(reader.nodes("table").size(), 1U);
    EXPECT_EQ(reader.properties(reader.nodes("table")[0])["rows"][0]["cells"][1], "configured-alias");
}

TEST_F(RemoteSshPluginTest, SavesKeepsAndRemovesAPasswordThroughTheNativeCredentialInterface) {
    HeadlessProduct product(data());
    boot(product);
    std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Windows"}});
    emit(product, form, "textField", 1, "change", {{"value", "windows-host"}});
    emit(product, form, "checkbox", 0, "change", {{"checked", true}});
    emit(product, form, "pathField", 2, "change", {{"value", "c:"}});
    const auto enteredPassword = SurfaceReader(product.declared(form)).nodes("textField")[3];
    ASSERT_TRUE(product.product().surfaces().command(form, "remote-ssh", enteredPassword, "focus", nlohmann::json::object(), product.product().render()).hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([]() { const auto* input = ImGui::GetInputTextState(ImGui::GetActiveID()); return input != nullptr && (input->Flags & ImGuiInputTextFlags_Password) != 0; }));
    // clang-format on
    product.type("Private profile credential");
    product.press(ImGuiKey_Enter);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return dialog(product).empty() && profiles(product).size() == 1U; }));
    // clang-format on
    const auto saved = profiles(product)[0];
    EXPECT_EQ(saved["root"], "C:/");
    EXPECT_EQ(saved["savePassword"], true);
    EXPECT_FALSE(saved.contains("password"));
    EXPECT_EQ(saved.dump().find("Private profile credential"), std::string::npos);
    ASSERT_EQ(product.system().credentials.size(), 1U);
    EXPECT_EQ(product.system().credentials.begin()->second, "Private profile credential");
    const auto original = product.system().credentials;
    emit(product, page, "table", 0, "activate", {{"id", saved["id"]}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return !dialog(product).empty(); }));
    // clang-format on
    form = dialog(product);
    EXPECT_TRUE(SurfaceReader(product.declared(form)).showsKey("remote-ssh.password.saved"));
    const auto password = SurfaceReader(product.declared(form)).nodes("textField")[3];
    EXPECT_EQ(SurfaceReader(product.declared(form)).properties(password)["value"], "");
    product.system().credentialsFail = true;
    emit(product, form, "textField", 0, "change", {{"value", "Renamed"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return dialog(product).empty() && profiles(product)[0]["name"] == "Renamed"; }));
    // clang-format on
    EXPECT_EQ(product.system().credentials, original);
    product.system().credentialsFail = false;
    emit(product, page, "table", 0, "activate", {{"id", saved["id"]}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return !dialog(product).empty(); }));
    // clang-format on
    form = dialog(product);
    emit(product, form, "checkbox", 0, "change", {{"checked", false}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return dialog(product).empty(); }));
    // clang-format on
    EXPECT_TRUE(product.system().credentials.empty());
    EXPECT_EQ(profiles(product)[0]["savePassword"], false);
    EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE level = 'error'").empty());
}

TEST_F(RemoteSshPluginTest, EditsAndRemovesProfilesWithoutPasswordsWhenNativeStorageIsUnavailable) {
    HeadlessProduct product(data());
    product.system().credentialsFail = true;
    boot(product);
    product.recordCalls("workpane_credential_read");
    product.recordCalls("workpane_credential_write");
    product.recordCalls("workpane_credential_erase");
    std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Development"}});
    emit(product, form, "textField", 1, "change", {{"value", "host"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product).size() == 1U; }));
    // clang-format on
    const std::string id = profiles(product)[0]["id"];
    EXPECT_EQ(profiles(product)[0]["savePassword"], false);
    emit(product, page, "table", 0, "activate", {{"id", id}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !dialog(product).empty(); }));
    // clang-format on
    form = dialog(product);
    emit(product, form, "textField", 0, "change", {{"value", "Renamed"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product)[0]["name"] == "Renamed"; }));
    // clang-format on
    emit(product, page, "table", 0, "action", {{"id", id}, {"action", "remove"}});
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return profiles(product).empty(); }));
    // clang-format on
    EXPECT_EQ(product.calls("workpane_credential_read"), 0U);
    EXPECT_EQ(product.calls("workpane_credential_write"), 0U);
    EXPECT_EQ(product.calls("workpane_credential_erase"), 0U);
    EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE level = 'error'").empty());
}

TEST_F(RemoteSshPluginTest, RestoresBothCredentialEntriesWhenAnEndpointChangeCannotCommit) {
    HeadlessProduct product(data());
    boot(product);
    std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Development"}});
    emit(product, form, "textField", 1, "change", {{"value", "original-host"}});
    emit(product, form, "textField", 3, "change", {{"value", "Private original credential"}});
    emit(product, form, "checkbox", 0, "change", {{"checked", true}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product).size() == 1U; }));
    // clang-format on
    const auto original = product.system().credentials;
    const std::string id = profiles(product)[0]["id"];
    emit(product, page, "table", 0, "activate", {{"id", id}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !dialog(product).empty(); }));
    // clang-format on
    form = dialog(product);
    emit(product, form, "textField", 1, "change", {{"value", "replacement-host"}});
    emit(product, form, "textField", 3, "change", {{"value", "Private replacement credential"}});
    refusePreferenceWrites();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("remote-ssh.error.save"); }));
    // clang-format on
    EXPECT_EQ(product.system().credentials, original);
    EXPECT_EQ(profiles(product)[0]["host"], "original-host");
    EXPECT_EQ(profiles(product)[0]["savePassword"], true);
    allowPreferenceWrites();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product)[0]["host"] == "replacement-host"; }));
    // clang-format on
    ASSERT_EQ(product.system().credentials.size(), 1U);
    EXPECT_NE(product.system().credentials.begin()->first, original.begin()->first);
    EXPECT_EQ(product.system().credentials.begin()->second, "Private replacement credential");
}

TEST_F(RemoteSshPluginTest, StoresAPasswordForAProfileWithTheMaximumIdentifierLength) {
    const std::string id(64, 'a');
    storeProfiles(nlohmann::json::array({{{"id", id}, {"name", "Development"}, {"host", "host"}, {"user", ""}, {"port", 0}, {"identity", ""}, {"configuration", ""}, {"root", "/project"}, {"timeoutSeconds", 30}}}));
    HeadlessProduct product(data());
    boot(product);
    emit(product, page, "table", 0, "activate", {{"id", id}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !dialog(product).empty(); }));
    // clang-format on
    const std::string form = dialog(product);
    emit(product, form, "textField", 3, "change", {{"value", "Private profile credential"}});
    emit(product, form, "checkbox", 0, "change", {{"checked", true}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && product.system().credentials.size() == 1U; }));
    // clang-format on
    EXPECT_EQ(profiles(product)[0]["id"], id);
    EXPECT_EQ(product.system().credentials.begin()->second, "Private profile credential");
    EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE level = 'error'").empty());
}

TEST_F(RemoteSshPluginTest, RestoresThePasswordWhenRemovingItsProfileCannotCommit) {
    HeadlessProduct product(data());
    boot(product);
    const std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Development"}});
    emit(product, form, "textField", 1, "change", {{"value", "host"}});
    emit(product, form, "textField", 3, "change", {{"value", "Private saved credential"}});
    emit(product, form, "checkbox", 0, "change", {{"checked", true}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product).size() == 1U; }));
    // clang-format on
    const auto original = product.system().credentials;
    const std::string id = profiles(product)[0]["id"];
    refusePreferenceWrites();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    product.recordCalls("workpane_notify");
    emit(product, page, "table", 0, "action", {{"id", id}, {"action", "remove"}});
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const auto notice = product.lastCall("workpane_notify"); return notice.is_object() && notice.value("plugin", "") == "remote-ssh" && notice.value("severity", "") == "error"; }));
    // clang-format on
    EXPECT_EQ(product.system().credentials, original);
    ASSERT_EQ(profiles(product).size(), 1U);
    EXPECT_EQ(profiles(product)[0]["id"], id);
    EXPECT_EQ(profiles(product)[0]["savePassword"], true);
    allowPreferenceWrites();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    emit(product, page, "table", 0, "action", {{"id", id}, {"action", "remove"}});
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return profiles(product).empty(); }));
    // clang-format on
    EXPECT_TRUE(product.system().credentials.empty());
}

TEST_F(RemoteSshPluginTest, RemovesAnUncommittedPasswordWhenItsProfileCannotBeSaved) {
    refusePreferenceWrites();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    HeadlessProduct product(data());
    boot(product);
    const std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Development"}});
    emit(product, form, "textField", 1, "change", {{"value", "host"}});
    emit(product, form, "textField", 3, "change", {{"value", "Private uncommitted credential"}});
    emit(product, form, "checkbox", 0, "change", {{"checked", true}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product, &form]() { return SurfaceReader(product.declared(form)).showsKey("remote-ssh.error.save"); }));
    // clang-format on
    EXPECT_TRUE(profiles(product).empty());
    EXPECT_TRUE(product.system().credentials.empty());
    ASSERT_TRUE(product.answerDialog("cancel"));
}

TEST_F(RemoteSshPluginTest, ShowsCredentialStorageFailureInsideTheProfileForm) {
    HeadlessProduct product(data());
    boot(product);
    product.system().credentialsFail = true;
    const std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Development"}});
    emit(product, form, "textField", 1, "change", {{"value", "host"}});
    emit(product, form, "textField", 3, "change", {{"value", "Private profile credential"}});
    emit(product, form, "checkbox", 0, "change", {{"checked", true}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product, &form]() { return SurfaceReader(product.declared(form)).showsKey("remote-ssh.error.credentials"); }));
    // clang-format on
    EXPECT_TRUE(profiles(product).empty());
    ASSERT_TRUE(product.answerDialog("cancel"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return dialog(product).empty(); }));
    // clang-format on
    EXPECT_TRUE(product.system().credentials.empty());
    EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE level = 'error'").empty());
}

TEST_F(RemoteSshPluginTest, ValidatesAbsoluteRemoteRootsWithoutAskingForAnOperatingSystem) {
    HeadlessProduct product(data());
    boot(product);
    const std::string form = newProfile(product);
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const SurfaceReader reader(product.declared(form)); const auto alert = reader.nodeShowing("remote-ssh.error.name"); return alert.has_value() && reader.properties(*alert).value("visible", false); }));
    // clang-format on
    EXPECT_TRUE(profiles(product).empty());
    emit(product, form, "textField", 0, "change", {{"value", "Windows workspace"}});
    emit(product, form, "textField", 1, "change", {{"value", "windows-host"}});
    emit(product, form, "pathField", 2, "change", {{"value", "relative/project"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("remote-ssh.error.root"); }));
    // clang-format on
    EXPECT_TRUE(profiles(product).empty());
    emit(product, form, "pathField", 2, "change", {{"value", "C:/project"}});
    emit(product, form, "numberField", 0, "change", {{"value", 2222}});
    emit(product, form, "numberField", 1, "change", {{"value", 45}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product).size() == 1U; }));
    // clang-format on
    EXPECT_EQ(profiles(product)[0]["root"], "C:/project");
    EXPECT_FALSE(profiles(product)[0].contains("platform"));
    EXPECT_EQ(profiles(product)[0]["port"], 2222);
    EXPECT_EQ(profiles(product)[0]["timeoutSeconds"], 45);
}

TEST_F(RemoteSshPluginTest, EditsRemovesAndCancelsProfilesThroughTheirActualActions) {
    HeadlessProduct product(data());
    boot(product);
    std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Original"}});
    emit(product, form, "textField", 1, "change", {{"value", "host"}});
    emit(product, form, "pathField", 2, "change", {{"value", "/project"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product).size() == 1U; }));
    // clang-format on
    const std::string id = profiles(product)[0]["id"];
    emit(product, page, "table", 0, "activate", {{"id", id}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !dialog(product).empty(); }));
    // clang-format on
    form = dialog(product);
    emit(product, form, "textField", 0, "change", {{"value", "Renamed"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product)[0]["name"] == "Renamed"; }));
    // clang-format on
    EXPECT_EQ(profiles(product).size(), 1U);
    EXPECT_EQ(profiles(product)[0]["id"], id);
    emit(product, page, "table", 0, "action", {{"id", id}, {"action", "remove"}});
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return profiles(product).empty(); }));
    // clang-format on
    form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Cancelled"}});
    ASSERT_TRUE(product.answerDialog("cancel"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty(); }));
    // clang-format on
    EXPECT_TRUE(profiles(product).empty());
}

TEST_F(RemoteSshPluginTest, ReportsItsProfileLimitAndStillAllowsAnExistingProfileToBeEdited) {
    nlohmann::json saved = nlohmann::json::array();

    for (int index = 0; index < 64; ++index) {
        saved.push_back({{"id", "profile-" + std::to_string(index)}, {"name", "Development " + std::to_string(index)}, {"host", "host"}, {"user", ""}, {"port", 0}, {"identity", ""}, {"configuration", ""}, {"root", "/project"}, {"timeoutSeconds", 30}});
    }

    storeProfiles(saved);
    HeadlessProduct product(data());
    boot(product);
    std::string form = newProfile(product);
    emit(product, form, "textField", 0, "change", {{"value", "Overflow"}});
    emit(product, form, "textField", 1, "change", {{"value", "host"}});
    emit(product, form, "pathField", 2, "change", {{"value", "/project"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("remote-ssh.error.limit"); }));
    // clang-format on
    EXPECT_EQ(profiles(product), saved);
    ASSERT_TRUE(product.answerDialog("cancel"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty(); }));
    // clang-format on
    emit(product, page, "table", 0, "action", {{"id", "profile-0"}, {"action", "edit"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !dialog(product).empty(); }));
    // clang-format on
    form = dialog(product);
    emit(product, form, "textField", 0, "change", {{"value", "Updated"}});
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product).empty() && profiles(product)[0]["name"] == "Updated"; }));
    // clang-format on
    EXPECT_EQ(profiles(product).size(), 64U);
}

TEST_F(RemoteSshPluginTest, RemovesAProfileAfterItsFailedConnectionHasReleasedItsResources) {
    storeProfiles(nlohmann::json::array({{{"id", "development"}, {"name", "Development"}, {"host", "host"}, {"user", ""}, {"port", 0}, {"identity", ""}, {"configuration", ""}, {"root", "/project"}, {"timeoutSeconds", 30}, {"savePassword", false}}}));
    HeadlessProduct product(data());
    boot(product);
    emit(product, page, "table", 0, "action", {{"id", "development"}, {"action", "connect"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() {
        const SurfaceReader reader(product.declared(page));
        const auto rows = reader.properties(reader.nodes("table")[0])["rows"];

        return rows[0]["cells"][4] == "Failed";
    }));
    // clang-format on

    emit(product, page, "table", 0, "action", {{"id", "development"}, {"action", "remove"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.answerDialog("confirm"); }));
    ASSERT_TRUE(product.frameUntil([&]() { return profiles(product).empty(); }));
    // clang-format on
}

TEST_F(RemoteSshPluginTest, CancelsPreparationThroughTheVisibleConnectionAction) {
    const nlohmann::json saved = nlohmann::json::array({{{"id", "development"}, {"name", "Development"}, {"host", "host"}, {"user", ""}, {"port", 0}, {"identity", ""}, {"configuration", ""}, {"root", "/project"}, {"timeoutSeconds", 30}}});
    storeProfiles(saved);
    HeadlessProduct product(data());
    product.processes().executables["ssh"] = RootedPath::of("usr/bin/ssh");
    boot(product);
    emit(product, page, "table", 0, "action", {{"id", "development"}, {"action", "connect"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() {
        const SurfaceReader reader(product.declared(page));
        const auto rows = reader.properties(reader.nodes("table")[0])["rows"];

        return rows[0]["actions"][0]["id"] == "disconnect";
    }));
    // clang-format on

    const SurfaceReader preparing(product.declared(page));
    const auto actions = preparing.properties(preparing.nodes("table")[0])["rows"][0]["actions"];
    EXPECT_EQ(actions.size(), 1U);
    emit(product, page, "table", 0, "action", {{"id", "development"}, {"action", "edit"}});
    product.frame();
    EXPECT_TRUE(dialog(product).empty());
    emit(product, page, "table", 0, "action", {{"id", "development"}, {"action", "disconnect"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() {
        const SurfaceReader reader(product.declared(page));
        const auto rows = reader.properties(reader.nodes("table")[0])["rows"];

        return rows[0]["cells"][4] == "Disconnected";
    }));
    // clang-format on

    EXPECT_EQ(profiles(product), saved);
    const SurfaceReader disconnected(product.declared(page));
    EXPECT_EQ(disconnected.properties(disconnected.nodes("table")[0])["rows"][0]["actions"].size(), 3U);
    product.stop();
}

} // namespace workpane::tests
