#include "platform/NativeSystemInspector.h"

#include "platform/macos/MacRegistryEntry.h"
#include "platform/macos/MacSysctl.h"
#include "platform/posix/NetworkInterfaceList.h"
#include "text/Integers.h"

#include <mach/mach.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace workpane::platform {

nlohmann::json NativeSystemInspector::operatingSystem() {
    static constexpr std::array<std::pair<int, const char*>, 6> releases{{{26, "Tahoe"}, {15, "Sequoia"}, {14, "Sonoma"}, {13, "Ventura"}, {12, "Monterey"}, {11, "Big Sur"}}};
    const std::string product = MacSysctl::text("kern.osproductversion");
    const auto major = static_cast<int>(text::Integers::parse(std::string_view(product).substr(0, product.find('.'))).value_or(0));
    std::string name = "macOS " + std::to_string(major);

    for (const auto& [release, marketing] : releases) {
        if (release == major) {
            name = std::string("macOS ") + marketing;
        }
    }

    std::array<char, 256> host{};
    gethostname(host.data(), host.size() - 1);
    const std::string build = MacSysctl::text("kern.osversion");
    const std::string version = build.empty() ? product : product + " (" + build + ")";
    // The kernel names its byte order by the digits of a number as memory holds it, and any other answer is unknown.
    const auto order = MacSysctl::integer("hw.byteorder");
    const char* byteOrder = order == 4321 ? "big-endian" : order == 1234 ? "little-endian" : "";

    return {{"hostName", host.data()}, {"name", name}, {"version", version}, {"kernel", MacSysctl::text("kern.ostype") + " " + MacSysctl::text("kern.osrelease")}, {"architectureBits", sizeof(void*) * 8}, {"byteOrder", byteOrder}};
}

// Apple silicon groups its cores in performance levels with their own caches, and an Intel Mac describes one level through the older names.
nlohmann::json NativeSystemInspector::processors() {
    const std::int64_t levels = MacSysctl::integer("hw.nperflevels");
    nlohmann::json cores = nlohmann::json::array();

    for (std::int64_t level = 0; level < std::max<std::int64_t>(levels, 1); ++level) {
        const std::string prefix = levels > 0 ? "hw.perflevel" + std::to_string(level) + "." : "hw.";
        const std::int64_t physical = MacSysctl::integer((prefix + "physicalcpu").c_str());
        const std::int64_t logical = MacSysctl::integer((prefix + "logicalcpu").c_str());
        const std::int64_t count = physical > 0 ? physical : MacSysctl::integer("hw.physicalcpu");

        for (std::int64_t core = 0; core < count; ++core) {
            cores.push_back({{"id", cores.size()}, {"l1Data", MacSysctl::integer((prefix + "l1dcachesize").c_str())}, {"l1Instruction", MacSysctl::integer((prefix + "l1icachesize").c_str())}, {"l2", MacSysctl::integer((prefix + "l2cachesize").c_str())}, {"l3", MacSysctl::integer((prefix + "l3cachesize").c_str())}, {"maximumFrequency", MacSysctl::integer("hw.cpufrequency_max")}, {"smt", logical > physical && physical > 0}});
        }
    }

    // An Intel processor lists its features as words, and Apple silicon answers one name per feature it implements.
    nlohmann::json flags = nlohmann::json::array();
    const std::string features = MacSysctl::text("machdep.cpu.features") + " " + MacSysctl::text("machdep.cpu.leaf7_features");
    std::string word;

    for (const char character : features + " ") {
        if (character != ' ') {
            word += character;
            continue;
        }

        if (!word.empty()) {
            flags.push_back(word);
        }

        word.clear();
    }

    static constexpr std::array<const char*, 16> armFeatures{"AES", "PMULL", "SHA1", "SHA256", "SHA512", "SHA3", "CRC32", "LSE", "LSE2", "FP16", "DotProd", "FHM", "BF16", "I8MM", "SME", "SME2"};

    for (const char* feature : armFeatures) {
        if (MacSysctl::integer((std::string("hw.optional.arm.FEAT_") + feature).c_str()) == 1) {
            flags.push_back(feature);
        }
    }

    const std::string vendor = MacSysctl::text("machdep.cpu.vendor");
    return nlohmann::json::array({{{"vendor", vendor.empty() ? "Apple" : vendor}, {"model", MacSysctl::text("machdep.cpu.brand_string")}, {"physicalCores", MacSysctl::integer("hw.physicalcpu")}, {"logicalCores", MacSysctl::integer("hw.logicalcpu")}, {"cores", cores}, {"flags", flags}}});
}

