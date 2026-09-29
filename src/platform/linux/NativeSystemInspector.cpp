#include "platform/NativeSystemInspector.h"

#include "platform/linux/LinuxSysfs.h"
#include "platform/posix/NetworkInterfaceList.h"
#include "text/Integers.h"

#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace workpane::platform {

nlohmann::json NativeSystemInspector::operatingSystem() {
    std::ifstream release("/etc/os-release");
    std::map<std::string, std::string> fields;

    for (std::string line; std::getline(release, line);) {
        const std::size_t separator = line.find('=');

        if (separator == std::string::npos) {
            continue;
        }

        std::string value = line.substr(separator + 1);

        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }

        fields[line.substr(0, separator)] = value;
    }

    utsname system{};
    uname(&system);
    std::array<char, 256> host{};
    gethostname(host.data(), host.size() - 1);
    const std::string name = fields.contains("PRETTY_NAME") ? fields["PRETTY_NAME"] : fields.contains("NAME") ? fields["NAME"] : "Linux";
    const std::string version = fields.contains("VERSION") ? fields["VERSION"] : fields["VERSION_ID"];

    return {{"hostName", host.data()}, {"name", name}, {"version", version}, {"kernel", std::string(system.sysname) + " " + system.release}, {"architectureBits", sizeof(void*) * 8}, {"byteOrder", std::endian::native == std::endian::big ? "big-endian" : "little-endian"}};
}

