#include "platform/NativeProcesses.h"
#include "platform/PathTextHelper.h"
#include "platform/PrivateDirectoryHelper.h"
#include "process/Process.h"
#include "process/ProcessEnd.h"
#include "process/ProcessEvents.h"
#include "process/ProcessStream.h"
#include "support/LocalPort.h"
#include "support/Resources.h"
#include "support/SftpProbeHelper.h"
#include "support/SshTerminalProbeHelper.h"
#include "support/SshWorkspaceProbeHelper.h"
#include "support/TemporaryDirectory.h"
#include "support/WindowsAccountHelper.h"
#include "text/Base64Helper.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>

#include <windows.h>

#include <aclapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::tests {

class WindowsSshIntegrationTest : public ::testing::Test {
  protected:
    enum class ScopeEnd { Cancel, EarlyCancel, Disconnect, Crash, Lease };

    static constexpr auto startupTimeout = std::chrono::seconds(60);

    struct Output final {
        std::mutex mutex;
        std::condition_variable changed;
        std::string text;
        std::string output;
        std::string error;
        std::optional<process::ProcessEnd> end;
    };

    void SetUp() override {
        const auto ssh = m_launcher.find("ssh", {});
        const auto sshd = m_launcher.find("sshd", {});
        const auto sftpServer = m_launcher.find("sftp-server", {});
        const auto keygen = m_launcher.find("ssh-keygen", {});
        ASSERT_TRUE(ssh.has_value());
        ASSERT_TRUE(sshd.has_value());
        ASSERT_TRUE(sftpServer.has_value());
        ASSERT_TRUE(keygen.has_value());
        m_ssh = *ssh;
        m_sshd = *sshd;
        m_sftpServer = *sftpServer;
        m_root = std::filesystem::canonical(m_directory.path());
        const auto account = WindowsAccountHelper::current();
        ASSERT_TRUE(account.hasValue());
        m_account = account.value();
        std::array<unsigned char, 32> bytes{};
        ASSERT_EQ(RAND_bytes(bytes.data(), static_cast<int>(bytes.size())), 1);
        std::ostringstream token;

        for (const unsigned char byte : bytes) {
            token << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
        }

        m_token = token.str();
        m_nonce = m_token.substr(0, 32);
        secureKeys();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());

        for (const std::string_view name : {"host", "client"}) {
            const auto output = std::make_shared<Output>();
            auto process = start(*keygen, {"-q", "-t", "ed25519", "-N", "", "-f", path("keys/" + std::string(name))}, "", output);
            ASSERT_NE(process, nullptr);
            expectExit(output, 0);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
        }