// Utilization is the share of the ticks each processor spent working between two samples taken a fifth of a second apart.
nlohmann::json NativeSystemInspector::processorUsage() {
    // clang-format off
    const auto sample = []() {
        natural_t count = 0;
        processor_info_array_t info = nullptr;
        mach_msg_type_number_t infoCount = 0;
        std::vector<std::array<std::uint64_t, 2>> ticks;

        if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &count, &info, &infoCount) != KERN_SUCCESS) {
            return ticks;
        }

        const auto* load = reinterpret_cast<const processor_cpu_load_info*>(info);

        for (natural_t index = 0; index < count; ++index) {
            const auto& state = load[index].cpu_ticks;
            const std::uint64_t working = state[CPU_STATE_USER] + state[CPU_STATE_SYSTEM] + state[CPU_STATE_NICE];
            ticks.push_back({working, working + state[CPU_STATE_IDLE]});
        }

        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info), infoCount * sizeof(integer_t));

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
        threads.push_back({{"utilization", utilization}, {"frequency", 0}});
        total += utilization;
    }

    return {{"utilization", total / static_cast<double>(before.size())}, {"threads", threads}};
}

// Memory in use is what Activity Monitor counts: application memory, wired memory and the compressor, and the rest is available.
nlohmann::json NativeSystemInspector::memory() {
    const std::int64_t total = MacSysctl::integer("hw.memsize");
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count) != KERN_SUCCESS) {
        return {{"total", total}, {"free", 0}, {"available", 0}, {"modules", nlohmann::json::array()}};
    }

    const auto page = static_cast<std::int64_t>(vm_kernel_page_size);
    const std::int64_t used = (static_cast<std::int64_t>(statistics.internal_page_count) - static_cast<std::int64_t>(statistics.purgeable_count) + static_cast<std::int64_t>(statistics.wire_count) + static_cast<std::int64_t>(statistics.compressor_page_count)) * page;
    return {{"total", total}, {"free", static_cast<std::int64_t>(statistics.free_count) * page}, {"available", std::max<std::int64_t>(0, total - used)}, {"modules", nlohmann::json::array()}};
}

// A graphics processor is an accelerator in the registry, whose PCI device names its vendor and memory on an Intel Mac.
nlohmann::json NativeSystemInspector::graphics() {
    nlohmann::json adapters = nlohmann::json::array();

    for (const auto& accelerator : MacRegistryEntry::matching("IOAccelerator")) {
        std::string model = accelerator.text("model");
        std::int64_t vendorId = 0;
        std::int64_t deviceId = 0;
        std::int64_t memory = 0;

        for (auto ancestor = accelerator.parent(); ancestor.has_value(); ancestor = ancestor->parent()) {
            if (!ancestor->conformsTo("IOPCIDevice")) {
                continue;
            }

            model = model.empty() ? ancestor->text("model") : model;
            vendorId = ancestor->number("vendor-id");
            deviceId = ancestor->number("device-id");
            memory = ancestor->number("VRAM,totalMB") * 1024 * 1024;
            break;
        }

        const std::string vendor = vendorId == 0x8086 ? "Intel" : vendorId == 0x1002 ? "AMD" : vendorId == 0x10DE ? "NVIDIA" : model.starts_with("Apple") ? "Apple" : "";
        std::array<char, 8> vendorText{};
        std::array<char, 8> deviceText{};

        if (vendorId != 0) {
            std::snprintf(vendorText.data(), vendorText.size(), "0x%04llX", static_cast<unsigned long long>(vendorId & 0xFFFF));
            std::snprintf(deviceText.data(), deviceText.size(), "0x%04llX", static_cast<unsigned long long>(deviceId & 0xFFFF));
        }

        adapters.push_back({{"vendor", vendor}, {"name", model}, {"driver", accelerator.text("IOClass")}, {"vendorId", vendorText.data()}, {"deviceId", deviceText.data()}, {"dedicatedMemory", memory}, {"sharedMemory", 0}, {"frequency", 0}, {"cores", accelerator.number("gpu-core-count")}});
    }

    return adapters;
}

nlohmann::json NativeSystemInspector::mainboard() {
    const auto platform = MacRegistryEntry::first("IOPlatformExpertDevice");

    if (!platform.has_value()) {
        return {{"vendor", ""}, {"name", ""}, {"version", ""}, {"serial", ""}};
    }

    return {{"vendor", platform->text("manufacturer")}, {"name", platform->text("model")}, {"version", platform->text("version")}, {"serial", platform->text("IOPlatformSerialNumber")}};
}