// Each processor entry names its socket and core, and its caches and frequency are read from the topology the kernel publishes per logical processor.
nlohmann::json NativeSystemInspector::processors() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::vector<std::map<std::string, std::string>> entries(1);

    for (std::string line; std::getline(cpuinfo, line);) {
        if (line.find_first_not_of(" \t") == std::string::npos) {
            entries.emplace_back();
            continue;
        }

        const std::size_t separator = line.find(':');

        if (separator == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, separator);
        key.erase(key.find_last_not_of(" \t") + 1);
        const std::size_t start = line.find_first_not_of(" \t", separator + 1);
        entries.back()[key] = start == std::string::npos ? "" : line.substr(start);
    }

    // An ARM processor names no model, so its implementer and part codes are read against the designs this product knows by name.
    static constexpr std::array<std::pair<int, const char*>, 13> implementers{{{0x41, "ARM"}, {0x42, "Broadcom"}, {0x43, "Cavium"}, {0x46, "Fujitsu"}, {0x48, "HiSilicon"}, {0x4E, "NVIDIA"}, {0x50, "Applied Micro"}, {0x51, "Qualcomm"}, {0x53, "Samsung"}, {0x56, "Marvell"}, {0x61, "Apple"}, {0x69, "Intel"}, {0xC0, "Ampere"}}};
    static constexpr std::array<std::pair<int, const char*>, 20> parts{{{0xD03, "Cortex-A53"}, {0xD04, "Cortex-A35"}, {0xD05, "Cortex-A55"}, {0xD07, "Cortex-A57"}, {0xD08, "Cortex-A72"}, {0xD09, "Cortex-A73"}, {0xD0A, "Cortex-A75"}, {0xD0B, "Cortex-A76"}, {0xD0C, "Neoverse-N1"}, {0xD0D, "Cortex-A77"}, {0xD40, "Neoverse-V1"}, {0xD41, "Cortex-A78"}, {0xD44, "Cortex-X1"}, {0xD46, "Cortex-A510"}, {0xD47, "Cortex-A710"}, {0xD48, "Cortex-X2"}, {0xD49, "Neoverse-N2"}, {0xD4F, "Neoverse-V2"}, {0xD80, "Cortex-A520"}, {0xD81, "Cortex-A720"}}};
    // clang-format off
    const auto armImplementer = [](const std::string& code) {
        const auto value = text::Integers::parse(code, 16).value_or(0);
        const auto found = std::ranges::find_if(implementers, [value](const auto& known) { return known.first == value; });
        return found == implementers.end() ? code : std::string(found->second);
    };

    const auto armPart = [](const std::map<std::string, std::string>& entry) {
        const auto implementer = entry.contains("CPU implementer") ? text::Integers::parse(entry.at("CPU implementer"), 16).value_or(0) : 0;
        const auto value = text::Integers::parse(entry.at("CPU part"), 16).value_or(0);
        const auto found = std::ranges::find_if(parts, [value](const auto& known) { return known.first == value; });
        return implementer == 0x41 && found != parts.end() ? std::string(found->second) : std::string();
    };
    // clang-format on

    std::map<std::string, nlohmann::json> sockets;
    std::map<std::string, std::set<std::string>> socketCores;
    std::string model;
    std::string vendor;
    std::string flags;

    for (const auto& entry : entries) {
        if (!entry.contains("processor")) {
            model = entry.contains("Hardware") && model.empty() ? entry.at("Hardware") : model;
            continue;
        }

        const std::string socket = entry.contains("physical id") ? entry.at("physical id") : "0";
        const std::string core = entry.contains("core id") ? entry.at("core id") : entry.at("processor");
        const std::filesystem::path topology = "/sys/devices/system/cpu/cpu" + entry.at("processor");
        nlohmann::json& processor = sockets[socket];
        model = entry.contains("model name") ? entry.at("model name") : entry.contains("CPU part") && model.empty() ? armPart(entry) : model;
        vendor = entry.contains("vendor_id") ? entry.at("vendor_id") : entry.contains("CPU implementer") ? armImplementer(entry.at("CPU implementer")) : vendor;
        flags = entry.contains("flags") ? entry.at("flags") : entry.contains("Features") ? entry.at("Features") : flags;

        if (processor.is_null()) {
            processor = {{"cores", nlohmann::json::array()}, {"logicalCores", 0}};
        }

        processor["logicalCores"] = processor["logicalCores"].get<int>() + 1;

        if (!socketCores[socket].insert(core).second) {
            continue;
        }

        std::array<std::int64_t, 4> caches{};

        for (int index = 0; index < 4; ++index) {
            const std::filesystem::path cache = topology / "cache" / ("index" + std::to_string(index));
            const std::int64_t level = LinuxSysfs::integer(cache / "level");
            const std::string type = LinuxSysfs::text(cache / "type");
            const std::size_t slot = level == 1 ? (type == "Instruction" ? 1 : 0) : level == 2 ? 2 : 3;

            if (level > 0) {
                caches[slot] = LinuxSysfs::size(cache / "size");
            }
        }

        const std::string siblings = LinuxSysfs::text(topology / "topology" / "thread_siblings_list");
        processor["cores"].push_back({{"id", text::Integers::parse(core).value_or(0)}, {"l1Data", caches[0]}, {"l1Instruction", caches[1]}, {"l2", caches[2]}, {"l3", caches[3]}, {"maximumFrequency", LinuxSysfs::integer(topology / "cpufreq" / "cpuinfo_max_freq") * 1000}, {"smt", siblings.find_first_of(",-") != std::string::npos}});
    }

    nlohmann::json found = nlohmann::json::array();
    nlohmann::json featureList = nlohmann::json::array();
    std::istringstream words(flags);

    for (std::string word; words >> word;) {
        featureList.push_back(word);
    }

    for (auto& [socket, processor] : sockets) {
        processor["vendor"] = vendor;
        processor["model"] = model;
        processor["physicalCores"] = processor["cores"].size();
        processor["flags"] = featureList;
        found.push_back(processor);
    }

    return found;
}