        std::filesystem::copy_file(m_root / "keys/client.pub", m_root / "keys/authorized_keys");
        std::ofstream(m_root / "client_config") << "Host *\n    IdentitiesOnly yes\n    UserKnownHostsFile " << std::quoted(path("known_hosts")) << "\n    GlobalKnownHostsFile NUL\n";
        m_port = LocalPort::free();
        m_transferPort = LocalPort::free();
        ASSERT_GT(m_port, 0);
        ASSERT_GT(m_transferPort, 0);
        ASSERT_NE(m_port, m_transferPort);
        configure("sshd_config", m_port);
        configure("transfer_config", m_transferPort);
        const auto output = std::make_shared<Output>();
        m_daemonOutputs.push_back(output);
        m_daemons.push_back(start(m_sshd, {"-d", "-e", "-f", path("sshd_config")}, std::nullopt, output));
        ASSERT_NE(m_daemons.back(), nullptr);
        listening(output);
    }

    void TearDown() override {
        if (m_connection != nullptr) {
            m_connection->stop();
            m_connection.reset();
        }

        for (auto& daemon : m_daemons) {
            if (daemon != nullptr) {
                daemon->stop();
            }
        }

        m_daemons.clear();

        if (::testing::Test::HasFailure()) {
            for (const auto& output : m_daemonOutputs) {
                const std::lock_guard lock(output->mutex);
                ADD_FAILURE() << output->text;
            }
        }
    }

    [[nodiscard]] std::string path(std::string_view name) const {
        return platform::PathTextHelper::generic(m_root / name);
    }

    void secureKeys() const {
        const auto keys = m_root / "keys";
        ASSERT_FALSE(platform::PrivateDirectoryHelper::create(keys));
        auto name = keys.wstring();
        PSECURITY_DESCRIPTOR received = nullptr;
        PSID owner = nullptr;
        ASSERT_EQ(GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &received), ERROR_SUCCESS);
        const std::unique_ptr<void, decltype(&LocalFree)> descriptor(received, LocalFree);
        ASSERT_NE(owner, nullptr);
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_ALL_ACCESS;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        BuildTrusteeWithSidW(&access.Trustee, owner);
        PACL created = nullptr;
        ASSERT_EQ(SetEntriesInAclW(1, &access, nullptr, &created), ERROR_SUCCESS);
        const std::unique_ptr<void, decltype(&LocalFree)> permissions(created, LocalFree);
        ASSERT_EQ(SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, created, nullptr), ERROR_SUCCESS);
    }

    [[nodiscard]] std::unique_ptr<process::Process> start(const std::filesystem::path& executable, std::vector<std::string> arguments, std::optional<std::string> input, const std::shared_ptr<Output>& output) {
        process::ProcessEvents events;
        // clang-format off
        events.output = [output](process::ProcessStream stream, std::string bytes) {
            const std::lock_guard lock(output->mutex);
            output->text.append(bytes, 0, std::min(bytes.size(), 131072U - output->text.size()));
            auto& captured = stream == process::ProcessStream::Output ? output->output : output->error;
            captured.append(bytes, 0, std::min(bytes.size(), 131072U - captured.size()));
            output->changed.notify_all();
        };

        events.exited = [output](process::ProcessEnd end) {
            const std::lock_guard lock(output->mutex);
            output->end = end;
            output->changed.notify_all();
        };
        // clang-format on

        auto process = m_launcher.start({executable, std::move(arguments), m_root, {}, {}, std::move(input)}, std::move(events));
        EXPECT_TRUE(process.hasValue()) << (process.hasValue() ? "" : process.error().code);

        return process.hasValue() ? std::move(process.value()) : nullptr;
    }

    void expectExit(const std::shared_ptr<Output>& output, int code) const {
        std::unique_lock lock(output->mutex);
        // clang-format off
        ASSERT_TRUE(output->changed.wait_for(lock, std::chrono::seconds(30), [&output]() { return output->end.has_value(); }));
        // clang-format on
        ASSERT_EQ(output->end->code, code) << output->text;
        ASSERT_FALSE(output->end->crashed) << output->text;
    }

    void listening(const std::shared_ptr<Output>& output) const {
        std::unique_lock lock(output->mutex);
        // clang-format off
        ASSERT_TRUE(output->changed.wait_for(lock, std::chrono::seconds(10), [&output]() { return output->end.has_value() || output->text.find("Server listening on") != std::string::npos; }));
        // clang-format on
        ASSERT_FALSE(output->end.has_value()) << output->text;
    }

    void configure(std::string_view file, int port) const {
        std::ofstream(m_root / file) << "ListenAddress 127.0.0.1\nPort " << port << "\nHostKey " << std::quoted(path("keys/host")) << "\nAuthorizedKeysFile " << std::quoted(path("keys/authorized_keys")) << "\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nPubkeyAuthentication yes\nSubsystem sftp " << std::quoted(platform::PathTextHelper::generic(m_sftpServer)) << "\nLogLevel VERBOSE\n";
        std::ifstream publicKey(m_root / "keys/host.pub");
        std::string algorithm;
        std::string encoded;
        publicKey >> algorithm >> encoded;
        std::ofstream(m_root / "known_hosts", std::ios::app) << "[127.0.0.1]:" << port << " " << algorithm << " " << encoded << "\n";
    }

    [[nodiscard]] std::vector<std::string> options() const {
        return {"-F", path("client_config"), "-i", path("keys/client"), "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes", "-o", "UserKnownHostsFile=" + path("known_hosts"), "-o", "GlobalKnownHostsFile=NUL", "-o", "ConnectTimeout=5", "-o", "ControlMaster=no", "-o", "ControlPath=none"};
    }

    [[nodiscard]] std::optional<std::string> line(std::string_view prefix, std::chrono::seconds timeout = std::chrono::seconds(30)) const {
        return line(m_output, prefix, timeout);
    }

    [[nodiscard]] static std::optional<std::string> line(const std::shared_ptr<Output>& output, std::string_view prefix, std::chrono::seconds timeout = std::chrono::seconds(30)) {
        std::unique_lock lock(output->mutex);
        // clang-format off
        const auto complete = [&]() {
            const auto begin = output->text.find(prefix);
            return (begin != std::string::npos && output->text.find('\n', begin) != std::string::npos) || output->end.has_value();
        };
        // clang-format on

        EXPECT_TRUE(output->changed.wait_for(lock, timeout, complete));
        const auto begin = output->text.find(prefix);

        if (begin == std::string::npos) {
            ADD_FAILURE() << output->text;
            return std::nullopt;
        }

        const auto end = output->text.find('\n', begin);

        if (end == std::string::npos) {
            ADD_FAILURE() << output->text;
            return std::nullopt;
        }

        return output->text.substr(begin, end - begin - (end > begin && output->text[end - 1] == '\r' ? 1U : 0U));
    }

    [[nodiscard]] static std::string asset(std::string_view name) {
        std::ifstream file(Resources::staged() / "plugins" / "remote-ssh" / "assets" / name, std::ios::binary);
        const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        EXPECT_FALSE(source.empty());
        EXPECT_LE(source.size(), 49152U);

        return source;
    }

    [[nodiscard]] static std::string encoded(std::string_view source) {
        std::string utf16;

        for (const char character : source) {
            const auto byte = static_cast<unsigned char>(character);
            EXPECT_LT(byte, 128U);
            utf16.push_back(static_cast<char>(byte));
            utf16.push_back('\0');
        }

        return text::Base64Helper::encode(utf16);
    }

    [[nodiscard]] std::unique_ptr<process::Process> source(int port, std::string_view content, const std::shared_ptr<Output>& output) {
        auto arguments = options();
        const std::string command = "powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand " + encoded(asset("ssh-loader-windows.ps1"));
        EXPECT_LE(command.size(), 8191U);
        arguments.insert(arguments.end(), {"-T", "-p", std::to_string(port), "-l", m_account, "127.0.0.1", command});
        auto running = start(m_ssh, std::move(arguments), std::nullopt, output);

        if (running != nullptr) {
            EXPECT_TRUE(running->write(text::Base64Helper::encode(content) + "\n"));
        }

        return running;
    }

    void verifyManagedJobs(ScopeEnd ending) {
        const std::string identity = m_token.substr(32);
        const std::string declaration = "$scopeNonce = '" + m_nonce + "'\n$scopeIdentity = '" + identity + "'\n$scopeLease = " + (ending == ScopeEnd::Lease ? "2000" : "60000") + "\n";
        const std::string control = asset("ssh-jobs-windows.ps1") + declaration + R"ps(
$scope = [Workpane.Ssh.Jobs]::new($scopeNonce, $sshInput, $scopeLease, $sshInputFile.SafeFileHandle.DangerousGetHandle())

try {
    $job = $scope.Open($scopeIdentity)
    [Console]::Out.WriteLine('OWNER ' + $PID)
    [Console]::Out.WriteLine('ADMITTED ' + $job)

    while ($true) {
        $command = $scope.ReadControl()

        if ($null -eq $command -or $command -eq 'quit') {
            break
        }

        if ($command -eq 'stop') {
            $scope.Stop($scopeIdentity)
            [Console]::Out.WriteLine('STOPPED')
            continue
        }

        if ($command -eq 'ping') {
            $scope.Renew()
            [Console]::Out.WriteLine('PONG')
            continue
        }

        if ($command -eq 'closed') {
            $refused = $false

            try {
                $scope.Open($scopeIdentity)
            }

            catch {
                if ($_.Exception.InnerException -isnot [System.InvalidOperationException]) {
                    throw
                }

                $refused = $true
            }

            if (!$refused) {
                throw 'The closed SSH identity was admitted'
            }

            [Console]::Out.WriteLine('REFUSED')
            continue
        }

        throw 'The SSH scope command is invalid'
    }
}

finally {
    $scope.Dispose()
}

)ps";
        m_output = std::make_shared<Output>();
        m_connection = source(m_port, control, m_output);
        ASSERT_NE(m_connection, nullptr);
        const auto ownerLine = line("OWNER ", startupTimeout);
        const auto admitted = line("ADMITTED ");
        ASSERT_TRUE(ownerLine.has_value());
        ASSERT_TRUE(admitted.has_value());
        const auto job = admitted->substr(std::string_view("ADMITTED ").size());

        if (ending == ScopeEnd::EarlyCancel) {
            ASSERT_TRUE(m_connection->write("stop\n"));
            ASSERT_TRUE(line("STOPPED").has_value());
        }

        const auto transferOutput = std::make_shared<Output>();
        m_daemonOutputs.push_back(transferOutput);
        m_daemons.push_back(start(m_sshd, {"-d", "-e", "-f", path("transfer_config")}, std::nullopt, transferOutput));
        ASSERT_NE(m_daemons.back(), nullptr);
        listening(transferOutput);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        const std::string producer = asset("ssh-jobs-windows.ps1") + "\n[Workpane.Ssh.Jobs]::Join('" + job + "')\n$childCode = '" + encoded("Start-Sleep -Seconds 120") + "'\n" + R"ps(
