#include "SystemInformation.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <QtConcurrentRun>

#include <hwinfo/battery.h>
#include <hwinfo/cpu.h>
#include <hwinfo/disk.h>
#include <hwinfo/gpu.h>
#include <hwinfo/mainboard.h>
#include <hwinfo/monitoring/cpu.h>
#include <hwinfo/monitoring/disk.h>
#include <hwinfo/network.h>
#include <hwinfo/os.h>
#include <hwinfo/ram.h>
#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace workpane::plugins::systeminformation {

class SystemInformationHelper final {
  public:
    static QString fromNativeString(const std::string& value);
    static QString diskInterfaceName(hwinfo::Disk::Interface value);
    static QString batteryStateName(const hwinfo::Battery& battery);
    static quint64 totalMemoryBytes(const hwinfo::Memory& memory);
    static Result<SystemSnapshot> cancelledCollection();
    static bool isCancelled(const std::atomic_bool& cancelled);
};

// Platform buffers carry fixed-size padding, so every native value keeps only printable characters.
QString SystemInformationHelper::fromNativeString(const std::string& value) {
    if (value.empty() || value == "<unknown>") {
        return {};
    }

    QString text = QString::fromStdString(value);
    // clang-format off
    text.removeIf([](QChar character) { return !character.isPrint(); });
    // clang-format on
    return text.trimmed();
}

QString SystemInformationHelper::diskInterfaceName(hwinfo::Disk::Interface value) {
    std::ostringstream stream;
    stream << value;
    return fromNativeString(stream.str());
}

QString SystemInformationHelper::batteryStateName(const hwinfo::Battery& battery) {
    if (battery.charging()) {
        return QStringLiteral("charging");
    }
    if (battery.discharging()) {
        return QStringLiteral("discharging");
    }

    return QStringLiteral("unknown");
}

// The memory modules are not enumerated on macOS, so the installed size comes from the kernel rather than from their sum.
quint64 SystemInformationHelper::totalMemoryBytes(const hwinfo::Memory& memory) {
#if defined(Q_OS_MACOS)
    Q_UNUSED(memory);
    quint64 physicalBytes = 0;
    size_t valueSize = sizeof(physicalBytes);

    if (sysctlbyname("hw.memsize", &physicalBytes, &valueSize, nullptr, 0) != 0) {
        return 0;
    }

    return physicalBytes;
#else
    return memory.size();
#endif
}

Result<SystemSnapshot> SystemInformationHelper::cancelledCollection() {
    return Result<SystemSnapshot>::failure({"systeminformation_collection_cancelled", "System information collection was cancelled", {}});
}

bool SystemInformationHelper::isCancelled(const std::atomic_bool& cancelled) {
    return cancelled.load(std::memory_order_relaxed);
}

Result<SystemSnapshot> HwinfoSystemInformationProvider::collect(const std::atomic_bool& cancelled) {
    SystemSnapshot snapshot;
    snapshot.capturedAtUtc = QDateTime::currentDateTimeUtc();

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const hwinfo::OS nativeOperatingSystem;
    snapshot.operatingSystem.hostName = QSysInfo::machineHostName().trimmed();
    snapshot.operatingSystem.name = SystemInformationHelper::fromNativeString(nativeOperatingSystem.name());
    snapshot.operatingSystem.version = SystemInformationHelper::fromNativeString(nativeOperatingSystem.version());
    snapshot.operatingSystem.kernel = SystemInformationHelper::fromNativeString(nativeOperatingSystem.kernel());
    snapshot.operatingSystem.architectureBits = nativeOperatingSystem.is64bit() ? 64 : nativeOperatingSystem.is32bit() ? 32 : 0;
    snapshot.operatingSystem.byteOrder = nativeOperatingSystem.isLittleEndian() ? QStringLiteral("little-endian") : nativeOperatingSystem.isBigEndian() ? QStringLiteral("big-endian") : QString{};

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const auto nativeProcessors = hwinfo::getAllCPUs();
    snapshot.processors.reserve(static_cast<qsizetype>(nativeProcessors.size()));

    for (const auto& nativeProcessor : nativeProcessors) {
        if (SystemInformationHelper::isCancelled(cancelled)) {
            return SystemInformationHelper::cancelledCollection();
        }

        Processor processor;
        processor.id = nativeProcessor.id();
        processor.vendor = SystemInformationHelper::fromNativeString(nativeProcessor.vendor());
        processor.model = SystemInformationHelper::fromNativeString(nativeProcessor.modelName());
        processor.physicalCoreCount = nativeProcessor.numPhysicalCores();
        processor.logicalCoreCount = nativeProcessor.numLogicalCores();
        for (const auto& flag : nativeProcessor.flags()) {
            const QString value = SystemInformationHelper::fromNativeString(flag);
            if (!value.isEmpty()) {
                processor.flags.append(value);
            }
        }
        processor.cores.reserve(static_cast<qsizetype>(nativeProcessor.cores().size()));
        for (const auto& nativeCore : nativeProcessor.cores()) {
            processor.cores.append({nativeCore.id, nativeCore.cache.l1_data, nativeCore.cache.l1_instruction, nativeCore.cache.l2, nativeCore.cache.l3, nativeCore.max_frequency_hz, nativeCore.smt});
        }
        snapshot.processors.append(std::move(processor));
    }

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)

    if (!snapshot.processors.isEmpty() && !SystemInformationHelper::isCancelled(cancelled)) {
        const auto liveProcessor = hwinfo::monitoring::cpu::fetch(std::chrono::milliseconds(200));
        if (std::isfinite(liveProcessor.utilization) && liveProcessor.utilization >= 0.0 && liveProcessor.utilization <= 1.0) {
            snapshot.processorUsage.utilization = liveProcessor.utilization;
        }
        snapshot.processorUsage.threadUtilization.reserve(static_cast<qsizetype>(liveProcessor.thread_utilization.size()));
        for (const double utilization : liveProcessor.thread_utilization) {
            if (std::isfinite(utilization) && utilization >= 0.0 && utilization <= 1.0) {
                snapshot.processorUsage.threadUtilization.append(utilization);
            }
        }
        snapshot.processorUsage.threadFrequencyHz.reserve(static_cast<qsizetype>(liveProcessor.thread_frequency_hz.size()));
        for (const qint64 frequency : liveProcessor.thread_frequency_hz) {
            snapshot.processorUsage.threadFrequencyHz.append(frequency);
        }
    }

