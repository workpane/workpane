#include "support/WindowsDesktop.h"

#include "platform/windows/WindowsTextHelper.h"

#include <gtest/gtest.h>
#include <sddl.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

namespace workpane::tests {

WindowsDesktop::WindowsDesktop() = default;

WindowsDesktop::~WindowsDesktop() {
    if (m_switched) {
        EXPECT_NE(SwitchDesktop(m_previousInput.get()), FALSE);
    }

    if (m_threadChanged) {
        EXPECT_NE(SetThreadDesktop(m_previousThread), FALSE);
    }
}

int WindowsDesktop::run(int argc, char** argv) {
    if (argc < 2 || !prepareIdentity() || !open()) {
        return fail("The native test could not open its input desktop");
    }

    const HANDLE current = GetCurrentProcess();
    std::array<Handle, 3> handles{Handle(nullptr, CloseHandle), Handle(nullptr, CloseHandle), Handle(nullptr, CloseHandle)};
    std::array<HANDLE, 4> inherited{};

    for (std::size_t index = 0; index < handles.size(); ++index) {
        const DWORD identifier = STD_INPUT_HANDLE - static_cast<DWORD>(index);

        if (DuplicateHandle(current, GetStdHandle(identifier), current, &inherited[index], 0, TRUE, DUPLICATE_SAME_ACCESS) == FALSE) {
            return fail("The native test could not inherit its standard handles");
        }

        handles[index].reset(inherited[index]);
    }

    inherited.back() = m_desktop.get();

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<std::byte> storage(size);
    const auto attributes = std::bit_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());

    if (size == 0 || InitializeProcThreadAttributeList(attributes, 1, 0, &size) == FALSE) {
        return fail("The native test could not create its inheritance list");
    }

    const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, decltype(&DeleteProcThreadAttributeList)> owned(attributes, DeleteProcThreadAttributeList);

    if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(), sizeof(inherited), nullptr, nullptr) == FALSE) {
        return fail("The native test could not restrict its inherited handles");
    }

    const Handle job(CreateJobObjectW(nullptr, nullptr), CloseHandle);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (job == nullptr || SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, static_cast<DWORD>(sizeof(limits))) == FALSE) {
        return fail("The native test could not create its process group");
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = static_cast<DWORD>(sizeof(startup));
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inherited[0];
    startup.StartupInfo.hStdOutput = inherited[1];
    startup.StartupInfo.hStdError = inherited[2];
    startup.StartupInfo.lpDesktop = m_name.data();
    startup.lpAttributeList = attributes;
    const auto program = platform::WindowsTextHelper::wide(argv[1]);
    std::wstring command;

    for (int index = 1; index < argc; ++index) {
        if (!command.empty()) {
            command.push_back(L' ');
        }

        command += platform::WindowsTextHelper::quoted(platform::WindowsTextHelper::wide(argv[index]));
    }

    PROCESS_INFORMATION started{};
    constexpr DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW;
    const BOOL created = m_token == nullptr ? CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &startup.StartupInfo, &started) : CreateProcessAsUserW(m_token.get(), program.c_str(), command.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &startup.StartupInfo, &started);

    if (created == FALSE) {
        return fail("The native test process could not be created");
    }

    const Handle process(started.hProcess, CloseHandle);
    const Handle thread(started.hThread, CloseHandle);

    if (AssignProcessToJobObject(job.get(), process.get()) == FALSE || ResumeThread(thread.get()) == MAXDWORD) {
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), maximumStop);
        return fail("The native test process could not start in its process group");
    }

    if (WaitForSingleObject(process.get(), maximumDuration) != WAIT_OBJECT_0) {
        TerminateJobObject(job.get(), 1);
        WaitForSingleObject(process.get(), maximumStop);
        return fail("The native test process did not finish within its deadline");
    }

    DWORD code = 1;

    if (GetExitCodeProcess(process.get(), &code) == FALSE) {
        return fail("The native test process did not report its exit");
    }

    return static_cast<int>(code);
}