$launch = [System.Diagnostics.ProcessStartInfo]::new()
$launch.FileName = 'powershell.exe'
$launch.Arguments = '-NoLogo -NoProfile -NonInteractive -EncodedCommand ' + $childCode
$launch.UseShellExecute = $false
$launch.RedirectStandardInput = $true
$launch.RedirectStandardOutput = $true
$launch.RedirectStandardError = $true
$child = [System.Diagnostics.Process]::Start($launch)

try {
    [Console]::Out.WriteLine('MEMBERS ' + $PID + ' ' + $child.Id)
    $child.WaitForExit()
}

finally {
    $child.Dispose()
}

)ps";
        const auto producerOutput = std::make_shared<Output>();
        auto producing = source(m_transferPort, producer, producerOutput);
        ASSERT_NE(producing, nullptr);

        if (ending == ScopeEnd::EarlyCancel) {
            std::unique_lock lock(producerOutput->mutex);
            // clang-format off
            ASSERT_TRUE(producerOutput->changed.wait_for(lock, startupTimeout, [&producerOutput]() { return producerOutput->end.has_value(); }));
            // clang-format on
            EXPECT_NE(producerOutput->end->code, 0);
            EXPECT_EQ(producerOutput->text.find("MEMBERS "), std::string::npos);
            lock.unlock();
            ASSERT_TRUE(m_connection->write("closed\nquit\n"));
            ASSERT_TRUE(line("REFUSED").has_value());
            return;
        }

        if (ending == ScopeEnd::Lease) {
            const auto deadline = std::chrono::steady_clock::now() + startupTimeout;

            while (std::chrono::steady_clock::now() < deadline) {
                std::unique_lock lock(producerOutput->mutex);
                const auto begin = producerOutput->text.find("MEMBERS ");

                if (producerOutput->end.has_value() || (begin != std::string::npos && producerOutput->text.find('\n', begin) != std::string::npos)) {
                    break;
                }

                std::ignore = producerOutput->changed.wait_for(lock, std::chrono::milliseconds(200));
                lock.unlock();
                ASSERT_TRUE(m_connection->write("ping\n"));
            }
        }

        const auto members = line(producerOutput, "MEMBERS ", startupTimeout);
        ASSERT_TRUE(members.has_value());
        std::istringstream identifiers(members->substr(std::string_view("MEMBERS ").size()));
        DWORD parent = 0;
        DWORD child = 0;
        identifiers >> parent >> child;
        ASSERT_GT(parent, 0U);
        ASSERT_GT(child, 0U);
        const std::unique_ptr<void, decltype(&CloseHandle)> parentHandle(OpenProcess(SYNCHRONIZE, FALSE, parent), CloseHandle);
        const std::unique_ptr<void, decltype(&CloseHandle)> childHandle(OpenProcess(SYNCHRONIZE, FALSE, child), CloseHandle);
        ASSERT_NE(parentHandle.get(), nullptr);
        ASSERT_NE(childHandle.get(), nullptr);
        ASSERT_EQ(WaitForSingleObject(parentHandle.get(), 0), WAIT_TIMEOUT);
        ASSERT_EQ(WaitForSingleObject(childHandle.get(), 0), WAIT_TIMEOUT);

        if (ending == ScopeEnd::Cancel) {
            ASSERT_TRUE(m_connection->write("stop\n"));
            ASSERT_TRUE(line("STOPPED").has_value());
            ASSERT_TRUE(m_connection->write("closed\n"));
            ASSERT_TRUE(line("REFUSED").has_value());
            ASSERT_TRUE(m_connection->write("quit\n"));
        }

        if (ending == ScopeEnd::Disconnect) {
            m_connection->stop();
        }

        if (ending == ScopeEnd::Crash) {
            const auto owner = static_cast<DWORD>(std::stoul(ownerLine->substr(std::string_view("OWNER ").size())));
            const std::unique_ptr<void, decltype(&CloseHandle)> ownerHandle(OpenProcess(PROCESS_TERMINATE, FALSE, owner), CloseHandle);
            ASSERT_NE(ownerHandle.get(), nullptr);
            ASSERT_TRUE(TerminateProcess(ownerHandle.get(), 1));
        }

        ASSERT_EQ(WaitForSingleObject(parentHandle.get(), 30000), WAIT_OBJECT_0);
        ASSERT_EQ(WaitForSingleObject(childHandle.get(), 30000), WAIT_OBJECT_0);
        std::unique_lock lock(producerOutput->mutex);
        // clang-format off
        ASSERT_TRUE(producerOutput->changed.wait_for(lock, std::chrono::seconds(30), [&producerOutput]() { return producerOutput->end.has_value(); }));
        // clang-format on
    }

    TemporaryDirectory m_directory;
    platform::NativeProcesses m_launcher;
    std::filesystem::path m_root;
    std::filesystem::path m_ssh;
    std::filesystem::path m_sshd;
    std::filesystem::path m_sftpServer;
    std::string m_account;
    std::string m_token;
    std::string m_nonce;
    int m_port{0};
    int m_transferPort{0};
    std::vector<std::unique_ptr<process::Process>> m_daemons;
    std::vector<std::shared_ptr<Output>> m_daemonOutputs;
    std::unique_ptr<process::Process> m_connection;
    std::shared_ptr<Output> m_output;
};

