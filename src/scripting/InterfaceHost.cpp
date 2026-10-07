#include "scripting/InterfaceHost.h"

#include "localization/Localization.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"
#include "ui/IconCatalog.h"
#include "ui/WidgetHelper.h"
#include "ui/model/RenderContext.h"
#include "ui/model/SurfaceStore.h"
#include "ui/model/TextValue.h"
#include "ui/shell/DialogAnswer.h"
#include "ui/shell/DialogButton.h"
#include "ui/shell/DialogHost.h"
#include "ui/shell/DialogRequest.h"
#include "ui/shell/Shell.h"
#include "ui/theme/FontRole.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::scripting {

ui::NodeId InterfaceHost::node(json::ObjectReader& reader) {
    std::int64_t node = 0;
    reader.readInteger("node", node, 1, largestNode);

    return static_cast<ui::NodeId>(node);
}

InterfaceHost::InterfaceHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies) {}

Result<void> InterfaceHost::registerFunctions() {
    // clang-format off
    const std::vector<std::tuple<std::string, ScriptRuntime::Effect, ScriptRuntime::HostFunction>> functions{
        {"workpane_ui_mount", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return mount(argument); }},
        {"workpane_ui_patch", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return patch(argument); }},
        {"workpane_ui_children", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return children(argument); }},
        {"workpane_ui_command", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return command(argument); }},
        {"workpane_ui_unmount", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return unmount(argument); }},
        {"workpane_ui_failed", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return failed(argument); }},
        {"workpane_ui_measure", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return measure(argument); }},
        {"workpane_navigate", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return navigate(argument); }},
        {"workpane_dialog_open", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return dialog(argument); }},
        {"workpane_dialog_close", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return closeDialog(argument); }},
        {"workpane_dialog_buttons", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return dialogButtons(argument); }},
    };
    // clang-format on

    for (const auto& [name, effect, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, effect, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

// A surface belongs to the plugin whose identity it carries, so no plugin can declare a surface for another one.
bool InterfaceHost::ownsSurface(std::string_view owner, std::string_view surface) {
    for (const std::string_view kind : {"view:", "band:", "settings:", "dialog:"}) {
        const std::string prefix = std::string(kind) + std::string(owner) + ":";

        if (surface.starts_with(prefix) && surface.size() > prefix.size()) {
            return true;
        }
    }

    return false;
}

Result<void> InterfaceHost::owns(std::string_view owner, std::string_view surface) const {
    if (const auto known = HostOwners::check(m_services.plugins, owner); !known.hasValue()) {
        return known;
    }

    if (!ownsSurface(owner, surface)) {
        return Result<void>::failure({"ui_surface_foreign", "A surface identity names another owner", std::string(surface)});
    }

    return Result<void>::success();
}

nlohmann::json InterfaceHost::mount(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    const json::Json* tree = nullptr;
    json::ObjectReader reader(argument, "ui.mount");
    reader.readText("plugin", plugin).readText("surface", surface).readObject("tree", tree);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto owned = owns(plugin, surface); !owned.hasValue()) {
        return HostReply::failure(owned.error());
    }

    const auto mounted = m_services.surfaces.mount(surface, plugin, assets(plugin), *tree, m_services.render);
    return mounted.hasValue() ? HostReply::success() : HostReply::failure(mounted.error());
}

nlohmann::json InterfaceHost::patch(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    const json::Json* properties = nullptr;
    json::ObjectReader reader(argument, "ui.patch");
    reader.readText("plugin", plugin).readText("surface", surface).readObject("props", properties);
    const auto node = InterfaceHost::node(reader);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto patched = m_services.surfaces.patch(surface, plugin, node, *properties);
    return patched.hasValue() ? HostReply::success() : HostReply::failure(patched.error());
}

nlohmann::json InterfaceHost::children(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    const json::Json* nodes = nullptr;
    const json::Json* properties = &json::ObjectReader::emptyObject();
    json::ObjectReader reader(argument, "ui.children");
    reader.readText("plugin", plugin).readText("surface", surface).readArray("children", nodes).readObject("props", properties, json::Presence::Optional);
    const auto node = InterfaceHost::node(reader);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto replaced = m_services.surfaces.replaceChildren(surface, plugin, node, *nodes, *properties, m_services.render);
    return replaced.hasValue() ? HostReply::success() : HostReply::failure(replaced.error());
}

nlohmann::json InterfaceHost::command(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    std::string name;
    const json::Json* arguments = &json::ObjectReader::emptyObject();
    json::ObjectReader reader(argument, "ui.command");
    reader.readText("plugin", plugin).readText("surface", surface).readText("command", name).readObject("arguments", arguments, json::Presence::Optional);
    const auto node = InterfaceHost::node(reader);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto executed = m_services.surfaces.command(surface, plugin, node, name, *arguments, m_services.render);
    return executed.hasValue() ? HostReply::success() : HostReply::failure(executed.error());
}

nlohmann::json InterfaceHost::unmount(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    json::ObjectReader reader(argument, "ui.unmount");
    reader.readText("plugin", plugin).readText("surface", surface);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto removed = m_services.surfaces.unmount(surface, plugin, m_services.render);
    return removed.hasValue() ? HostReply::success() : HostReply::failure(removed.error());
}

// A view that could not be built is replaced by the translated failure surface instead of an empty area nobody can explain.
nlohmann::json InterfaceHost::failed(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    json::ObjectReader reader(argument, "ui.failed");
    reader.readText("plugin", plugin).readText("surface", surface);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto owned = owns(plugin, surface); !owned.hasValue()) {
        return HostReply::failure(owned.error());
    }

    m_services.shell.markFailed(surface);

    return HostReply::success();
}