// Utilization is the share of the time each processor spent working between two samples of the kernel counters a fifth of a second apart.
nlohmann::json NativeSystemInspector::processorUsage() {
    // clang-format off
    const auto sample = []() {
        std::ifstream stat("/proc/stat");
        std::vector<std::array<std::uint64_t, 2>> ticks;

        for (std::string line; std::getline(stat, line);) {
            if (!line.starts_with("cpu") || line.starts_with("cpu ")) {
                continue;
            }

            std::istringstream fields(line.substr(line.find(' ')));
            std::array<std::uint64_t, 8> values{};

            for (auto& value : values) {
                fields >> value;
            }

            const std::uint64_t working = values[0] + values[1] + values[2] + values[5] + values[6] + values[7];
            ticks.push_back({working, working + values[3] + values[4]});
        }

        return ticks;
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
        const double elapsed = static_cast<double>(after[index][1] - before[index][1]);
        const double utilization = elapsed > 0.0 ? static_cast<double>(after[index][0] - before[index][0]) / elapsed : 0.0;
        const std::int64_t frequency = LinuxSysfs::integer("/sys/devices/system/cpu/cpu" + std::to_string(index) + "/cpufreq/scaling_cur_freq") * 1000;
        threads.push_back({{"utilization", utilization}, {"frequency", frequency}});
        total += utilization;
    }

    return {{"utilization", total / static_cast<double>(before.size())}, {"threads", threads}};
}

nlohmann::json NativeSystemInspector::memory() {
    std::ifstream meminfo("/proc/meminfo");
    std::map<std::string, std::int64_t> values;

    for (std::string line; std::getline(meminfo, line);) {
        const std::size_t separator = line.find(':');

        if (separator != std::string::npos) {
            values[line.substr(0, separator)] = text::Integers::leading(std::string_view(line).substr(separator + 1)).value_or(0) * 1024;
        }
    }

    return {{"total", values["MemTotal"]}, {"free", values["MemFree"]}, {"available", values["MemAvailable"]}, {"modules", nlohmann::json::array()}};
}

// A graphics adapter is a card of the rendering manager, identified by its PCI vendor and device and by the driver bound to it.
nlohmann::json NativeSystemInspector::graphics() {
    nlohmann::json adapters = nlohmann::json::array();
    std::error_code failure;
    std::vector<std::filesystem::path> cards;

    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", failure)) {
        const std::string name = entry.path().filename().string();

        if (name.starts_with("card") && name.find('-') == std::string::npos) {
            cards.push_back(entry.path());
        }
    }

    std::ranges::sort(cards);

    for (const auto& card : cards) {
        const std::filesystem::path device = card / "device";
        const std::string vendorText = LinuxSysfs::text(device / "vendor");
        const std::string deviceText = LinuxSysfs::text(device / "device");
        const auto vendorId = text::Integers::parse(vendorText, 16).value_or(0);
        const auto deviceId = text::Integers::parse(deviceText, 16).value_or(0);
        const std::string vendor = vendorId == 0x10DE ? "NVIDIA" : vendorId == 0x1002 ? "AMD" : vendorId == 0x8086 ? "Intel" : vendorId == 0x1AF4 ? "Red Hat" : vendorId == 0x15AD ? "VMware" : "";
        const std::filesystem::path driver = std::filesystem::read_symlink(device / "driver", failure);
        // A code holds the prefix and every hexadecimal digit an unsigned long may need, so the compiler proves it is never cut.
        std::array<char, 2 + sizeof(unsigned long) * 2 + 1> vendorCode{};
        std::array<char, 2 + sizeof(unsigned long) * 2 + 1> deviceCode{};

        if (vendorId != 0) {
            std::snprintf(vendorCode.data(), vendorCode.size(), "0x%04lX", static_cast<unsigned long>(vendorId));
            std::snprintf(deviceCode.data(), deviceCode.size(), "0x%04lX", static_cast<unsigned long>(deviceId));
        }

        adapters.push_back({{"vendor", vendor}, {"name", LinuxSysfs::text(device / "product_name")}, {"driver", driver.filename().string()}, {"vendorId", vendorCode.data()}, {"deviceId", deviceCode.data()}, {"dedicatedMemory", LinuxSysfs::integer(device / "mem_info_vram_total")}, {"sharedMemory", 0}, {"frequency", LinuxSysfs::integer(card / "gt_max_freq_mhz") * 1000000}, {"cores", 0}});
    }

    return adapters;
}

