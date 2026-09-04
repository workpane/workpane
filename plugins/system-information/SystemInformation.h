#pragma once

#include "domain/Result.h"

#include <QDateTime>
#include <QFuture>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>
#include <optional>

namespace workpane::plugins::systeminformation {

struct ProcessorCore final {
    quint64 id{0};
    quint64 l1DataBytes{0};
    quint64 l1InstructionBytes{0};
    quint64 l2Bytes{0};
    quint64 l3Bytes{0};
    quint64 maximumFrequencyHz{0};
    bool simultaneousMultithreading{false};
};

struct Processor final {
    quint32 id{0};
    QString vendor;
    QString model;
    quint64 physicalCoreCount{0};
    quint64 logicalCoreCount{0};
    QStringList flags;
    QVector<ProcessorCore> cores;
};

struct ProcessorUsage final {
    std::optional<double> utilization;
    QVector<double> threadUtilization;
    QVector<qint64> threadFrequencyHz;
};

struct MemoryModule final {
    quint32 id{0};
    QString vendor;
    QString name;
    QString model;
    QString serialNumber;
    quint64 sizeBytes{0};
    quint64 frequencyHz{0};
};

struct Memory final {
    quint64 totalBytes{0};
    quint64 freeBytes{0};
    quint64 availableBytes{0};
    QVector<MemoryModule> modules;
};

struct OperatingSystem final {
    QString hostName;
    QString name;
    QString version;
    QString kernel;
    int architectureBits{0};
    QString byteOrder;
};

struct GraphicsProcessor final {
    quint32 id{0};
    QString vendor;
    QString name;
    QString driverVersion;
    QString vendorId;
    QString deviceId;
    quint64 dedicatedMemoryBytes{0};
    quint64 sharedMemoryBytes{0};
    quint64 frequencyHz{0};
    quint64 coreCount{0};
};

struct Mainboard final {
    QString vendor;
    QString name;
    QString version;
    QString serialNumber;
};

struct DiskVolume final {
    QString mountPoint;
    quint64 freeBytes{0};
};

struct Disk final {
    quint32 id{0};
    QString vendor;
    QString model;
    QString serialNumber;
    QString interfaceName;
    quint64 sizeBytes{0};
    QVector<DiskVolume> volumes;
};

struct Battery final {
    quint32 id{0};
    QString vendor;
    QString model;
    QString serialNumber;
    QString technology;
    QString state;
    quint64 fullEnergy{0};
    quint64 currentEnergy{0};
    std::optional<double> capacityPercent;
};

struct NetworkInterface final {
    QString index;
    QString description;
    QString macAddress;
    QString ipv4Address;
    QString ipv6Address;
};

// A screen belongs to the window system rather than to the hardware probe, so it is read on the thread that owns it.
struct Display final {
    QString name;
    int widthPixels{0};
    int heightPixels{0};
    double devicePixelRatio{0.0};
    double logicalDotsPerInch{0.0};
    double refreshHz{0.0};
    bool primary{false};
};

struct SystemSnapshot final {
    QDateTime capturedAtUtc;
    OperatingSystem operatingSystem;
    QVector<Processor> processors;
    ProcessorUsage processorUsage;
    Memory memory;
    QVector<GraphicsProcessor> graphicsProcessors;
    Mainboard mainboard;
    QVector<Disk> disks;
    QVector<Battery> batteries;
    QVector<NetworkInterface> networkInterfaces;
    QVector<Display> displays;
};

class SystemInformationProvider {
  public:
    virtual ~SystemInformationProvider() = default;
    [[nodiscard]] virtual Result<SystemSnapshot> collect(const std::atomic_bool& cancelled) = 0;
};

class HwinfoSystemInformationProvider final : public SystemInformationProvider {
  public:
    [[nodiscard]] Result<SystemSnapshot> collect(const std::atomic_bool& cancelled) override;
};

struct SystemInformationCollection final {
    QFuture<Result<SystemSnapshot>> future;
    std::shared_ptr<std::atomic_bool> cancelled;
};

class SystemCollection final {
  public:
    [[nodiscard]] static QVector<Display> connectedDisplays();
    [[nodiscard]] static SystemInformationCollection collectSystemInformation(std::shared_ptr<SystemInformationProvider> provider);
};

} // namespace workpane::plugins::systeminformation
