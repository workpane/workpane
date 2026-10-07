#include "http/MimeTypes.h"
#include "http/RequestRecord.h"
#include "http/StaticFileResolver.h"
#include "http/StaticFileServer.h"
#include "support/LocalPort.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace workpane::http {

// Writes the files of a small site: an index, a nested folder with its own index, a text file with a space in its name and an empty folder.
class HttpTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const std::filesystem::path root = m_directory.path();
        write(root / "index.html", "<p>home</p>");
        write(root / "nested" / "index.htm", "<p>nested</p>");
        write(root / "notes file.txt", "plain words");
        write(root / "data.unknownext", "bytes");
        std::filesystem::create_directories(root / "empty");
    }

    static void write(const std::filesystem::path& file, const std::string& content) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream stream(file, std::ios::binary);
        stream << content;
    }

    [[nodiscard]] std::filesystem::path root() const {
        return std::filesystem::canonical(m_directory.path());
    }

    // The port is one the operating system picks, so tests running at the same time never reach for the same one.
    [[nodiscard]] std::unique_ptr<StaticFileServer> serve(int& port, StaticFileServer::RequestSink sink) const {
        port = tests::LocalPort::free();
        auto started = StaticFileServer::start("127.0.0.1", port, root(), sink);

        return started.hasValue() ? std::move(started.value()) : nullptr;
    }

  private:
    tests::TemporaryDirectory m_directory;
};

TEST_F(HttpTest, ResolvesFilesFoldersAndEncodedNames) {
    const auto home = StaticFileResolver::resolve(root(), "/");
    ASSERT_TRUE(home.hasValue());
    EXPECT_EQ(home.value().path.filename(), "index.html");
    EXPECT_EQ(home.value().mimeType, "text/html");

    const auto nested = StaticFileResolver::resolve(root(), "/nested/?query=1");
    ASSERT_TRUE(nested.hasValue());
    EXPECT_EQ(nested.value().path.filename(), "index.htm");

    const auto spaced = StaticFileResolver::resolve(root(), "/notes%20file.txt");
    ASSERT_TRUE(spaced.hasValue());
    EXPECT_EQ(spaced.value().mimeType, "text/plain");
    EXPECT_EQ(spaced.value().size, 11U);
    EXPECT_EQ(StaticFileResolver::resolve(root(), "/data.unknownext").value().mimeType, "application/octet-stream");
    EXPECT_EQ(MimeTypes::forExtension(".PNG"), "image/png");
}

TEST_F(HttpTest, RefusesTargetsThatLeaveTheRootOrNameNothing) {
    const std::vector<std::string> invalid{"/../secret", "/..%2Fsecret", "/a%00b", "/a%5Cb", "/%zz", "relative", std::string("/a\0b", 4), "/" + std::string(8193, 'a')};

    for (const auto& target : invalid) {
        const auto resolved = StaticFileResolver::resolve(root(), target);
        ASSERT_FALSE(resolved.hasValue()) << target;
        EXPECT_EQ(resolved.error().code, "http_request_invalid") << target;
    }

    for (const std::string_view target : {"/missing.txt", "/empty/", "/nested/missing/"}) {
        const auto resolved = StaticFileResolver::resolve(root(), target);
        ASSERT_FALSE(resolved.hasValue()) << target;
        EXPECT_EQ(resolved.error().code, "http_file_missing") << target;
    }

    std::error_code failure;
    std::filesystem::create_symlink("/etc/hosts", root() / "outside", failure);

    if (!failure) {
        EXPECT_EQ(StaticFileResolver::resolve(root(), "/outside").error().code, "http_file_missing");
    }

    EXPECT_FALSE(StaticFileResolver::canonicalRoot(root() / "absent").hasValue());
    EXPECT_FALSE(StaticFileResolver::canonicalRoot("relative/folder").hasValue());
}

TEST_F(HttpTest, ServesGetRequestsAndLogsEveryAnswer) {
    std::mutex mutex;
    std::vector<RequestRecord> records;
    int port = 0;
    // clang-format off
    auto server = serve(port, [&mutex, &records](RequestRecord record) { const std::lock_guard lock(mutex); records.push_back(std::move(record)); });
    // clang-format on
    ASSERT_NE(server, nullptr);

    httplib::Client client("127.0.0.1", port);
    const auto home = client.Get("/");
    ASSERT_TRUE(home);
    EXPECT_EQ(home->status, 200);
    EXPECT_EQ(home->body, "<p>home</p>");
    EXPECT_EQ(home->get_header_value("Cache-Control"), "no-store");

    const auto missing = client.Get("/missing.txt");
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 404);

    const auto posted = client.Post("/", "body", "text/plain");
    ASSERT_TRUE(posted);
    EXPECT_EQ(posted->status, 400);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < deadline) {
        {
            const std::lock_guard lock(mutex);

            if (records.size() == 3) {
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const std::lock_guard lock(mutex);
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[0].status, 200);
    EXPECT_EQ(records[0].method, "GET");
    EXPECT_EQ(records[0].path, "/");
    EXPECT_EQ(records[0].responseBytes, 11);
    EXPECT_EQ(records[0].remoteAddress, "127.0.0.1");
    EXPECT_TRUE(records[0].timestamp.ends_with("Z"));
    EXPECT_EQ(records[1].status, 404);
    EXPECT_EQ(records[2].status, 400);
    EXPECT_EQ(records[2].method, "POST");
}

TEST_F(HttpTest, RefusesAnInvalidAddressAnOccupiedPortAndReleasesThePortWhenStopped) {
    int port = 0;
    // clang-format off
    const StaticFileServer::RequestSink ignored = [](RequestRecord) {};
    // clang-format on
    auto server = serve(port, ignored);
    ASSERT_NE(server, nullptr);

    EXPECT_EQ(StaticFileServer::start("localhost", port + 1, root(), ignored).error().code, "http_host_invalid");
    EXPECT_EQ(StaticFileServer::start("127.0.0.1", 0, root(), ignored).error().code, "http_port_invalid");
    EXPECT_EQ(StaticFileServer::start("127.0.0.1", port, root() / "absent", ignored).error().code, "http_root_invalid");
    EXPECT_EQ(StaticFileServer::start("127.0.0.1", port, root(), ignored).error().code, "http_bind_failed");

    server->stop();
    auto again = StaticFileServer::start("127.0.0.1", port, root(), ignored);
    ASSERT_TRUE(again.hasValue());
    EXPECT_TRUE(StaticFileServer::numericHost("::1"));
    EXPECT_FALSE(StaticFileServer::numericHost("example.com"));
}

} // namespace workpane::http
