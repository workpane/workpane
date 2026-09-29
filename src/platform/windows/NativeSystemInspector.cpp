#include "platform/NativeSystemInspector.h"

#include "platform/windows/SmbiosTable.h"
#include "platform/windows/WindowsRegistry.h"
#include "platform/windows/WindowsText.h"
#include "text/Integers.h"

#include <winsock2.h>

#include <windows.h>

#include <batclass.h>
#include <dxgi.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include <winioctl.h>
#include <winternl.h>
#include <wrl/client.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

namespace workpane::platform {

// Windows 11 still names itself Windows 10 in the registry, and only its build number tells the two apart.
nlohmann::json NativeSystemInspector::operatingSystem() {
    const wchar_t* current = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const std::string build = WindowsRegistry::text(current, L"CurrentBuildNumber");
    std::string name = WindowsRegistry::text(current, L"ProductName");

    if (text::Integers::parse(build).value_or(0) >= 22000 && name.starts_with("Windows 10")) {
        name.replace(0, 10, "Windows 11");
    }

    std::array<wchar_t, 256> host{};
    DWORD length = static_cast<DWORD>(host.size());
    GetComputerNameExW(ComputerNameDnsHostname, host.data(), &length);
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    const WORD architecture = system.wProcessorArchitecture;
    const bool wide = architecture == PROCESSOR_ARCHITECTURE_AMD64 || architecture == PROCESSOR_ARCHITECTURE_ARM64 || architecture == PROCESSOR_ARCHITECTURE_IA64;
    const std::string version = WindowsRegistry::text(current, L"DisplayVersion") + " (" + build + "." + std::to_string(WindowsRegistry::number(current, L"UBR")) + ")";
    const std::string kernel = "Windows NT " + std::to_string(WindowsRegistry::number(current, L"CurrentMajorVersionNumber")) + "." + std::to_string(WindowsRegistry::number(current, L"CurrentMinorVersionNumber"));

    return {{"hostName", WindowsText::narrow(host.data())}, {"name", name}, {"version", version}, {"kernel", kernel}, {"architectureBits", wide ? 64 : 32}, {"byteOrder", "little-endian"}};
}

// Each physical core and each cache names the logical processors it serves in a mask, which is how a cache is matched to its cores.
nlohmann::json NativeSystemInspector::processors() {
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &size);
    std::vector<std::uint8_t> buffer(size);
    nlohmann::json cores = nlohmann::json::array();
    std::vector<KAFFINITY> masks;
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX> caches;
    int logical = 0;

    if (size > 0 && GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &size) != 0) {
        for (DWORD offset = 0; offset < size;) {
            const auto* record = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);

            if (record->Relationship == RelationProcessorCore) {
                masks.push_back(record->Processor.GroupMask[0].Mask);
                logical += std::popcount(static_cast<std::uint64_t>(record->Processor.GroupMask[0].Mask));
                cores.push_back({{"id", cores.size()}, {"smt", (record->Processor.Flags & LTP_PC_SMT) != 0}});
            }

            if (record->Relationship == RelationCache) {
                caches.push_back(*record);
            }

            offset += record->Size;
        }
    }

    const wchar_t* processor = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    const std::uint64_t frequency = static_cast<std::uint64_t>(WindowsRegistry::number(processor, L"~MHz")) * 1000000;

    for (std::size_t index = 0; index < cores.size(); ++index) {
        std::array<std::uint64_t, 4> sizes{};

        for (const auto& cache : caches) {
            const bool shared = (cache.Cache.GroupMask.Mask & masks[index]) != 0;
            const std::size_t slot = cache.Cache.Level == 1 ? (cache.Cache.Type == CacheInstruction ? 1 : 0) : cache.Cache.Level == 2 ? 2 : 3;

            if (shared && cache.Cache.Level >= 1 && cache.Cache.Level <= 3) {
                sizes[slot] = cache.Cache.CacheSize;
            }
        }

        cores[index]["l1Data"] = sizes[0];
        cores[index]["l1Instruction"] = sizes[1];
        cores[index]["l2"] = sizes[2];
        cores[index]["l3"] = sizes[3];
        cores[index]["maximumFrequency"] = frequency;
    }

    static constexpr std::array<std::pair<DWORD, const char*>, 14> features{{{13, "SSE3"}, {36, "SSSE3"}, {37, "SSE4.1"}, {38, "SSE4.2"}, {39, "AVX"}, {40, "AVX2"}, {41, "AVX512F"}, {12, "NX"}, {28, "RDRAND"}, {19, "NEON"}, {30, "ARMv8 Crypto"}, {31, "CRC32"}, {34, "LSE"}, {43, "ARMv8.2 DotProd"}}};
    nlohmann::json flags = nlohmann::json::array();

    for (const auto& [feature, name] : features) {
        if (IsProcessorFeaturePresent(feature) != 0) {
            flags.push_back(name);
        }
    }

    return nlohmann::json::array({{{"vendor", WindowsRegistry::text(processor, L"VendorIdentifier")}, {"model", WindowsRegistry::text(processor, L"ProcessorNameString")}, {"physicalCores", cores.size()}, {"logicalCores", logical}, {"cores", cores}, {"flags", flags}}});
}