bool WindowsDesktop::open() {
    m_previousThread = GetThreadDesktop(GetCurrentThreadId());
    m_previousInput.reset(OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP));

    if (m_previousThread == nullptr || m_previousInput == nullptr) {
        return false;
    }

    std::array<wchar_t, 256> station{};
    DWORD stationBytes = 0;

    if (GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station.data(), static_cast<DWORD>(sizeof(station)), &stationBytes) == FALSE) {
        return false;
    }

    const std::wstring name = L"WorkpaneTest-" + std::to_wstring(GetCurrentProcessId());
    m_name = std::wstring(station.data()) + L"\\" + name;
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = static_cast<DWORD>(sizeof(attributes));
    attributes.bInheritHandle = TRUE;
    attributes.lpSecurityDescriptor = m_security.get();
    m_desktop.reset(CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, &attributes));

    if (m_desktop == nullptr || SetThreadDesktop(m_desktop.get()) == FALSE) {
        return false;
    }

    m_threadChanged = true;

    if (SwitchDesktop(m_desktop.get()) == FALSE) {
        return false;
    }

    m_switched = true;

    return true;
}

bool WindowsDesktop::prepareIdentity() {
    HANDLE opened = nullptr;
    constexpr DWORD access = TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT;

    if (OpenProcessToken(GetCurrentProcess(), access, &opened) == FALSE) {
        return false;
    }

    const Handle original(opened, CloseHandle);
    struct Identity final {
        TOKEN_USER user;
        std::array<std::byte, SECURITY_MAX_SID_SIZE> storage;
    } identity{};
    DWORD size = 0;

    if (GetTokenInformation(original.get(), TokenUser, &identity, static_cast<DWORD>(sizeof(identity)), &size) == FALSE) {
        return false;
    }

    LPWSTR encoded = nullptr;

    if (ConvertSidToStringSidW(identity.user.User.Sid, &encoded) == FALSE) {
        return false;
    }

    const std::unique_ptr<wchar_t, decltype(&LocalFree)> account(encoded, LocalFree);
    const std::wstring rule = std::wstring(L"O:") + account.get() + L"D:P(A;;GA;;;SY)(A;;GA;;;" + account.get() + L")S:(ML;;NW;;;ME)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;

    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(rule.c_str(), SDDL_REVISION_1, &descriptor, nullptr) == FALSE) {
        return false;
    }

    m_security.reset(descriptor);
    TOKEN_ELEVATION elevation{};

    if (GetTokenInformation(original.get(), TokenElevation, &elevation, static_cast<DWORD>(sizeof(elevation)), &size) == FALSE) {
        return false;
    }

    if (elevation.TokenIsElevated == 0) {
        return true;
    }

    HANDLE restricted = nullptr;

    if (CreateRestrictedToken(original.get(), DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 0, nullptr, 0, nullptr, 0, nullptr, &restricted) == FALSE) {
        return false;
    }

    m_token.reset(restricted);
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    TOKEN_DEFAULT_DACL defaults{};

    if (GetSecurityDescriptorDacl(m_security.get(), &present, &defaults.DefaultDacl, &defaulted) == FALSE || present == FALSE || defaults.DefaultDacl == nullptr || SetTokenInformation(m_token.get(), TokenDefaultDacl, &defaults, static_cast<DWORD>(sizeof(defaults))) == FALSE) {
        return false;
    }

    alignas(DWORD) std::array<std::byte, SECURITY_MAX_SID_SIZE> medium{};
    DWORD mediumBytes = static_cast<DWORD>(medium.size());

    if (CreateWellKnownSid(WinMediumLabelSid, nullptr, medium.data(), &mediumBytes) == FALSE) {
        return false;
    }

    TOKEN_MANDATORY_LABEL label{};
    label.Label.Sid = medium.data();
    label.Label.Attributes = SE_GROUP_INTEGRITY;

    if (SetTokenInformation(m_token.get(), TokenIntegrityLevel, &label, static_cast<DWORD>(sizeof(label)) + mediumBytes) == FALSE || GetTokenInformation(m_token.get(), TokenElevation, &elevation, static_cast<DWORD>(sizeof(elevation)), &size) == FALSE) {
        return false;
    }

    if (elevation.TokenIsElevated != 0) {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }

    return true;
}

int WindowsDesktop::fail(std::string_view message) {
    const auto code = GetLastError();
    std::fprintf(stderr, "%.*s. Native error %lu.\n", static_cast<int>(message.size()), message.data(), code);
    std::fflush(stderr);

    return 1;
}

} // namespace workpane::tests
