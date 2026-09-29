#pragma once

#include "Error.h"
#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "platform/SystemServices.h"
#include "ui/Fonts.h"

#include <imgui.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The monospaced families installed on the machine, listed and read on a worker, so choosing one never blocks the frame that draws it.
// A family is drawn in the bundled face until its file was read, and for good when it is not installed or cannot be read, which is reported once.
class FontFamilies final {
  public:
    static constexpr std::size_t longestName{256};

    using ChangeHandler = std::function<void()>;
    using ListHandler = std::function<void(const std::vector<std::string>& families)>;
    using FailureHandler = std::function<void(const Error& error)>;

    FontFamilies(Fonts& fonts, ImFontAtlas& atlas, platform::SystemServices& system, execution::WorkerPool& workers, execution::MainThreadQueue& mainThread);

    FontFamilies(const FontFamilies&) = delete;
    FontFamilies& operator=(const FontFamilies&) = delete;

    void setChangeHandler(ChangeHandler handler);
    void setFailureHandler(FailureHandler handler);
    void list(ListHandler handler);
    void request(std::string_view family);

  private:
    void enumerate();
    void listed(std::vector<platform::InstalledFont> fonts);
    void read(const std::string& family);
    void finish(const std::string& family, std::vector<unsigned char> data);
    void fail(const Error& error) const;

    Fonts& m_fonts;
    ImFontAtlas& m_atlas;
    platform::SystemServices& m_system;
    execution::WorkerPool& m_workers;
    execution::MainThreadQueue& m_mainThread;
    std::optional<std::map<std::string, std::filesystem::path, std::less<>>> m_installed;
    bool m_listing{false};
    std::vector<ListHandler> m_waiting;
    std::set<std::string, std::less<>> m_requested;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    ChangeHandler m_changed;
    FailureHandler m_failed;
};

} // namespace workpane::ui