#endif

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const hwinfo::Memory nativeMemory;
    snapshot.memory.totalBytes = SystemInformationHelper::totalMemoryBytes(nativeMemory);
    snapshot.memory.freeBytes = nativeMemory.free();
    snapshot.memory.availableBytes = nativeMemory.available();
    snapshot.memory.modules.reserve(static_cast<qsizetype>(nativeMemory.modules().size()));

    for (const auto& nativeModule : nativeMemory.modules()) {
        if (SystemInformationHelper::isCancelled(cancelled)) {
            return SystemInformationHelper::cancelledCollection();
        }

        snapshot.memory.modules.append({nativeModule.id, SystemInformationHelper::fromNativeString(nativeModule.vendor), SystemInformationHelper::fromNativeString(nativeModule.name), SystemInformationHelper::fromNativeString(nativeModule.model), SystemInformationHelper::fromNativeString(nativeModule.serial_number), nativeModule._size_bytes, nativeModule.frequency_hz});
    }

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const auto nativeGraphicsProcessors = hwinfo::getAllGPUs();
    snapshot.graphicsProcessors.reserve(static_cast<qsizetype>(nativeGraphicsProcessors.size()));

    for (const auto& nativeGraphicsProcessor : nativeGraphicsProcessors) {
        if (SystemInformationHelper::isCancelled(cancelled)) {
            return SystemInformationHelper::cancelledCollection();
        }

        snapshot.graphicsProcessors.append({nativeGraphicsProcessor.id(), SystemInformationHelper::fromNativeString(nativeGraphicsProcessor.vendor()), SystemInformationHelper::fromNativeString(nativeGraphicsProcessor.name()), SystemInformationHelper::fromNativeString(nativeGraphicsProcessor.driverVersion()), SystemInformationHelper::fromNativeString(nativeGraphicsProcessor.vendor_id()), SystemInformationHelper::fromNativeString(nativeGraphicsProcessor.device_id()), nativeGraphicsProcessor.dedicated_memory_Bytes(), nativeGraphicsProcessor.shared_memory_Bytes(), nativeGraphicsProcessor.frequency_hz(), nativeGraphicsProcessor.num_cores()});
    }

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const hwinfo::MainBoard nativeMainboard;
    snapshot.mainboard = {SystemInformationHelper::fromNativeString(nativeMainboard.vendor()), SystemInformationHelper::fromNativeString(nativeMainboard.name()), SystemInformationHelper::fromNativeString(nativeMainboard.version()), SystemInformationHelper::fromNativeString(nativeMainboard.serialNumber())};

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const auto nativeDisks = hwinfo::getAllDisks();
    snapshot.disks.reserve(static_cast<qsizetype>(nativeDisks.size()));

    for (const auto& nativeDisk : nativeDisks) {
        if (SystemInformationHelper::isCancelled(cancelled)) {
            return SystemInformationHelper::cancelledCollection();
        }

        Disk disk;
        disk.id = nativeDisk.id();
        disk.vendor = SystemInformationHelper::fromNativeString(nativeDisk.vendor());
        disk.model = SystemInformationHelper::fromNativeString(nativeDisk.model());
        disk.serialNumber = SystemInformationHelper::fromNativeString(nativeDisk.serial_number());
        disk.interfaceName = SystemInformationHelper::diskInterfaceName(nativeDisk.disk_interface());
        disk.sizeBytes = nativeDisk.size();
        disk.volumes.reserve(static_cast<qsizetype>(nativeDisk.mount_points().size()));
        for (const auto& mountPoint : nativeDisk.mount_points()) {
            if (SystemInformationHelper::isCancelled(cancelled)) {
                return SystemInformationHelper::cancelledCollection();
            }

            const auto volume = hwinfo::monitoring::disk::fetch(mountPoint);
            disk.volumes.append({SystemInformationHelper::fromNativeString(volume.mount_point), volume.free_bytes});
        }
        snapshot.disks.append(std::move(disk));
    }

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const auto nativeBatteries = hwinfo::getAllBatteries();
    snapshot.batteries.reserve(static_cast<qsizetype>(nativeBatteries.size()));

    for (const auto& nativeBattery : nativeBatteries) {
        if (SystemInformationHelper::isCancelled(cancelled)) {
            return SystemInformationHelper::cancelledCollection();
        }

        Battery battery;
        battery.id = nativeBattery.id();
        battery.vendor = SystemInformationHelper::fromNativeString(nativeBattery.vendor());
        battery.model = SystemInformationHelper::fromNativeString(nativeBattery.model());
        battery.serialNumber = SystemInformationHelper::fromNativeString(nativeBattery.serialNumber());
        battery.technology = SystemInformationHelper::fromNativeString(nativeBattery.technology());
        battery.state = SystemInformationHelper::batteryStateName(nativeBattery);
        battery.fullEnergy = nativeBattery.energyFull();
        battery.currentEnergy = nativeBattery.energyNow();
        const double capacity = nativeBattery.capacity();
        if (nativeBattery.energyFull() > 0 && std::isfinite(capacity) && capacity >= 0.0 && capacity <= 100.0) {
            battery.capacityPercent = capacity;
        }
        snapshot.batteries.append(std::move(battery));
    }

    if (SystemInformationHelper::isCancelled(cancelled)) {
        return SystemInformationHelper::cancelledCollection();
    }

    const auto nativeNetworkInterfaces = hwinfo::getAllNetworks();
    snapshot.networkInterfaces.reserve(static_cast<qsizetype>(nativeNetworkInterfaces.size()));

    for (const auto& nativeNetworkInterface : nativeNetworkInterfaces) {
        if (SystemInformationHelper::isCancelled(cancelled)) {
            return SystemInformationHelper::cancelledCollection();
        }

        snapshot.networkInterfaces.append({SystemInformationHelper::fromNativeString(nativeNetworkInterface.interfaceIndex()), SystemInformationHelper::fromNativeString(nativeNetworkInterface.description()), SystemInformationHelper::fromNativeString(nativeNetworkInterface.mac()), SystemInformationHelper::fromNativeString(nativeNetworkInterface.ip4()), SystemInformationHelper::fromNativeString(nativeNetworkInterface.ip6())});
    }

    return Result<SystemSnapshot>::success(std::move(snapshot));
}

