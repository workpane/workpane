#include "scripting/SystemHost.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "platform/PathText.h"
#include "platform/SystemInspector.h"
#include "platform/SystemServices.h"
#include "platform/UrlPolicy.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"
#include "time/Timestamps.h"
#include "ui/FontFamilies.h"
#include "ui/PseudoTerminalHost.h"
#include "ui/model/RenderContext.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace workpane::scripting {

Result<std::vector<platform::FileFilter>> SystemHost::filters(const json::Json& values) {
    std::vector<platform::FileFilter> parsed;

    for (const auto& value : values) {
        platform::FileFilter filter;
        json::ObjectReader reader(value, "dialog.filters");
        reader.readText("name", filter.name).read("patterns", filter.patterns);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<platform::FileFilter>>::failure(finished.error());
        }

        if (filter.patterns.empty()) {
            return Result<std::vector<platform::FileFilter>>::failure({"dialog_filter_invalid", "A file filter names at least one pattern", filter.name});
        }

        parsed.push_back(std::move(filter));
    }

    return Result<std::vector<platform::FileFilter>>::success(std::move(parsed));
}

std::optional<platform::MessageKind> SystemHost::kind(std::string_view name) {
    if (name == "information") {
        return platform::MessageKind::Information;
    }

    if (name == "warning") {
        return platform::MessageKind::Warning;
    }

    if (name == "error") {
        return platform::MessageKind::Error;
    }

    if (name == "question") {
        return platform::MessageKind::Question;
    }

    return std::nullopt;
}

SystemHost::SystemHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies) {}