// Utilization is the share of the time each processor spent outside its idle loop between two samples taken a fifth of a second apart.
nlohmann::json NativeSystemInspector::processorUsage() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const std::size_t count = system.dwNumberOfProcessors;
    // clang-format off
    const auto sample = [count]() {
        std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> times(count);
        ULONG written = 0;

        if (NtQuerySystemInformation(SystemProcessorPerformanceInformation, times.data(), static_cast<ULONG>(times.size() * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)), &written) != 0) {
            times.clear();
        }

        return times;
    };
    // clang-format on

    const auto before = sample();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto after = sample();

    if (before.empty() || before.size() != after.size()) {
        return nlohmann::json::object();
    }

    nlohmann::json threads = nlohmann::json::array();
    double total = 0.0;

    for (std::size_t index = 0; index < before.size(); ++index) {
        const auto idle = static_cast<double>(after[index].IdleTime.QuadPart - before[index].IdleTime.QuadPart);
        const auto busy = static_cast<double>(after[index].KernelTime.QuadPart - before[index].KernelTime.QuadPart + after[index].UserTime.QuadPart - before[index].UserTime.QuadPart);
        const double utilization = busy > 0.0 ? std::clamp((busy - idle) / busy, 0.0, 1.0) : 0.0;
        threads.push_back({{"utilization", utilization}, {"frequency", 0}});
        total += utilization;
    }

    return {{"utilization", total / static_cast<double>(before.size())}, {"threads", threads}};
}

nlohmann::json NativeSystemInspector::memory() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);

    return {{"total", status.ullTotalPhys}, {"free", status.ullAvailPhys}, {"available", status.ullAvailPhys}, {"modules", SmbiosTable().memoryModules()}};
}

// Every hardware adapter DirectX sees is listed, and the software renderer it also offers is left out.
nlohmann::json NativeSystemInspector::graphics() {
    nlohmann::json adapters = nlohmann::json::array();
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return adapters;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 description{};

        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        LARGE_INTEGER driver{};
        std::array<char, 32> driverText{};
        std::array<char, 8> vendorText{};
        std::array<char, 8> deviceText{};

        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) {
            std::snprintf(driverText.data(), driverText.size(), "%u.%u.%u.%u", HIWORD(driver.HighPart), LOWORD(driver.HighPart), HIWORD(driver.LowPart), LOWORD(driver.LowPart));
        }

        std::snprintf(vendorText.data(), vendorText.size(), "0x%04X", description.VendorId);
        std::snprintf(deviceText.data(), deviceText.size(), "0x%04X", description.DeviceId);
        const UINT vendorId = description.VendorId;
        const std::string vendor = vendorId == 0x10DE ? "NVIDIA" : vendorId == 0x1002 || vendorId == 0x1022 ? "AMD" : vendorId == 0x8086 ? "Intel" : vendorId == 0x5143 ? "Qualcomm" : "";
        adapters.push_back({{"vendor", vendor}, {"name", WindowsText::narrow(description.Description)}, {"driver", driverText.data()}, {"vendorId", vendorText.data()}, {"deviceId", deviceText.data()}, {"dedicatedMemory", description.DedicatedVideoMemory}, {"sharedMemory", description.SharedSystemMemory}, {"frequency", 0}, {"cores", 0}});
    }

    return adapters;
}

nlohmann::json NativeSystemInspector::mainboard() {
    return SmbiosTable().baseboard();
}

