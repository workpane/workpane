#include "ui/IconCatalog.h"

#include "ui/FontScope.h"
#include "ui/LucideGlyphs.h"
#include "ui/WidgetHelper.h"

namespace workpane::ui {

const std::vector<IconCatalog::Entry>& IconCatalog::table() {
    static const std::vector<Entry> entries{{Icon::Add, "add"}, {Icon::Terminal, "terminal"}, {Icon::Layout, "layout"}, {Icon::Search, "search"}, {Icon::Settings, "settings"}, {Icon::Close, "close"}, {Icon::More, "more"}, {Icon::Focus, "focus"}, {Icon::Restore, "restore"}, {Icon::WebServer, "web-server"}, {Icon::Browser, "browser"}, {Icon::Start, "start"}, {Icon::Stop, "stop"}, {Icon::Back, "back"}, {Icon::Forward, "forward"}, {Icon::Home, "home"}, {Icon::Shelf, "shelf"}, {Icon::Bookmark, "bookmark"}, {Icon::Folder, "folder"}, {Icon::File, "file"}, {Icon::Edit, "edit"}, {Icon::Refresh, "refresh"}, {Icon::Clear, "clear"}, {Icon::Import, "import"}, {Icon::Export, "export"}, {Icon::ExternalLink, "external-link"}, {Icon::Logs, "logs"}, {Icon::Tasks, "tasks"}, {Icon::Workspace, "workspace"}, {Icon::Donate, "donate"}, {Icon::System, "system"}, {Icon::Processor, "processor"}, {Icon::Memory, "memory"}, {Icon::Graphics, "graphics"}, {Icon::Mainboard, "mainboard"}, {Icon::Storage, "storage"}, {Icon::Battery, "battery"}, {Icon::Network, "network"}, {Icon::Information, "information"}, {Icon::Success, "success"}, {Icon::Warning, "warning"}, {Icon::Error, "error"}, {Icon::Visible, "visible"}, {Icon::Hidden, "hidden"}, {Icon::Minus, "minus"}, {Icon::Schedule, "schedule"}, {Icon::Bell, "bell"}, {Icon::Spark, "spark"}, {Icon::Person, "person"}, {Icon::Tool, "tool"}, {Icon::Chat, "chat"}, {Icon::Components, "components"}, {Icon::CaseSensitive, "case-sensitive"}, {Icon::WholeWord, "whole-word"}, {Icon::Attach, "attach"}, {Icon::Image, "image"}, {Icon::Audio, "audio"}, {Icon::Document, "document"}, {Icon::Module, "module"}, {Icon::Namespace, "namespace"}, {Icon::Class, "class"}, {Icon::Structure, "structure"}, {Icon::Interface, "interface"}, {Icon::Enumeration, "enumeration"}, {Icon::Enumerator, "enumerator"}, {Icon::Function, "function"}, {Icon::Field, "field"}, {Icon::Variable, "variable"}, {Icon::Constant, "constant"}, {Icon::String, "string"}, {Icon::Number, "number"}, {Icon::Boolean, "boolean"}, {Icon::Array, "array"}, {Icon::Object, "object"}, {Icon::Key, "key"}, {Icon::Null, "null"}, {Icon::Event, "event"}, {Icon::Operator, "operator"}, {Icon::TypeParameter, "type-parameter"}};
    return entries;
}

std::vector<std::string_view> IconCatalog::productNames() {
    std::vector<std::string_view> names;

    for (const Entry& entry : table()) {
        names.push_back(entry.name);
    }

    return names;
}

std::optional<Icon> IconCatalog::parse(std::string_view name) {
    for (const Entry& entry : table()) {
        if (entry.name == name) {
            return entry.icon;
        }
    }

    const auto glyph = LucideGlyphs::find(name);

    if (!glyph.has_value()) {
        return std::nullopt;
    }

    return Icon{*glyph};
}

// An empty name declares no icon, and any other name must reach a glyph of the face.
Result<std::optional<Icon>> IconCatalog::parseOptional(const std::string& name) {
    if (name.empty()) {
        return Result<std::optional<Icon>>::success(std::nullopt);
    }

    const auto parsed = parse(name);

    if (!parsed.has_value()) {
        return Result<std::optional<Icon>>::failure({"ui_icon_unknown", "An icon names neither an icon of the product nor a glyph of the Lucide face", name});
    }

    return Result<std::optional<Icon>>::success(parsed);
}

// The em square of a Lucide glyph is the square the caller gives, and the face is pushed so it is rasterized at the density of the framebuffer.
void IconCatalog::draw(const Fonts& fonts, ImDrawList& list, Icon icon, ImVec2 topLeft, float size, Color color) {
    const FontScope scope(fonts, FontFace::Icon, size / ImGui::GetStyle().FontScaleDpi);
    fonts.face(FontFace::Icon)->RenderChar(&list, fonts.size(FontFace::Icon, size), topLeft, WidgetHelper::ink(color), icon.glyph);
}

} // namespace workpane::ui
