#include "platform/NativeViews.h"
#include "support/LocalPort.h"
#include "support/TemporaryDirectory.h"
#include "ui/NativeViewHost.h"
#include "ui/NativeWebView.h"
#include "ui/WebDownload.h"
#include "ui/WebNavigation.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::platform {

// A hidden product window with the native views of the platform, whose pages the test drives through the engine of the system.
class NativeWebViewTest : public ::testing::Test {
  protected:
    static constexpr const char* document{"<html><head><title>Probe</title><link rel=\"icon\" href=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==\"></head><body><a id=\"next\" href=\"https://example.com/next\">Next</a></body></html>"};

    void SetUp() override {
#if defined(__linux__)
        if (std::getenv("DISPLAY") == nullptr) {
            GTEST_SKIP() << "No X display runs";
        }
#endif

        ASSERT_EQ(glfwInit(), GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(800, 600, "Workpane", nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
        m_host = NativeViews::create(m_window, false, m_data.path() / "web", m_data.path() / "Downloads");
        auto created = m_host->createWebView();
        ASSERT_TRUE(created.hasValue()) << created.error().code;
        m_view = std::move(created.value());
        // clang-format off
        m_view->setNavigationHandler([this](ui::WebNavigation navigation) { m_navigation = std::move(navigation); });
        // clang-format on
    }

    void TearDown() override {
        m_popup.reset();
        m_view.reset();
        m_host.reset();

        if (m_window != nullptr) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }
    }

    // The events of the window and of the engine run until the condition holds or the time is up.
    [[nodiscard]] bool until(const std::function<bool()>& condition) const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            glfwWaitEventsTimeout(0.01);
            std::ignore = m_host->pump();
        }

        return condition();
    }

    tests::TemporaryDirectory m_data;
    GLFWwindow* m_window{nullptr};
    std::unique_ptr<ui::NativeViewHost> m_host;
    std::unique_ptr<ui::NativeWebView> m_view;
    std::unique_ptr<ui::NativeWebView> m_popup;
    ui::WebNavigation m_navigation;
};

// A document loads with its title, and the page script draws its icon and posts it as a PNG data address.
TEST_F(NativeWebViewTest, LoadsADocumentAndReportsItsIcon) {
    ASSERT_TRUE(m_view->setHtml(document).hasValue());

    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title == "Probe" && !m_navigation.icon.empty(); })) << m_navigation.title;
    // clang-format on
    EXPECT_TRUE(m_navigation.icon.starts_with("data:image/png;base64,"));
}

// A page reads a user agent that names the browser of its engine with its version, as sites expect of a current browser, and may show an element in full screen.
TEST_F(NativeWebViewTest, TellsPagesWhichBrowserItIsAndLetsThemGoFullScreen) {
    ASSERT_TRUE(m_view->setHtml("<html><head><script>document.title = navigator.userAgent + '|' + document.fullscreenEnabled;</script></head></html>").hasValue());

    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title.find('|') != std::string::npos; })) << m_navigation.title;
    // clang-format on
    const std::string agent = m_navigation.title.substr(0, m_navigation.title.find('|'));
#if defined(_WIN32)
    EXPECT_NE(agent.find(" Chrome/"), std::string::npos) << agent;
    EXPECT_NE(agent.find(" Edg/"), std::string::npos) << agent;
#else
    EXPECT_NE(agent.find(" Version/"), std::string::npos) << agent;
    EXPECT_TRUE(agent.ends_with(" Safari/605.1.15")) << agent;
#endif
    EXPECT_TRUE(m_navigation.title.ends_with("|true")) << m_navigation.title;
}

// A link the reader opens with the modifier of new tabs is posted as an address for a background tab instead of being followed.
TEST_F(NativeWebViewTest, OpensTheAddressTheReaderAsksForInTheBackground) {
    std::vector<std::pair<std::string, bool>> opened;
    // clang-format off
    m_view->setOpenHandler([&opened](std::string url, bool background) { opened.emplace_back(std::move(url), background); });
    // clang-format on
    ASSERT_TRUE(m_view->setHtml(document).hasValue());
    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title == "Probe" && !m_navigation.loading; }));
    // clang-format on

    ASSERT_TRUE(m_view->evaluate("document.getElementById('next').dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true, button: 0, metaKey: true, ctrlKey: true }));").hasValue());
    // clang-format off
    ASSERT_TRUE(until([&opened]() { return !opened.empty(); }));
    // clang-format on
    EXPECT_EQ(opened[0], std::make_pair(std::string("https://example.com/next"), true));
    EXPECT_EQ(m_navigation.title, "Probe");
}

// The channel of the page script lives in a script world of its own, so a script of the page cannot post to it, which WebView2 has no world for.
#if !defined(_WIN32)
TEST_F(NativeWebViewTest, KeepsItsChannelOutOfReachOfThePage) {
    ASSERT_TRUE(m_view->setHtml(document).hasValue());
    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title == "Probe" && !m_navigation.loading; }));
    // clang-format on

    ASSERT_TRUE(m_view->evaluate("document.title = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.workpane ? 'Reached' : 'Hidden';").hasValue());
    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title != "Probe"; }));
    // clang-format on
    EXPECT_EQ(m_navigation.title, "Hidden");
}
#endif

