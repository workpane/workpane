#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace workpane::tests {

// The native test starts on a private desktop shared by all its threads and restores the reader's desktop on exit.
class WindowsDesktop final {
  public:
    WindowsDesktop();
    ~WindowsDesktop();

    WindowsDesktop(const WindowsDesktop&) = delete;
    WindowsDesktop& operator=(const WindowsDesktop&) = delete;

    [[nodiscard]] int run(int argc, char** argv);

  private:
    using Desktop = std::unique_ptr<std::remove_pointer_t<HDESK>, decltype(&CloseDesktop)>;
    using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

    [[nodiscard]] bool open();
    [[nodiscard]] bool prepareIdentity();
    [[nodiscard]] static int fail(std::string_view message);

    static constexpr DWORD maximumDuration{90000};
    static constexpr DWORD maximumStop{10000};

    std::wstring m_name;
    HDESK m_previousThread{nullptr};
    Desktop m_previousInput{nullptr, CloseDesktop};
    Desktop m_desktop{nullptr, CloseDesktop};
    Handle m_token{nullptr, CloseHandle};
    std::unique_ptr<void, decltype(&LocalFree)> m_security{nullptr, LocalFree};
    bool m_threadChanged{false};
    bool m_switched{false};
};

} // namespace workpane::tests