// A physical drive answers its identity without administrator rights, and a volume letter answers the number of the drive it lives on.
nlohmann::json NativeSystemInspector::disks() {
    nlohmann::json found = nlohmann::json::array();
    std::vector<DWORD> numbers;

    for (DWORD number = 0; number < 32; ++number) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(number);
        HANDLE drive = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

        if (drive == INVALID_HANDLE_VALUE) {
            continue;
        }

        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        std::array<std::uint8_t, 1024> descriptor{};
        DISK_GEOMETRY_EX geometry{};
        DWORD described = 0;
        DWORD measuredBytes = 0;
        const bool answered = DeviceIoControl(drive, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), descriptor.data(), static_cast<DWORD>(descriptor.size()), &described, nullptr) != 0;
        const bool measured = DeviceIoControl(drive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geometry, sizeof(geometry), &measuredBytes, nullptr) != 0;
        CloseHandle(drive);

        if (!answered || described < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            continue;
        }

        STORAGE_DEVICE_DESCRIPTOR device{};
        std::copy_n(descriptor.begin(), sizeof(device), reinterpret_cast<std::uint8_t*>(&device));
        // A text of the descriptor ends at its zero or at the last byte the device wrote, whichever comes first, so a long one is never read past what was returned.
        // clang-format off
        const auto field = [&descriptor, described](DWORD offset) {
            if (offset == 0 || offset >= described) {
                return std::string();
            }

            const auto begin = descriptor.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto end = std::find(begin, descriptor.begin() + static_cast<std::ptrdiff_t>(described), std::uint8_t{0});

            return std::string(begin, end);
        };
        // clang-format on

        const STORAGE_BUS_TYPE bus = device.BusType;
        const std::string connection = bus == BusTypeNvme ? "NVMe" : bus == BusTypeSata || bus == BusTypeAta ? "SATA" : bus == BusTypeUsb ? "USB" : bus == BusTypeSas || bus == BusTypeScsi ? "SCSI" : bus == BusTypeSd ? "SD" : bus == BusTypeMmc ? "MMC" : bus == BusTypeVirtual || bus == BusTypeFileBackedVirtual ? "Virtual" : bus == BusTypeRAID ? "RAID" : "";
        std::string vendor = field(device.VendorIdOffset);
        std::string model = field(device.ProductIdOffset);
        std::string serial = field(device.SerialNumberOffset);

        for (std::string* text : {&vendor, &model, &serial}) {
            text->erase(0, text->find_first_not_of(' '));
            text->erase(text->find_last_not_of(' ') + 1);
        }

        numbers.push_back(number);
        found.push_back({{"vendor", vendor}, {"model", model}, {"serial", serial}, {"interface", connection}, {"size", measured ? geometry.DiskSize.QuadPart : 0}, {"volumes", nlohmann::json::array()}});
    }

    std::array<wchar_t, 512> letters{};
    const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(letters.size()), letters.data());

    for (const wchar_t* root = letters.data(); length > 0 && *root != L'\0'; root += wcslen(root) + 1) {
        const UINT type = GetDriveTypeW(root);

        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
            continue;
        }

        const std::wstring volumePath = std::wstring(L"\\\\.\\") + root[0] + L":";
        HANDLE volume = CreateFileW(volumePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned = 0;
        const bool located = volume != INVALID_HANDLE_VALUE && DeviceIoControl(volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number), &returned, nullptr) != 0;

        if (volume != INVALID_HANDLE_VALUE) {
            CloseHandle(volume);
        }

        ULARGE_INTEGER available{};

        if (!located || GetDiskFreeSpaceExW(root, &available, nullptr, nullptr) == 0) {
            continue;
        }

        for (std::size_t position = 0; position < numbers.size(); ++position) {
            if (numbers[position] == number.DeviceNumber) {
                found[position]["volumes"].push_back({{"mountPoint", WindowsText::narrow(root)}, {"free", available.QuadPart}});
            }
        }
    }

    return found;
}