TEST_F(WindowsSshIntegrationTest, TransfersBinaryFilesThroughTheNativeSftpSubsystemAndProductProcessBridge) {
    auto arguments = options();
    arguments.insert(arguments.end(), {"-T", "-p", std::to_string(m_port), "-l", m_account, "127.0.0.1"});
    SftpProbeHelper::verify(m_ssh, std::move(arguments), m_root);
}

TEST_F(WindowsSshIntegrationTest, ExpandsPlatformDiscoveryInBothNativeWindowsCommandShells) {
    const std::string marker = "WORKPANE_" + m_nonce;
    const std::string probe = "echo " + marker + ":%OS%:$env:OS";
    const auto command = m_launcher.find("cmd.exe", {});
    const auto powershell = m_launcher.find("powershell.exe", {});
    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(powershell.has_value());
    const auto commandOutput = std::make_shared<Output>();
    const auto powerShellOutput = std::make_shared<Output>();
    auto commandProcess = start(*command, {"/d", "/c", probe}, "", commandOutput);
    auto powerShellProcess = start(*powershell, {"-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded(probe)}, "", powerShellOutput);
    ASSERT_NE(commandProcess, nullptr);
    ASSERT_NE(powerShellProcess, nullptr);
    EXPECT_EQ(line(commandOutput, marker), marker + ":Windows_NT:$env:OS");
    EXPECT_EQ(line(powerShellOutput, marker), marker + ":%OS%:Windows_NT");
    expectExit(commandOutput, 0);
    expectExit(powerShellOutput, 0);
}

TEST_F(WindowsSshIntegrationTest, EditsFilesAndRunsOwnedProgramsThroughTheProductionSshWorkspace) {
    std::vector<int> ports{m_port};

    for (int index = 1; index < 20; ++index) {
        const auto port = LocalPort::free();
        ASSERT_GT(port, 0);
        ASSERT_EQ(std::ranges::find(ports, port), ports.end());
        const auto configuration = "workspace_config_" + std::to_string(index);
        configure(configuration, port);
        const auto output = std::make_shared<Output>();
        m_daemonOutputs.push_back(output);
        m_daemons.push_back(start(m_sshd, {"-d", "-e", "-f", path(configuration)}, std::nullopt, output));
        ASSERT_NE(m_daemons.back(), nullptr);
        listening(output);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ports.push_back(port);
    }

    const nlohmann::json profile{{"id", "direct-workspace"}, {"name", "Native workspace"}, {"host", "127.0.0.1"}, {"user", m_account}, {"port", m_port}, {"identity", path("keys/client")}, {"configuration", path("client_config")}, {"root", path("workspace")}, {"savePassword", false}};
    SshWorkspaceProbeHelper::verify(m_ssh, profile, ports, m_root);
}

TEST_F(WindowsSshIntegrationTest, CancelsNativeSshJobsAndRefusesClosedAdmission) {
    verifyManagedJobs(ScopeEnd::Cancel);
}

TEST_F(WindowsSshIntegrationTest, RunsAnInteractiveSshTerminalWithNativeResizeAndInterruptionThroughTheProduct) {
    const auto port = LocalPort::free();
    ASSERT_GT(port, 0);
    ASSERT_NE(port, m_port);
    ASSERT_NE(port, m_transferPort);
    configure("terminal_transfer_config", port);

    for (const auto& configuration : {"transfer_config", "terminal_transfer_config"}) {
        const auto output = std::make_shared<Output>();
        m_daemonOutputs.push_back(output);
        m_daemons.push_back(start(m_sshd, {"-d", "-e", "-f", path(configuration)}, std::nullopt, output));
        ASSERT_NE(m_daemons.back(), nullptr);
        listening(output);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
    }

    auto control = options();
    control.insert(control.end(), {"-T", "-p", std::to_string(m_port), "-l", m_account, "127.0.0.1"});
    const int proxyPort = LocalPort::free();
    ASSERT_GT(proxyPort, 0);
    control.insert(control.end() - 1, {"-D", "127.0.0.1:" + std::to_string(proxyPort)});
    auto transfer = options();
    transfer.insert(transfer.end(), {"-T", "-p", std::to_string(port), "-l", m_account, "127.0.0.1"});
    auto terminal = options();
    terminal.insert(terminal.end(), {"-T", "-p", std::to_string(m_transferPort), "-l", m_account, "127.0.0.1"});
    SshTerminalProbeHelper::verify(m_ssh, control, transfer, std::move(terminal), m_root);
}

TEST_F(WindowsSshIntegrationTest, RecyclesNativeJobResourcesWithoutReopeningClosedProgramIdentities) {
    const auto output = std::make_shared<Output>();
    const std::string content = asset("ssh-jobs-windows.ps1") + "\n$scopeNonce = '" + m_nonce + "'\n" + R"ps(
$scope = [Workpane.Ssh.Jobs]::new($scopeNonce, $sshInput, 60000, $sshInputFile.SafeFileHandle.DangerousGetHandle())

try {
    for ($index = 1; $index -le 1000; $index++) {
        $identity = $index.ToString('x32')
        $null = $scope.Open($identity)
        $scope.Stop($identity)
    }

    $cancelled = (1001).ToString('x32')
    $scope.Stop($cancelled)
    $refused = $false

    try {
        $null = $scope.Open($cancelled)
    }

    catch {
        $refused = $_.Exception.InnerException -is [System.InvalidOperationException]
    }

    if (-not $refused) {
        throw 'The cancelled SSH program was admitted'
    }

    for ($index = 1002; $index -lt 1034; $index++) {
        $null = $scope.Open($index.ToString('x32'))
    }

    $refused = $false

    try {
        $null = $scope.Open((1034).ToString('x32'))
    }

    catch {
        $refused = $_.Exception.InnerException -is [System.InvalidOperationException]
    }

    if (-not $refused) {
        throw 'The SSH program admission bound was exceeded'
    }

    for ($index = 1002; $index -lt 1034; $index++) {
        $scope.Stop($index.ToString('x32'))
    }

    $null = $scope.Open((1034).ToString('x32'))
    $scope.Stop((1034).ToString('x32'))
    [Console]::Out.WriteLine('SUSTAINED 1000')
}


finally {
    $scope.Dispose()
}

)ps";
    auto running = source(m_port, content, output);
    ASSERT_NE(running, nullptr);
    EXPECT_EQ(line(output, "SUSTAINED ", startupTimeout), "SUSTAINED 1000");
}

TEST_F(WindowsSshIntegrationTest, PreservesOwnedBinaryStreamsAndActualTargetExitAfterItsSourceHeader) {
    m_output = std::make_shared<Output>();
    const auto control = asset("ssh-jobs-windows.ps1") + asset("ssh-paths-windows.ps1") + asset("ssh-events-windows.ps1") + "\n$sshArguments = @('" + m_nonce + "', '60000')\n" + asset("ssh-scope-windows.ps1");
    m_connection = source(m_port, control, m_output);
    ASSERT_NE(m_connection, nullptr);
    const auto announced = line("SCOPE ", startupTimeout);
    ASSERT_TRUE(announced.has_value());
    const auto boundary = announced->rfind(' ');
    ASSERT_NE(boundary, std::string::npos);
    const auto directory = announced->substr(6, boundary - 6);
    ASSERT_TRUE(m_connection->write("open 1\n"));
    ASSERT_EQ(line("OPEN "), "OPEN 1");
    const auto daemonOutput = std::make_shared<Output>();
    m_daemonOutputs.push_back(daemonOutput);
    m_daemons.push_back(start(m_sshd, {"-d", "-e", "-f", path("transfer_config")}, std::nullopt, daemonOutput));
    ASSERT_NE(m_daemons.back(), nullptr);
    listening(daemonOutput);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    const std::string target = R"ps(
if ($null -ne [Environment]::GetEnvironmentVariable('WORKPANE_CLEARED')) {
    exit 19
}

$targetOutput = [Console]::OpenStandardOutput()
$targetError = [Console]::OpenStandardError()
$value = [Text.Encoding]::ASCII.GetBytes([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($env:WORKPANE_PROBE)) + "`n")
$targetOutput.Write($value, 0, $value.Length)
[Console]::OpenStandardInput().CopyTo($targetOutput)
$diagnostic = [byte[]](0, 255, 101, 114, 114, 111, 114)
$targetError.Write($diagnostic, 0, $diagnostic.Length)
exit 7
)ps";
    const auto folder = text::Base64Helper::encode(directory + "/program.1");
    const auto value = text::Base64Helper::encode("literal '[]; 雪");
    const auto producer = asset("ssh-jobs-windows.ps1") + asset("ssh-paths-windows.ps1") + asset("ssh-programs-windows.ps1") + "\n$record = $sshEncoding.GetString([Convert]::FromBase64String('" + folder + "'))\n$encoded = '" + encoded(target) + "'\n$variables = @{ WORKPANE_PROBE = $sshEncoding.GetString([Convert]::FromBase64String('" + value + "')) }\n" + R"ps(
$job = [IO.File]::ReadAllText([IO.Path]::Combine($record, 'job'))
[Workpane.Ssh.Jobs]::Join($job)
[Workpane.Ssh.Paths]::CreatePrivate([IO.Path]::Combine($record, 'claimed'))
[Environment]::SetEnvironmentVariable('WORKPANE_CLEARED', 'present')
$executable = [IO.Path]::Combine([Environment]::SystemDirectory, 'WindowsPowerShell/v1.0/powershell.exe')
$arguments = [string[]]@('-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded)
$code = [Workpane.Ssh.Programs]::Run($executable, $arguments, $record, $variables, [string[]]@('WORKPANE_CLEARED'), $false, $sshInput, $sshOutput, $sshError, $record, $sshInputFile.SafeFileHandle.DangerousGetHandle())
exit $code
)ps";
    const auto output = std::make_shared<Output>();
    auto running = source(m_transferPort, producer, output);
    ASSERT_NE(running, nullptr);
    const std::string payload = std::string("\0\xff\x80\xc3\xa9\r\n", 7) + std::string(66000, 'x');
    ASSERT_TRUE(running->write(payload));
    ASSERT_TRUE(running->endInput());
    ASSERT_EQ(line("DONE ", startupTimeout), "DONE 1 7 1");
    expectExit(output, 7);
    EXPECT_EQ(output->output, value + "\n" + payload);
    EXPECT_EQ(output->error, std::string("\0\xff", 2) + "error");
    ASSERT_TRUE(m_connection->write("stop 1\n"));
    ASSERT_EQ(line("STOPPED "), "STOPPED 1");
    ASSERT_TRUE(m_connection->write("quit\n"));
    expectExit(m_output, 0);
    const std::u8string nativeDirectory(directory.begin(), directory.end());
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(nativeDirectory)));
}