// A physical disk is the block storage device behind whole media, and Apple silicon shows one device as several namespaces that are listed once.
nlohmann::json NativeSystemInspector::disks() {
    nlohmann::json found = nlohmann::json::array();
    std::vector<std::uint64_t> devices;
    std::vector<std::vector<std::string>> names;

    for (const auto& media : MacRegistryEntry::matching("IOMedia")) {
        if (!media.flag("Whole")) {
            continue;
        }

        std::optional<MacRegistryEntry> device;
        bool synthesized = false;

        for (auto ancestor = media.parent(); ancestor.has_value(); ancestor = ancestor->parent()) {
            synthesized = synthesized || ancestor->conformsTo("AppleAPFSContainer");

            if (ancestor->conformsTo("IOBlockStorageDevice")) {
                device = std::move(ancestor);
                break;
            }
        }

        const std::string interconnect = device.has_value() ? device->nestedText("Protocol Characteristics", "Physical Interconnect") : "";

        if (synthesized || !device.has_value() || interconnect == "Virtual Interface") {
            continue;
        }

        const auto known = std::ranges::find(devices, device->identity());
        const std::int64_t size = media.number("Size");

        if (known != devices.end()) {
            const auto position = static_cast<std::size_t>(known - devices.begin());
            names[position].push_back(media.text("BSD Name"));
            found[position]["size"] = std::max(found[position]["size"].get<std::int64_t>(), size);
            continue;
        }

        const std::string product = device->nestedText("Device Characteristics", "Product Name");
        const std::string model = product.empty() ? media.name() : product;
        const std::string vendor = device->nestedText("Device Characteristics", "Vendor Name");
        devices.push_back(device->identity());
        names.push_back({media.text("BSD Name")});
        found.push_back({{"vendor", vendor.empty() && model.starts_with("APPLE") ? "Apple" : vendor}, {"model", model}, {"serial", device->nestedText("Device Characteristics", "Serial Number")}, {"interface", interconnect}, {"size", size}, {"volumes", nlohmann::json::array()}});
    }

    const int count = getfsstat(nullptr, 0, MNT_NOWAIT);
    std::vector<struct statfs> mounts(static_cast<std::size_t>(std::max(count, 0)));
    const int filled = getfsstat(mounts.data(), static_cast<int>(mounts.size() * sizeof(struct statfs)), MNT_NOWAIT);

    for (int index = 0; index < filled; ++index) {
        const auto& mount = mounts[static_cast<std::size_t>(index)];
        const std::string device = mount.f_mntfromname;

        if ((mount.f_flags & MNT_LOCAL) == 0 || (mount.f_flags & MNT_DONTBROWSE) != 0 || !device.starts_with("/dev/")) {
            continue;
        }

        // The topmost whole media above a volume is the physical disk, because an APFS container is whole media synthesized on a partition.
        std::string disk;

        for (auto entry = MacRegistryEntry::forBsdName(device.substr(5)); entry.has_value(); entry = entry->parent()) {
            if (entry->conformsTo("IOMedia") && entry->flag("Whole")) {
                disk = entry->text("BSD Name");
            }
        }

        for (std::size_t position = 0; position < names.size(); ++position) {
            if (std::ranges::find(names[position], disk) != names[position].end()) {
                found[position]["volumes"].push_back({{"mountPoint", mount.f_mntonname}, {"free", static_cast<std::int64_t>(mount.f_bavail) * static_cast<std::int64_t>(mount.f_bsize)}});
            }
        }
    }

    return found;
}

nlohmann::json NativeSystemInspector::batteries() {
    const auto battery = MacRegistryEntry::first("AppleSmartBattery");

    if (!battery.has_value() || !battery->flag("BatteryInstalled")) {
        return nlohmann::json::array();
    }

    const std::int64_t current = battery->number("CurrentCapacity");
    const std::int64_t maximum = battery->number("MaxCapacity");
    const std::string state = battery->flag("IsCharging") ? "charging" : battery->flag("ExternalConnected") ? "not-charging" : "discharging";
    nlohmann::json entry{{"vendor", battery->text("Manufacturer")}, {"model", battery->text("DeviceName")}, {"serial", battery->text("Serial")}, {"technology", ""}, {"state", state}};

    if (maximum > 0 && current >= 0 && current <= maximum) {
        entry["capacity"] = static_cast<double>(current) / static_cast<double>(maximum);
    }

    return nlohmann::json::array({entry});
}

// The address list hides the hardware address of an interface from an application, and the registry still tells it.
nlohmann::json NativeSystemInspector::networkInterfaces() {
    nlohmann::json interfaces = NetworkInterfaceList::collect();

    for (const auto& ethernet : MacRegistryEntry::matching("IOEthernetInterface")) {
        const std::string name = ethernet.text("BSD Name");
        const auto controller = ethernet.parent();
        const std::string address = controller.has_value() ? controller->hardwareAddress("IOMACAddress") : "";

        for (auto& interface : interfaces) {
            if (interface["description"] == name && !address.empty()) {
                interface["mac"] = address;
            }
        }
    }

    return interfaces;
}

} // namespace workpane::platform
