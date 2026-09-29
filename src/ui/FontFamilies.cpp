#include "ui/FontFamilies.h"

#include <fstream>
#include <iterator>
#include <utility>

namespace workpane::ui {

FontFamilies::FontFamilies(Fonts& fonts, ImFontAtlas& atlas, platform::SystemServices& system, execution::WorkerPool& workers, execution::MainThreadQueue& mainThread) : m_fonts(fonts), m_atlas(atlas), m_system(system), m_workers(workers), m_mainThread(mainThread) {}

void FontFamilies::setChangeHandler(ChangeHandler handler) {
    m_changed = std::move(handler);
}

void FontFamilies::setFailureHandler(FailureHandler handler) {
    m_failed = std::move(handler);
}

// The families are answered in the order of their names once the machine was asked, which happens only the first time anybody needs them.
void FontFamilies::list(ListHandler handler) {
    if (m_installed.has_value()) {
        std::vector<std::string> families;

        for (const auto& [family, file] : *m_installed) {
            families.push_back(family);
        }

        handler(families);
        return;
    }

    m_waiting.push_back(std::move(handler));
    enumerate();
}

// A family is read once, the first time a component names it, and naming the bundled family asks for nothing.
void FontFamilies::request(std::string_view family) {
    if (family == Fonts::bundledMonospace || m_requested.contains(family)) {
        return;
    }

    m_requested.emplace(family);

    if (!m_installed.has_value()) {
        enumerate();
        return;
    }

    read(std::string(family));
}

void FontFamilies::enumerate() {
    if (m_listing) {
        return;
    }

    m_listing = true;
    const std::weak_ptr<bool> alive = m_alive;

    // clang-format off
    m_workers.post([this, alive, system = &m_system, mainThread = &m_mainThread]() {
        auto fonts = system->monospaceFonts();
        mainThread->post([this, alive, fonts = std::move(fonts)]() mutable {
            if (alive.expired()) {
                return;
            }

            listed(std::move(fonts));
        });
    });
    // clang-format on
}

// Handlers that waited for the list hear it, and the families named while the machine was asked are read now.
void FontFamilies::listed(std::vector<platform::InstalledFont> fonts) {
    m_installed.emplace();

    for (auto& font : fonts) {
        m_installed->emplace(std::move(font.family), std::move(font.file));
    }

    for (const auto& handler : std::exchange(m_waiting, {})) {
        list(handler);
    }

    for (const auto& family : m_requested) {
        read(family);
    }
}

void FontFamilies::read(const std::string& family) {
    const auto found = m_installed->find(family);

    if (found == m_installed->end()) {
        fail({"font_family_missing", "The font family is not installed", family});
        return;
    }

    const std::weak_ptr<bool> alive = m_alive;

    // clang-format off
    m_workers.post([this, alive, family, file = found->second, mainThread = &m_mainThread]() {
        std::ifstream stream(file, std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        mainThread->post([this, alive, family, data = std::move(data)]() mutable {
            if (alive.expired()) {
                return;
            }

            finish(family, std::move(data));
        });
    });
    // clang-format on
}

// A file that cannot be read leaves the family in the bundled face, and one that joins the atlas is drawn from the next frame.
void FontFamilies::finish(const std::string& family, std::vector<unsigned char> data) {
    if (data.empty()) {
        fail({"font_unreadable", "The file of the font family could not be read", family});
        return;
    }

    if (const auto adopted = m_fonts.adopt(m_atlas, family, std::move(data)); !adopted.hasValue()) {
        fail(adopted.error());
        return;
    }

    if (m_changed) {
        m_changed();
    }
}

void FontFamilies::fail(const Error& error) const {
    if (m_failed) {
        m_failed(error);
    }
}

} // namespace workpane::ui