// Every battery the power manager exposes is asked for its tag, then for its identity, its capacity and its status under that tag.
nlohmann::json NativeSystemInspector::batteries() {
    static const GUID batteryInterface{0x72631E54, 0x78A4, 0x11D0, {0xBC, 0xF7, 0x00, 0xAA, 0x00, 0xB7, 0xB3, 0x2A}};
    nlohmann::json found = nlohmann::json::array();
    HDEVINFO devices = SetupDiGetClassDevsW(&batteryInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devices == INVALID_HANDLE_VALUE) {
        return found;
    }

    SP_DEVICE_INTERFACE_DATA deviceInterface{};
    deviceInterface.cbSize = static_cast<DWORD>(sizeof(deviceInterface));

    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(devices, nullptr, &batteryInterface, index, &deviceInterface) != 0; ++index) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &deviceInterface, nullptr, 0, &required, nullptr);

        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }

        std::vector<std::uint8_t> detailBuffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W));

        if (SetupDiGetDeviceInterfaceDetailW(devices, &deviceInterface, detail, required, nullptr, nullptr) == 0) {
            continue;
        }

        HANDLE battery = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (battery == INVALID_HANDLE_VALUE) {
            continue;
        }

        BATTERY_QUERY_INFORMATION query{};
        DWORD wait = 0;
        DWORD returned = 0;

        if (DeviceIoControl(battery, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &query.BatteryTag, sizeof(query.BatteryTag), &returned, nullptr) == 0 || query.BatteryTag == 0) {
            CloseHandle(battery);
            continue;
        }

        // clang-format off
        const auto textOf = [battery, &query](BATTERY_QUERY_INFORMATION_LEVEL level) {
            std::array<wchar_t, 128> buffer{};
            DWORD size = 0;
            query.InformationLevel = level;

            return DeviceIoControl(battery, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), buffer.data(), static_cast<DWORD>((buffer.size() - 1) * sizeof(wchar_t)), &size, nullptr) != 0 ? WindowsText::narrow(buffer.data()) : std::string();
        };
        // clang-format on

        BATTERY_INFORMATION information{};
        query.InformationLevel = BatteryInformation;
        const bool informed = DeviceIoControl(battery, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), &information, sizeof(information), &returned, nullptr) != 0;
        BATTERY_WAIT_STATUS waitStatus{};
        waitStatus.BatteryTag = query.BatteryTag;
        BATTERY_STATUS status{};
        const bool measured = DeviceIoControl(battery, IOCTL_BATTERY_QUERY_STATUS, &waitStatus, sizeof(waitStatus), &status, sizeof(status), &returned, nullptr) != 0;
        const std::string state = !measured ? "unknown" : (status.PowerState & BATTERY_CHARGING) != 0 ? "charging" : (status.PowerState & BATTERY_DISCHARGING) != 0 ? "discharging" : (status.PowerState & BATTERY_POWER_ON_LINE) != 0 ? "not-charging" : "unknown";
        const std::string technology = informed ? std::string(reinterpret_cast<const char*>(information.Chemistry), 4) : "";
        nlohmann::json entry{{"vendor", textOf(BatteryManufactureName)}, {"model", textOf(BatteryDeviceName)}, {"serial", textOf(BatterySerialNumber)}, {"technology", technology.substr(0, technology.find('\0'))}, {"state", state}};

        if (informed && measured && information.FullChargedCapacity > 0 && status.Capacity != BATTERY_UNKNOWN_CAPACITY) {
            entry["capacity"] = std::clamp(static_cast<double>(status.Capacity) / static_cast<double>(information.FullChargedCapacity), 0.0, 1.0);
        }

        CloseHandle(battery);
        found.push_back(entry);
    }

    SetupDiDestroyDeviceInfoList(devices);

    return found;
}

// An adapter is listed while it is up and is not the loopback, with the first IPv4 address and a global IPv6 address before a link local one.
nlohmann::json NativeSystemInspector::networkInterfaces() {
    nlohmann::json found = nlohmann::json::array();
    ULONG size = 16384;
    std::vector<std::uint8_t> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size) == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    }

    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size) != NO_ERROR) {
        return found;
    }

    for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->OperStatus != IfOperStatusUp) {
            continue;
        }

        std::string mac;

        for (ULONG index = 0; index < adapter->PhysicalAddressLength; ++index) {
            std::array<char, 4> part{};
            std::snprintf(part.data(), part.size(), index == 0 ? "%02x" : ":%02x", adapter->PhysicalAddress[index]);
            mac += part.data();
        }

        std::string ipv4;
        std::string ipv6;

        for (const IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            std::array<char, INET6_ADDRSTRLEN> text{};
            const sockaddr* address = unicast->Address.lpSockaddr;

            if (address->sa_family == AF_INET && ipv4.empty()) {
                inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(address)->sin_addr, text.data(), text.size());
                ipv4 = text.data();
            }

            if (address->sa_family == AF_INET6) {
                const auto* inet6 = reinterpret_cast<const sockaddr_in6*>(address);
                const bool linkLocal = IN6_IS_ADDR_LINKLOCAL(&inet6->sin6_addr);

                if (ipv6.empty() || !linkLocal) {
                    inet_ntop(AF_INET6, &inet6->sin6_addr, text.data(), text.size());
                    ipv6 = text.data();
                }
            }
        }

        found.push_back({{"index", adapter->IfIndex}, {"description", WindowsText::narrow(adapter->Description)}, {"mac", mac}, {"ipv4", ipv4}, {"ipv6", ipv6}});
    }

    return found;
}

} // namespace workpane::platform