QVector<Display> SystemCollection::connectedDisplays() {
    QVector<Display> displays;
    const QScreen* primary = QGuiApplication::primaryScreen();

    for (const QScreen* screen : QGuiApplication::screens()) {
        if (screen == nullptr) {
            continue;
        }

        const QSize size = screen->size();
        displays.append({screen->name(), size.width(), size.height(), screen->devicePixelRatio(), screen->logicalDotsPerInch(), screen->refreshRate(), screen == primary});
    }

    return displays;
}

SystemInformationCollection SystemCollection::collectSystemInformation(std::shared_ptr<SystemInformationProvider> provider) {
    auto cancelled = std::make_shared<std::atomic_bool>(false);

    if (provider == nullptr) {
        return {QtFuture::makeReadyValueFuture(Result<SystemSnapshot>::failure({"systeminformation_provider_unavailable", "The System Information provider is unavailable", {}})), std::move(cancelled)};
    }
    // clang-format off
    auto future = QtConcurrent::run([provider = std::move(provider), cancelled]() { try { return provider->collect(*cancelled); } catch (const std::exception& exception) { return Result<SystemSnapshot>::failure({"systeminformation_collection_failed", "System information could not be collected", QString::fromUtf8(exception.what())}); } });
    // clang-format on
    return {std::move(future), std::move(cancelled)};
}

} // namespace workpane::plugins::systeminformation