TEST_F(WindowsSshIntegrationTest, AcceptsTheCompleteBoundedSourceHeaderWithoutLosingItsNextPhase) {
    const auto output = std::make_shared<Output>();
    std::string content = R"ps(
$next = [Workpane.Ssh.Streams]::Source($sshInput, 128)

if ($next -ne 'next-phase') {
    throw 'The SSH source loader lost its next phase'
}

$tail = [Workpane.Ssh.Streams]::Source($sshInput, 128)

if ($tail -ne 'raw-tail') {
    throw 'The SSH source loader lost the following program input'
}

[Console]::Out.WriteLine('SOURCE_BOUNDARY')

)ps";
    content.resize(49152, ' ');
    auto running = source(m_port, content, output);
    ASSERT_NE(running, nullptr);
    ASSERT_TRUE(running->write("next-phase\nraw-tail\n"));
    EXPECT_EQ(line(output, "SOURCE_BOUNDARY", startupTimeout), "SOURCE_BOUNDARY");
}

TEST_F(WindowsSshIntegrationTest, ResolvesNativeFilesystemTargetsAndRefusesJunctionEscapes) {
    const auto output = std::make_shared<Output>();
    const std::string root = text::Base64Helper::encode(platform::PathTextHelper::generic(m_root));
    const std::string name = text::Base64Helper::encode("Case Å雪");
    const std::string content = asset("ssh-paths-windows.ps1") + "\n$fixture = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + root + "'))\n$name = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + name + "'))\n" + R"ps(
$workspace = [IO.Path]::Combine($fixture, 'workspace')
$inside = [IO.Path]::Combine($workspace, $name)
$outside = [IO.Path]::Combine($fixture, 'workspace-other')
$null = [IO.Directory]::CreateDirectory($inside)
$null = [IO.Directory]::CreateDirectory($outside)
$document = [IO.Path]::Combine($inside, 'document.txt')
[IO.File]::WriteAllText($document, 'inside')
$external = [IO.Path]::Combine($outside, 'document.txt')
[IO.File]::WriteAllText($external, 'outside')
$resolved = [Workpane.Ssh.Paths]::Resolve($workspace, $document.ToUpperInvariant(), $false)

if (-not [String]::Equals($resolved, $document.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The native Unicode filesystem target was not resolved'
}

$new = [IO.Path]::Combine($inside, 'new', 'document.txt')
$resolved = [Workpane.Ssh.Paths]::Resolve($workspace, $new, $true)

if (-not [String]::Equals($resolved, $new.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The missing native filesystem target was not resolved'
}

$junction = [IO.Path]::Combine($workspace, 'jump')
$null = New-Item -ItemType Junction -Path $junction -Target $outside
$internal = [IO.Path]::Combine($workspace, 'internal')
$null = New-Item -ItemType Junction -Path $internal -Target $inside
$resolved = [Workpane.Ssh.Paths]::Resolve($workspace, [IO.Path]::Combine($internal, 'document.txt'), $false)

if (-not [String]::Equals($resolved, $document.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The internal native junction was not resolved'
}

foreach ($candidate in @($external, [IO.Path]::Combine($junction, 'document.txt'), [IO.Path]::Combine($junction, 'uncreated.txt'))) {
    $refused = $false

    try {
        $null = [Workpane.Ssh.Paths]::Resolve($workspace, $candidate, $true)
    }

    catch {
        $refused = $_.Exception.InnerException -is [UnauthorizedAccessException]
    }

    if (-not $refused) {
        throw 'The native filesystem target escaped its workspace'
    }
}

$dangling = [IO.Path]::Combine($workspace, 'dangling')
$absent = [IO.Path]::Combine($outside, 'absent')
$null = [IO.Directory]::CreateDirectory($absent)
$null = New-Item -ItemType Junction -Path $dangling -Target $absent
[IO.Directory]::Delete($absent)
$refused = $false

try {
    $null = [Workpane.Ssh.Paths]::Resolve($workspace, [IO.Path]::Combine($dangling, 'new.txt'), $true)
}

catch {
    $refused = $_.Exception.InnerException -is [ComponentModel.Win32Exception]
}

if (-not $refused) {
    throw 'The dangling native junction was treated as an absent directory'
}

[IO.Directory]::Delete($junction)
[IO.Directory]::Delete($internal)
[IO.Directory]::Delete($dangling)
[Console]::Out.WriteLine('CONTAINED')

)ps";
    auto running = source(m_port, content, output);
    ASSERT_NE(running, nullptr);
    EXPECT_EQ(line(output, "CONTAINED", startupTimeout), "CONTAINED");
}

TEST_F(WindowsSshIntegrationTest, RefusesNativeSshJobStartupAfterItsAcceptedIdentityIsCancelled) {
    verifyManagedJobs(ScopeEnd::EarlyCancel);
}

TEST_F(WindowsSshIntegrationTest, StopsNativeSshJobsWhenTheirOwnerConnectionEnds) {
    verifyManagedJobs(ScopeEnd::Disconnect);
}

TEST_F(WindowsSshIntegrationTest, StopsNativeSshJobsWhenTheirOwnerProcessCrashes) {
    verifyManagedJobs(ScopeEnd::Crash);
}

TEST_F(WindowsSshIntegrationTest, StopsNativeSshJobsWhenTheirOwnerLeaseExpires) {
    verifyManagedJobs(ScopeEnd::Lease);
}

} // namespace workpane::tests