// A board without firmware tables, such as most ARM boards, names itself in its device tree instead.
nlohmann::json NativeSystemInspector::mainboard() {
    const std::filesystem::path table = std::filesystem::exists("/sys/devices/virtual/dmi/id") ? "/sys/devices/virtual/dmi/id" : "/sys/class/dmi/id";
    const std::string name = LinuxSysfs::text(table / "board_name");
    return {{"vendor", LinuxSysfs::text(table / "board_vendor")}, {"name", name.empty() ? LinuxSysfs::text("/proc/device-tree/model") : name}, {"version", LinuxSysfs::text(table / "board_version")}, {"serial", LinuxSysfs::text(table / "board_serial")}};
}

// A disk is a block device that is not a loop, memory or mapped device, and a mounted partition of it is one of its volumes.
nlohmann::json NativeSystemInspector::disks() {
    nlohmann::json found = nlohmann::json::array();
    std::vector<std::string> names;
    std::error_code failure;

    for (const auto& entry : std::filesystem::directory_iterator("/sys/block", failure)) {
        const std::string name = entry.path().filename().string();

        if (name.starts_with("loop") || name.starts_with("ram") || name.starts_with("zram") || name.starts_with("dm-") || name.starts_with("nbd")) {
            continue;
        }

        const std::string path = std::filesystem::canonical(entry.path(), failure).string();
        const std::string interface = path.find("/nvme") != std::string::npos ? "NVMe" : path.find("/usb") != std::string::npos ? "USB" : path.find("/ata") != std::string::npos ? "SATA" : path.find("/virtio") != std::string::npos ? "VirtIO" : path.find("/mmc") != std::string::npos ? "SD/MMC" : "SCSI";
        const std::filesystem::path device = entry.path() / "device";
        names.push_back(name);
        found.push_back({{"vendor", LinuxSysfs::text(device / "vendor")}, {"model", LinuxSysfs::text(device / "model")}, {"serial", LinuxSysfs::text(device / "serial")}, {"interface", interface}, {"size", LinuxSysfs::integer(entry.path() / "size") * 512}, {"volumes", nlohmann::json::array()}});
    }

    std::ifstream mounts("/proc/mounts");

    for (std::string line; std::getline(mounts, line);) {
        std::istringstream fields(line);
        std::string device;
        std::string mountPoint;
        fields >> device >> mountPoint;

        if (!device.starts_with("/dev/")) {
            continue;
        }

        const std::string partition = device.substr(5);

        for (std::size_t position = 0; position < names.size(); ++position) {
            struct statvfs space{};

            if (!std::filesystem::exists("/sys/block/" + names[position] + "/" + partition) && partition != names[position]) {
                continue;
            }

            if (statvfs(mountPoint.c_str(), &space) == 0) {
                found[position]["volumes"].push_back({{"mountPoint", mountPoint}, {"free", static_cast<std::int64_t>(space.f_bavail) * static_cast<std::int64_t>(space.f_frsize)}});
            }
        }
    }

    return found;
}

nlohmann::json NativeSystemInspector::batteries() {
    nlohmann::json found = nlohmann::json::array();
    std::error_code failure;

    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/power_supply", failure)) {
        if (LinuxSysfs::text(entry.path() / "type") != "Battery") {
            continue;
        }

        const std::string status = LinuxSysfs::text(entry.path() / "status");
        const std::string state = status == "Charging" ? "charging" : status == "Discharging" ? "discharging" : status == "Not charging" || status == "Full" ? "not-charging" : "unknown";
        nlohmann::json battery{{"vendor", LinuxSysfs::text(entry.path() / "manufacturer")}, {"model", LinuxSysfs::text(entry.path() / "model_name")}, {"serial", LinuxSysfs::text(entry.path() / "serial_number")}, {"technology", LinuxSysfs::text(entry.path() / "technology")}, {"state", state}};
        const std::string capacity = LinuxSysfs::text(entry.path() / "capacity");

        if (!capacity.empty()) {
            battery["capacity"] = std::clamp(static_cast<double>(LinuxSysfs::integer(entry.path() / "capacity")) / 100.0, 0.0, 1.0);
        }

        found.push_back(battery);
    }

    return found;
}

nlohmann::json NativeSystemInspector::networkInterfaces() {
    return NetworkInterfaceList::collect();
}

} // namespace workpane::platform