// A page gets a window of its own that knows its opener, and the window asking to close tells whoever shows it.
TEST_F(NativeWebViewTest, GivesAPageTheWindowItOpensAndClosesIt) {
    int closes = 0;
    ui::WebNavigation popupNavigation;
    // clang-format off
    m_view->setPopupHandler([this](std::unique_ptr<ui::NativeWebView> popup) { m_popup = std::move(popup); });
    // clang-format on
    ASSERT_TRUE(m_view->setHtml(document).hasValue());
    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title == "Probe" && !m_navigation.loading; }));
    // clang-format on

    ASSERT_TRUE(m_view->evaluate("window.open('about:blank', '_blank');").hasValue());
    // clang-format off
    ASSERT_TRUE(until([this]() { return m_popup != nullptr; }));
    // clang-format on
    // clang-format off
    m_popup->setNavigationHandler([&popupNavigation](ui::WebNavigation navigation) { popupNavigation = std::move(navigation); });
    m_popup->setCloseHandler([&closes]() { ++closes; });
    // clang-format on

    ASSERT_TRUE(m_popup->evaluate("document.title = window.opener !== null ? 'Opened' : 'Alone';").hasValue());
    // clang-format off
    ASSERT_TRUE(until([&popupNavigation]() { return popupNavigation.title == "Opened"; })) << popupNavigation.title;
    // clang-format on
    ASSERT_TRUE(m_popup->evaluate("window.close();").hasValue());
    // clang-format off
    ASSERT_TRUE(until([&closes]() { return closes == 1; }));
    // clang-format on
}

// A response sent as an attachment and one the page cannot show are saved in the downloads folder under their names, and a second copy of a name gains a counter before its extension.
TEST_F(NativeWebViewTest, SavesWhatAPageDownloadsInTheDownloadsFolder) {
    httplib::Server server;
    // clang-format off
    server.Get("/report.txt", [](const httplib::Request&, httplib::Response& response) {
        response.set_header("Content-Disposition", "attachment; filename=\"report.txt\"");
        response.set_content("the report", "text/plain");
    });

    server.Get("/archive.bin", [](const httplib::Request&, httplib::Response& response) { response.set_content(std::string("\x01\x02\x03", 3), "application/octet-stream"); });
    // clang-format on

    const int port = tests::LocalPort::bind(server);
    ASSERT_GT(port, 0);
    std::vector<ui::WebDownload> downloads;
    // clang-format off
    std::thread serving([&server]() { std::ignore = server.listen_after_bind(); });
    m_view->setDownloadHandler([&downloads](ui::WebDownload download) { downloads.push_back(std::move(download)); });
    const auto downloaded = [this, &downloads, port](const std::string& path, std::size_t count) { return m_view->navigate("http://127.0.0.1:" + std::to_string(port) + path).hasValue() && until([&downloads, count]() { return downloads.size() == count; }); };
    // clang-format on
    const bool saved = downloaded("/report.txt", 1U) && downloaded("/report.txt", 2U) && downloaded("/archive.bin", 3U);
    server.stop();
    serving.join();

    ASSERT_TRUE(saved) << downloads.size();
    const std::filesystem::path folder = m_data.path() / "Downloads";
    const std::vector<std::filesystem::path> expected{folder / "report.txt", folder / "report (1).txt", folder / "archive.bin"};

    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_TRUE(downloads[index].finished) << downloads[index].message;
        EXPECT_EQ(downloads[index].path, expected[index]);
        EXPECT_TRUE(std::filesystem::is_regular_file(expected[index])) << expected[index];
    }

    std::ifstream report(expected[1], std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(report), std::istreambuf_iterator<char>()), "the report");
}

#if !defined(__APPLE__)
// The cookies and the storage of a page are written under the data directory of the product rather than where the platform keeps them for every program.
TEST_F(NativeWebViewTest, KeepsTheDataOfItsPagesUnderTheDataDirectory) {
    httplib::Server server;
    // clang-format off
    server.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_header("Set-Cookie", "visit=1; Max-Age=3600");
        response.set_content("<html><head><title>Stored</title></head><body><script>localStorage.setItem('visit', '1');</script></body></html>", "text/html");
    });
    // clang-format on

    const int port = tests::LocalPort::bind(server);
    ASSERT_GT(port, 0);
    // clang-format off
    std::thread serving([&server]() { std::ignore = server.listen_after_bind(); });
    // clang-format on
    const std::filesystem::path web = m_data.path() / "web";
    // clang-format off
    const auto stored = [&web]() {
        std::error_code error;
        return std::filesystem::exists(web, error) && std::ranges::any_of(std::filesystem::recursive_directory_iterator(web, error), [](const std::filesystem::directory_entry& entry) { return entry.is_regular_file(); });
    };
    // clang-format on

    ASSERT_TRUE(m_view->navigate("http://127.0.0.1:" + std::to_string(port) + "/").hasValue());
    // clang-format off
    const bool loaded = until([this]() { return m_navigation.title == "Stored" && !m_navigation.loading; });
    // clang-format on
    const bool written = loaded && until(stored);
    server.stop();
    serving.join();

    EXPECT_TRUE(loaded) << m_navigation.title;
    EXPECT_TRUE(written);
}
#endif

#if defined(__linux__)
// GTK takes the locale of the reader when the first web view starts it, and numbers keep the point before their decimals, which the product and Lua read and write everywhere.
TEST(LinuxNativeViews, KeepsThePointOfNumbersOnceGtkTakesTheLocale) {
    if (std::getenv("DISPLAY") == nullptr) {
        GTEST_SKIP() << "No X display runs";
    }

    ::setenv("LC_ALL", "pt_BR.UTF-8", 1);
    ASSERT_EQ(glfwInit(), GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Workpane", nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    tests::TemporaryDirectory data;
    auto host = NativeViews::create(window, false, data.path() / "web", data.path() / "Downloads");
    auto created = host->createWebView();
    ASSERT_TRUE(created.hasValue()) << created.error().code;

    EXPECT_STREQ(std::localeconv()->decimal_point, ".");
    created.value().reset();
    host.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
}
#endif

} // namespace workpane::platform