// A plugin reveals only a destination it declares itself, so a request answered by one plugin never moves the shell to another one.
nlohmann::json InterfaceHost::navigate(const nlohmann::json& argument) {
    std::string plugin;
    std::string item;
    json::ObjectReader reader(argument, "navigate");
    reader.readText("plugin", plugin).readText("item", item);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto moved = m_services.shell.navigate(plugin + ":" + item);
    return moved.hasValue() ? HostReply::success() : HostReply::failure(moved.error());
}

// A text is measured in points in the face and the size a canvas draws it with, in the language of the reader, so a plugin lays out what it draws around its words.
nlohmann::json InterfaceHost::measure(const nlohmann::json& argument) const {
    std::string plugin;
    const json::Json* text = &json::ObjectReader::absent();
    double size = smallestText;
    ui::FontFace face = ui::FontFace::Regular;
    json::ObjectReader reader(argument, "measure");
    reader.readText("plugin", plugin).readAny("text", text).readNumber("size", size, smallestText, largestText);
    reader.readChoice("face", face, {{"regular", ui::FontFace::Regular}, {"semibold", ui::FontFace::SemiBold}, {"monospace", ui::FontFace::Monospace}}, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    auto parsed = ui::TextValue::parse(*text, "measure.text");

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const ui::RenderContext& context = m_services.render;

    if (ImGui::GetCurrentContext() == nullptr || context.fonts().face(face) == nullptr) {
        return HostReply::failure({"ui_measure_unavailable", "Text is measured only once the window has its fonts", plugin});
    }

    const ui::FontRole role{face, static_cast<float>(size)};
    return HostReply::success(ui::WidgetHelper::textSize(context, role, context.text(parsed.value())).x / context.scale());
}

nlohmann::json InterfaceHost::dialog(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    ui::DialogRequest dialog;
    const json::Json* buttons = &json::ObjectReader::emptyList();
    double width = dialog.width;
    json::ObjectReader reader(argument, "dialog");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestNode);
    reader.readChoice("kind", dialog.kind, {{"confirm", ui::DialogKind::Confirm}, {"alert", ui::DialogKind::Alert}, {"prompt", ui::DialogKind::Prompt}, {"custom", ui::DialogKind::Custom}});
    reader.readText("title", dialog.title).read("message", dialog.message, json::Presence::Optional).read("detail", dialog.detail, json::Presence::Optional);
    reader.read("confirmText", dialog.confirmText, json::Presence::Optional).read("cancelText", dialog.cancelText, json::Presence::Optional).read("destructive", dialog.destructive, json::Presence::Optional);
    reader.read("value", dialog.value, json::Presence::Optional).read("placeholder", dialog.placeholder, json::Presence::Optional).read("surface", dialog.surface, json::Presence::Optional);
    reader.readArray("buttons", buttons, json::Presence::Optional).readNumber("width", width, smallestDialog, largestDialog, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    auto parsed = InterfaceHost::buttons(*buttons);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    dialog.buttons = std::move(parsed.value());

    // A custom dialog carries a surface its plugin mounted and may carry buttons, and every other kind carries neither.
    const bool custom = dialog.kind == ui::DialogKind::Custom;

    if (custom != !dialog.surface.empty() || (!custom && !dialog.buttons.empty()) || (custom && (!ownsSurface(plugin, dialog.surface) || !m_services.surfaces.contains(dialog.surface)))) {
        return HostReply::failure({"dialog_request_invalid", "A custom dialog needs its own mounted surface, and no other dialog carries a surface or buttons", dialog.surface});
    }

    const auto& localization = m_services.localization;
    dialog.cancelText = dialog.cancelText.empty() ? localization.translate("workpane.actions.cancel") : dialog.cancelText;
    dialog.confirmText = dialog.confirmText.empty() ? localization.translate(dialog.kind == ui::DialogKind::Alert ? "workpane.actions.ok" : "workpane.actions.confirm") : dialog.confirmText;
    dialog.width = static_cast<float>(width);
    dialog.owner = plugin;
    // clang-format off
    dialog.answer = [this, request](const ui::DialogAnswer& answer) { m_replies.reply(request, Result<nlohmann::json>::success({{"button", answer.button}, {"value", answer.value}})); };
    // clang-format on
    m_services.shell.dialogs().open(std::move(dialog));

    return HostReply::success();
}

// A dialog button carries an identity, a text and the optional variant, icon, enabled state and closes flag, and a button that does not close reports its press to the content of its dialog.
Result<std::vector<ui::DialogButton>> InterfaceHost::buttons(const json::Json& entries) {
    std::vector<ui::DialogButton> parsed;

    for (const auto& entry : entries) {
        ui::DialogButton button;
        std::string icon;
        json::ObjectReader reader(entry, "dialog.buttons");
        reader.readText("id", button.id).readText("text", button.text).readChoice("variant", button.variant, {{"default", ui::ButtonVariant::Default}, {"primary", ui::ButtonVariant::Primary}, {"destructive", ui::ButtonVariant::Destructive}}, json::Presence::Optional);
        reader.read("icon", icon, json::Presence::Optional).read("closes", button.closes, json::Presence::Optional).read("enabled", button.enabled, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<ui::DialogButton>>::failure(finished.error());
        }

        auto glyph = ui::IconCatalog::parseOptional(icon);

        if (!glyph.hasValue()) {
            return Result<std::vector<ui::DialogButton>>::failure(glyph.error());
        }

        button.icon = glyph.value();
        parsed.push_back(std::move(button));
    }

    return Result<std::vector<ui::DialogButton>>::success(std::move(parsed));
}

// The buttons of an open custom dialog follow the state of what it shows, such as a server that starts or stops while its form is open.
nlohmann::json InterfaceHost::dialogButtons(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    const json::Json* entries = nullptr;
    json::ObjectReader reader(argument, "dialog.buttons");
    reader.readText("plugin", plugin).readText("surface", surface).readArray("buttons", entries);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto owned = owns(plugin, surface); !owned.hasValue()) {
        return HostReply::failure(owned.error());
    }

    auto parsed = buttons(*entries);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    m_services.shell.dialogs().replaceButtons(surface, std::move(parsed.value()));

    return HostReply::success();
}

std::filesystem::path InterfaceHost::assets(std::string_view owner) const {
    if (owner == localization::Localization::coreOwner) {
        return m_services.info.resources / "images";
    }

    return m_services.plugins.find(owner)->directory / "assets";
}

nlohmann::json InterfaceHost::closeDialog(const nlohmann::json& argument) {
    std::string plugin;
    std::string surface;
    std::string button;
    json::ObjectReader reader(argument, "dialog.close");
    reader.readText("plugin", plugin).readText("surface", surface).readText("button", button);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto owned = owns(plugin, surface); !owned.hasValue()) {
        return HostReply::failure(owned.error());
    }

    m_services.shell.dialogs().dismiss(surface, button);

    return HostReply::success();
}

} // namespace workpane::scripting