Result<void> SystemHost::registerFunctions() {
    // clang-format off
    const std::vector<std::pair<std::string, ScriptRuntime::HostFunction>> functions{
        {"workpane_open_url", [this](const nlohmann::json& argument) { return openUrl(argument); }},
        {"workpane_reveal_path", [this](const nlohmann::json& argument) { return revealPath(argument); }},
        {"workpane_native_open", [this](const nlohmann::json& argument) { return openFiles(argument); }},
        {"workpane_native_folder", [this](const nlohmann::json& argument) { return selectFolder(argument); }},
        {"workpane_native_save", [this](const nlohmann::json& argument) { return saveFile(argument); }},
        {"workpane_native_message", [this](const nlohmann::json& argument) { return message(argument); }},
        {"workpane_native_notify", [this](const nlohmann::json& argument) { return notify(argument); }},
        {"workpane_clipboard_write", [this](const nlohmann::json& argument) { return writeClipboard(argument); }},
        {"workpane_clipboard_read", [this](const nlohmann::json& argument) { return readClipboard(argument); }},
        {"workpane_system_information", [this](const nlohmann::json& argument) { return information(argument); }},
        {"workpane_system_home", [this](const nlohmann::json& argument) { return home(argument); }},
        {"workpane_system_shell", [this](const nlohmann::json& argument) { return shell(argument); }},
        {"workpane_system_monospace_fonts", [this](const nlohmann::json& argument) { return monospaceFonts(argument); }},
    };
    // clang-format on

    for (const auto& [name, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, ScriptRuntime::Effect::Background, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

// The home directory is where a new terminal or a folder picker starts when nothing else names a place.
nlohmann::json SystemHost::home(const nlohmann::json& argument) const {
    std::string plugin;
    json::ObjectReader reader(argument, "system.home");
    reader.readText("plugin", plugin);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    return HostReply::success({{"path", platform::PathText::generic(m_services.system.home())}});
}

// The shell a terminal component starts by default is answered with its path and the name the reader knows it by, such as zsh or pwsh, which is what a new terminal is called.
nlohmann::json SystemHost::shell(const nlohmann::json& argument) const {
    std::string plugin;
    json::ObjectReader reader(argument, "system.shell");
    reader.readText("plugin", plugin);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path program = m_services.render.terminals().shellProgram();
    return HostReply::success({{"name", platform::PathText::utf8(program.stem())}, {"path", platform::PathText::generic(program)}});
}

// The platform may take its time handing an address to the browser, so it runs on a worker and answers the request when it is done.
nlohmann::json SystemHost::openUrl(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string url;
    json::ObjectReader reader(argument, "system.open-url");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest).readText("url", url);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    if (!platform::UrlPolicy::allowed(url)) {
        return HostReply::failure({"system_url_refused", "Only web addresses open in the default browser", url});
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, request, url, system = &m_services.system, mainThread = &m_services.mainThread]() {
        auto opened = system->openUrl(url);
        mainThread->post([this, alive, request, opened = std::move(opened)]() {
            if (alive.expired()) {
                return;
            }

            m_replies.reply(request, opened.hasValue() ? Result<nlohmann::json>::success(nullptr) : Result<nlohmann::json>::failure(opened.error()));
        });
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json SystemHost::revealPath(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    json::ObjectReader reader(argument, "system.reveal-path");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest).readText("path", path);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path target(std::u8string(path.begin(), path.end()));

    if (!target.is_absolute()) {
        return HostReply::failure({"system_path_invalid", "A path must be absolute", path});
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, request, target, system = &m_services.system, mainThread = &m_services.mainThread]() {
        auto revealed = system->revealPath(target);
        mainThread->post([this, alive, request, revealed = std::move(revealed)]() {
            if (alive.expired()) {
                return;
            }

            m_replies.reply(request, revealed.hasValue() ? Result<nlohmann::json>::success(nullptr) : Result<nlohmann::json>::failure(revealed.error()));
        });
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json SystemHost::openFiles(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string title;
    std::string initial;
    const json::Json* filters = &json::ObjectReader::emptyList();
    bool multiple = false;
    json::ObjectReader reader(argument, "native.open");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest).readText("title", title).read("initial", initial, json::Presence::Optional).readArray("filters", filters, json::Presence::Optional).read("multiple", multiple, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    auto parsed = SystemHost::filters(*filters);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.dialogs.openFiles(title, initial, std::move(parsed.value()), multiple, [this, alive, request](Result<std::vector<std::string>> paths) {
        if (!alive.expired()) {
            answerPaths(request, std::move(paths));
        }
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json SystemHost::selectFolder(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string title;
    std::string initial;
    json::ObjectReader reader(argument, "native.folder");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest).readText("title", title).read("initial", initial, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.dialogs.selectFolder(title, initial, [this, alive, request](Result<std::vector<std::string>> paths) {
        if (!alive.expired()) {
            answerPaths(request, std::move(paths));
        }
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json SystemHost::saveFile(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string title;
    std::string initial;
    const json::Json* filters = &json::ObjectReader::emptyList();
    json::ObjectReader reader(argument, "native.save");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest).readText("title", title).read("initial", initial, json::Presence::Optional).readArray("filters", filters, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    auto parsed = SystemHost::filters(*filters);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.dialogs.saveFile(title, initial, std::move(parsed.value()), [this, alive, request](Result<std::vector<std::string>> paths) {
        if (!alive.expired()) {
            answerPaths(request, std::move(paths));
        }
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json SystemHost::message(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string title;
    std::string text;
    std::string kind = "information";
    platform::MessageButtons buttons = platform::MessageButtons::Ok;
    json::ObjectReader reader(argument, "native.message");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest).readText("title", title).readText("message", text).read("kind", kind, json::Presence::Optional);
    reader.readChoice("buttons", buttons, {{"ok", platform::MessageButtons::Ok}, {"ok-cancel", platform::MessageButtons::OkCancel}, {"yes-no", platform::MessageButtons::YesNo}, {"yes-no-cancel", platform::MessageButtons::YesNoCancel}}, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto parsed = SystemHost::kind(kind);

    if (!parsed.has_value()) {
        return HostReply::failure({"dialog_kind_invalid", "A message kind is information, warning, error or question", kind});
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.dialogs.message(title, text, *parsed, buttons, [this, alive, request](Result<std::string> button) {
        if (alive.expired()) {
            return;
        }

        m_replies.reply(request, button.hasValue() ? Result<nlohmann::json>::success({{"button", button.value()}}) : Result<nlohmann::json>::failure(button.error()));
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json SystemHost::notify(const nlohmann::json& argument) {
    std::string plugin;
    std::string title;
    std::string text;
    std::string kind = "information";
    json::ObjectReader reader(argument, "native.notify");
    reader.readText("plugin", plugin).readText("title", title).read("message", text, json::Presence::Optional).read("kind", kind, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto parsed = SystemHost::kind(kind);

    if (!parsed.has_value()) {
        return HostReply::failure({"dialog_kind_invalid", "A message kind is information, warning, error or question", kind});
    }

    const auto notified = m_services.dialogs.notify(title, text, *parsed);
    return notified.hasValue() ? HostReply::success() : HostReply::failure(notified.error());
}

// The clipboard is reached through ImGui, whose platform backend is the one window system every text field of the product already copies through.
nlohmann::json SystemHost::writeClipboard(const nlohmann::json& argument) {
    std::string plugin;
    std::string text;
    json::ObjectReader reader(argument, "clipboard.write");
    reader.readText("plugin", plugin).read("text", text);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    ImGui::SetClipboardText(text.c_str());

    return HostReply::success();
}

nlohmann::json SystemHost::readClipboard(const nlohmann::json& argument) {
    std::string plugin;
    json::ObjectReader reader(argument, "clipboard.read");
    reader.readText("plugin", plugin);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const char* text = ImGui::GetClipboardText();
    return HostReply::success({{"text", text == nullptr ? std::string() : std::string(text)}});
}

void SystemHost::answerPaths(std::int64_t request, Result<std::vector<std::string>> paths) {
    if (!paths.hasValue()) {
        m_replies.reply(request, Result<nlohmann::json>::failure(paths.error()));
        return;
    }

    m_replies.reply(request, Result<nlohmann::json>::success({{"paths", paths.value()}}));
}

// The hardware is read on a worker, because the utilization sample alone takes a fifth of a second, and the displays are added on the interface thread that owns them.
nlohmann::json SystemHost::information(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    json::ObjectReader reader(argument, "system.information");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, request, inspector = &m_services.inspector, mainThread = &m_services.mainThread]() {
        auto snapshot = inspector->inspect();
        mainThread->post([this, alive, request, snapshot = std::move(snapshot)]() mutable {
            if (alive.expired()) {
                return;
            }

            if (!snapshot.hasValue()) {
                m_replies.reply(request, Result<nlohmann::json>::failure(snapshot.error()));
                return;
            }

            snapshot.value()["displays"] = m_services.displays();
            snapshot.value()["capturedAt"] = time::Timestamps::storedTimestamp(time::Timestamps::now());
            m_replies.reply(request, Result<nlohmann::json>::success(std::move(snapshot.value())));
        });
    });
    // clang-format on

    return HostReply::success();
}

// The monospaced families of the machine are answered by name in alphabetical order, after the frame that asked, even when they were listed before.
nlohmann::json SystemHost::monospaceFonts(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    json::ObjectReader reader(argument, "system.monospaceFonts");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestSystemRequest);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.render.families().list([this, alive, request, mainThread = &m_services.mainThread](const std::vector<std::string>& families) {
        mainThread->post([this, alive, request, families]() {
            if (alive.expired()) {
                return;
            }

            m_replies.reply(request, Result<nlohmann::json>::success({{"families", families}}));
        });
    });
    // clang-format on

    return HostReply::success();
}

} // namespace workpane::scripting
