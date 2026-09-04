#include "SystemInformationView.h"

#include "SystemInformationPlugin.h"
#include "plugins/PluginInterface.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace workpane::plugins::systeminformation {

struct Section final {
    QFrame* frame{nullptr};
    QVBoxLayout* body{nullptr};
};

class SystemInformationViewHelper final {
  public:
    static QString availableText(PluginHost& host, const QString& value);
    static QString formatInteger(PluginHost& host, quint64 value);
    static QString formatBytes(PluginHost& host, quint64 bytes);
    static QString formatFrequency(PluginHost& host, quint64 frequencyHz);
    static QString formatPercent(double ratio);
    static QString translatedValue(PluginHost& host, const QString& value);
    static Section createSection(QWidget* parent, PluginHost& host, const QString& titleKey, ui::IconName iconName);
    static void addDeviceTitle(Section& section, const QString& title);
    static void addRow(Section& section, PluginHost& host, const QString& labelKey, const QString& value);
    static void addProgress(Section& section, PluginHost& host, const QString& labelKey, double ratio);
    static void addEmptyState(Section& section, PluginHost& host);
    static QString processorCoreDetails(PluginHost& host, const Processor& processor);
    static QString processorThreadDetails(PluginHost& host, const ProcessorUsage& usage);
};

QString SystemInformationViewHelper::availableText(PluginHost& host, const QString& value) {
    return value.isEmpty() ? host.translate(QStringLiteral("system-information.common.unavailable")) : value;
}

QString SystemInformationViewHelper::formatInteger(PluginHost& host, quint64 value) {
    return value == 0 ? host.translate(QStringLiteral("system-information.common.unavailable")) : QLocale::system().toString(value);
}

QString SystemInformationViewHelper::formatBytes(PluginHost& host, quint64 bytes) {
    if (bytes == 0) {
        return host.translate(QStringLiteral("system-information.common.unavailable"));
    }

    static constexpr quint64 kibibyte = 1024;
    static constexpr quint64 mebibyte = kibibyte * 1024;
    static constexpr quint64 gibibyte = mebibyte * 1024;
    static constexpr quint64 tebibyte = gibibyte * 1024;
    const QLocale locale = QLocale::system();

    if (bytes >= tebibyte) {
        return QStringLiteral("%1 TiB").arg(locale.toString(static_cast<double>(bytes) / static_cast<double>(tebibyte), 'f', 2));
    }
    if (bytes >= gibibyte) {
        return QStringLiteral("%1 GiB").arg(locale.toString(static_cast<double>(bytes) / static_cast<double>(gibibyte), 'f', 2));
    }
    if (bytes >= mebibyte) {
        return QStringLiteral("%1 MiB").arg(locale.toString(static_cast<double>(bytes) / static_cast<double>(mebibyte), 'f', 1));
    }
    if (bytes >= kibibyte) {
        return QStringLiteral("%1 KiB").arg(locale.toString(static_cast<double>(bytes) / static_cast<double>(kibibyte), 'f', 1));
    }

    return QStringLiteral("%1 B").arg(locale.toString(bytes));
}

QString SystemInformationViewHelper::formatFrequency(PluginHost& host, quint64 frequencyHz) {
    if (frequencyHz == 0) {
        return host.translate(QStringLiteral("system-information.common.unavailable"));
    }

    const QLocale locale = QLocale::system();

    if (frequencyHz >= 1000000000ULL) {
        return QStringLiteral("%1 GHz").arg(locale.toString(static_cast<double>(frequencyHz) / 1000000000.0, 'f', 2));
    }

    return QStringLiteral("%1 MHz").arg(locale.toString(static_cast<double>(frequencyHz) / 1000000.0, 'f', 0));
}

QString SystemInformationViewHelper::formatPercent(double ratio) {
    return QStringLiteral("%1%").arg(QLocale::system().toString(ratio * 100.0, 'f', 1));
}

QString SystemInformationViewHelper::translatedValue(PluginHost& host, const QString& value) {
    if (value == QStringLiteral("little-endian")) {
        return host.translate(QStringLiteral("system-information.common.little-endian"));
    }
    if (value == QStringLiteral("big-endian")) {
        return host.translate(QStringLiteral("system-information.common.big-endian"));
    }
    if (value == QStringLiteral("charging")) {
        return host.translate(QStringLiteral("system-information.common.charging"));
    }
    if (value == QStringLiteral("discharging")) {
        return host.translate(QStringLiteral("system-information.common.discharging"));
    }
    if (value == QStringLiteral("unknown")) {
        return host.translate(QStringLiteral("system-information.common.unknown"));
    }

    return availableText(host, value);
}

Section SystemInformationViewHelper::createSection(QWidget* parent, PluginHost& host, const QString& titleKey, ui::IconName iconName) {
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("systemInformationCard"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    auto* heading = new QWidget(frame);
    auto* headingLayout = new QHBoxLayout(heading);
    headingLayout->setContentsMargins(0, 0, 0, 2);
    headingLayout->setSpacing(7);
    auto* icon = new QLabel(heading);
    icon->setPixmap(ui::IconCatalog::icon(iconName, host.theme()).pixmap(18, 18));
    auto* title = new QLabel(host.translate(titleKey), heading);
    title->setObjectName(QStringLiteral("systemInformationSectionTitle"));
    title->setFont(host.theme().font(ui::ThemeFont::SectionTitle));
    headingLayout->addWidget(icon);
    headingLayout->addWidget(title);
    headingLayout->addStretch();
    layout->addWidget(heading);
    return {frame, layout};
}

void SystemInformationViewHelper::addDeviceTitle(Section& section, const QString& title) {
    auto* label = new QLabel(title, section.frame);
    label->setObjectName(QStringLiteral("systemInformationDeviceTitle"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    section.body->addWidget(label);
}

void SystemInformationViewHelper::addRow(Section& section, PluginHost& host, const QString& labelKey, const QString& value) {
    auto* row = new QWidget(section.frame);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* label = new QLabel(host.translate(labelKey), row);
    label->setObjectName(QStringLiteral("systemInformationField"));
    label->setMinimumWidth(150);
    auto* content = new QLabel(value, row);
    content->setObjectName(QStringLiteral("systemInformationValue"));
    content->setTextInteractionFlags(Qt::TextSelectableByMouse);
    content->setWordWrap(true);
    layout->addWidget(label);
    layout->addWidget(content, 1);
    section.body->addWidget(row);
}

void SystemInformationViewHelper::addProgress(Section& section, PluginHost& host, const QString& labelKey, double ratio) {
    const double boundedRatio = std::clamp(ratio, 0.0, 1.0);
    auto* row = new QWidget(section.frame);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* label = new QLabel(host.translate(labelKey), row);
    label->setObjectName(QStringLiteral("systemInformationField"));
    label->setMinimumWidth(150);
    auto* progress = new QProgressBar(row);
    progress->setObjectName(QStringLiteral("systemInformationProgress"));
    progress->setRange(0, 1000);
    progress->setValue(static_cast<int>(std::round(boundedRatio * 1000.0)));
    progress->setFormat(formatPercent(boundedRatio));
    layout->addWidget(label);
    layout->addWidget(progress, 1);
    section.body->addWidget(row);
}

void SystemInformationViewHelper::addEmptyState(Section& section, PluginHost& host) {
    auto* label = new QLabel(host.translate(QStringLiteral("system-information.common.not-detected")), section.frame);
    label->setObjectName(QStringLiteral("mutedLabel"));
    section.body->addWidget(label);
}

QString SystemInformationViewHelper::processorCoreDetails(PluginHost& host, const Processor& processor) {
    QStringList details;
    details.reserve(processor.cores.size());

    for (const auto& core : processor.cores) {
        const QString smt = core.simultaneousMultithreading ? QStringLiteral("SMT") : QStringLiteral("—");
        details.append(host.translate(QStringLiteral("system-information.common.core-format")).arg(QLocale::system().toString(core.id), formatFrequency(host, core.maximumFrequencyHz), formatBytes(host, core.l1DataBytes + core.l1InstructionBytes), formatBytes(host, core.l2Bytes), formatBytes(host, core.l3Bytes), smt));
    }

    return details.join(QLatin1Char('\n'));
}

QString SystemInformationViewHelper::processorThreadDetails(PluginHost& host, const ProcessorUsage& usage) {
    QStringList details;
    details.reserve(usage.threadUtilization.size());

    for (qsizetype index = 0; index < usage.threadUtilization.size(); ++index) {
        const quint64 frequency = index < usage.threadFrequencyHz.size() && usage.threadFrequencyHz.at(index) > 0 ? static_cast<quint64>(usage.threadFrequencyHz.at(index)) : 0;
        details.append(host.translate(QStringLiteral("system-information.common.thread-format")).arg(QLocale::system().toString(index), formatPercent(usage.threadUtilization.at(index)), formatFrequency(host, frequency)));
    }

    return details.join(QLatin1Char('\n'));
}

SystemInformationView::SystemInformationView(SystemInformationPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host) {
    setObjectName(QStringLiteral("systemInformationView"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new ui::PageHeader(m_host.theme(), m_host.translate(QStringLiteral("system-information.plugin.title")), this);
    m_updated = new QLabel(header);
    m_updated->setObjectName(QStringLiteral("systemInformationUpdated"));
    m_refresh = new QPushButton(ui::IconCatalog::icon(ui::IconName::Refresh, m_host.theme()), m_host.translate(QStringLiteral("system-information.view.refresh")), header);
    m_refresh->setObjectName(QStringLiteral("systemInformationRefresh"));
    header->addWidget(m_updated);
    header->addStretch();
    header->addWidget(m_refresh);
    root->addWidget(header);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("systemInformationScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    m_content = new QWidget(scrollArea);
    m_content->setObjectName(QStringLiteral("systemInformationContent"));
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(14, 14, 14, 14);
    m_contentLayout->setSpacing(12);
    m_state = new QLabel(m_host.translate(QStringLiteral("system-information.view.collecting")), m_content);
    m_state->setObjectName(QStringLiteral("mutedLabel"));
    m_state->setAlignment(Qt::AlignCenter);
    m_contentLayout->addWidget(m_state, 1);
    scrollArea->setWidget(m_content);
    root->addWidget(scrollArea, 1);

    connect(m_refresh, &QPushButton::clicked, this, &SystemInformationView::requestRefresh);
    connect(&m_plugin, &SystemInformationPlugin::snapshotChanged, this, &SystemInformationView::renderSnapshot);
    connect(&m_plugin, &SystemInformationPlugin::refreshStateChanged, this, &SystemInformationView::updateRefreshState);

    if (m_plugin.snapshot().has_value()) {
        render(m_plugin.snapshot().value());
    } else if (!m_plugin.isRefreshing()) {
        requestRefresh();
    }

    m_refresh->setEnabled(!m_plugin.isRefreshing());
}

void SystemInformationView::render(const SystemSnapshot& snapshot) {
    clearContent();
    m_updated->setText(m_host.translate(QStringLiteral("system-information.view.updated")).arg(ui::Components::localTimestamp(snapshot.capturedAtUtc)));

    Section overview = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.overview"), ui::IconName::System);
    SystemInformationViewHelper::addRow(overview, m_host, QStringLiteral("system-information.field.name"), SystemInformationViewHelper::availableText(m_host, snapshot.operatingSystem.name));
    SystemInformationViewHelper::addRow(overview, m_host, QStringLiteral("system-information.field.model"), snapshot.processors.isEmpty() ? m_host.translate(QStringLiteral("system-information.common.unavailable")) : SystemInformationViewHelper::availableText(m_host, snapshot.processors.front().model));
    SystemInformationViewHelper::addRow(overview, m_host, QStringLiteral("system-information.field.total"), SystemInformationViewHelper::formatBytes(m_host, snapshot.memory.totalBytes));

    if (snapshot.processorUsage.utilization.has_value()) {
        SystemInformationViewHelper::addProgress(overview, m_host, QStringLiteral("system-information.field.utilization"), snapshot.processorUsage.utilization.value());
    }

    if (snapshot.memory.totalBytes > 0 && snapshot.memory.availableBytes <= snapshot.memory.totalBytes) {
        SystemInformationViewHelper::addProgress(overview, m_host, QStringLiteral("system-information.field.used"), static_cast<double>(snapshot.memory.totalBytes - snapshot.memory.availableBytes) / static_cast<double>(snapshot.memory.totalBytes));
    }

    m_contentLayout->addWidget(overview.frame);

    Section operatingSystem = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.operating-system"), ui::IconName::System);
    SystemInformationViewHelper::addRow(operatingSystem, m_host, QStringLiteral("system-information.field.host-name"), SystemInformationViewHelper::availableText(m_host, snapshot.operatingSystem.hostName));
    SystemInformationViewHelper::addRow(operatingSystem, m_host, QStringLiteral("system-information.field.name"), SystemInformationViewHelper::availableText(m_host, snapshot.operatingSystem.name));
    SystemInformationViewHelper::addRow(operatingSystem, m_host, QStringLiteral("system-information.field.version"), SystemInformationViewHelper::availableText(m_host, snapshot.operatingSystem.version));
    SystemInformationViewHelper::addRow(operatingSystem, m_host, QStringLiteral("system-information.field.kernel"), SystemInformationViewHelper::availableText(m_host, snapshot.operatingSystem.kernel));
    SystemInformationViewHelper::addRow(operatingSystem, m_host, QStringLiteral("system-information.field.architecture"), snapshot.operatingSystem.architectureBits == 0 ? m_host.translate(QStringLiteral("system-information.common.unavailable")) : QStringLiteral("%1-bit").arg(snapshot.operatingSystem.architectureBits));
    SystemInformationViewHelper::addRow(operatingSystem, m_host, QStringLiteral("system-information.field.byte-order"), SystemInformationViewHelper::translatedValue(m_host, snapshot.operatingSystem.byteOrder));
    m_contentLayout->addWidget(operatingSystem.frame);

    Section processors = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.processor"), ui::IconName::Processor);

    if (snapshot.processors.isEmpty()) {
        SystemInformationViewHelper::addEmptyState(processors, m_host);
    }

    if (snapshot.processorUsage.utilization.has_value()) {
        SystemInformationViewHelper::addProgress(processors, m_host, QStringLiteral("system-information.field.utilization"), snapshot.processorUsage.utilization.value());
    }

    if (!snapshot.processorUsage.threadUtilization.isEmpty()) {
        SystemInformationViewHelper::addRow(processors, m_host, QStringLiteral("system-information.field.utilization"), SystemInformationViewHelper::processorThreadDetails(m_host, snapshot.processorUsage));
    }

    for (qsizetype index = 0; index < snapshot.processors.size(); ++index) {
        const auto& processor = snapshot.processors.at(index);
        SystemInformationViewHelper::addDeviceTitle(processors, m_host.translate(QStringLiteral("system-information.common.processor-name")).arg(index + 1));
        SystemInformationViewHelper::addRow(processors, m_host, QStringLiteral("system-information.field.vendor"), SystemInformationViewHelper::availableText(m_host, processor.vendor));
        SystemInformationViewHelper::addRow(processors, m_host, QStringLiteral("system-information.field.model"), SystemInformationViewHelper::availableText(m_host, processor.model));
        SystemInformationViewHelper::addRow(processors, m_host, QStringLiteral("system-information.field.physical-cores"), SystemInformationViewHelper::formatInteger(m_host, processor.physicalCoreCount));
        SystemInformationViewHelper::addRow(processors, m_host, QStringLiteral("system-information.field.logical-cores"), SystemInformationViewHelper::formatInteger(m_host, processor.logicalCoreCount));
        if (!processor.cores.isEmpty()) {
            SystemInformationViewHelper::addRow(processors, m_host, QStringLiteral("system-information.field.core-details"), SystemInformationViewHelper::processorCoreDetails(m_host, processor));
        }
        if (!processor.flags.isEmpty()) {
            SystemInformationViewHelper::addRow(processors, m_host, QStringLiteral("system-information.field.capabilities"), processor.flags.join(QStringLiteral(", ")));
        }
    }

    m_contentLayout->addWidget(processors.frame);

    Section memory = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.memory"), ui::IconName::Memory);
    SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.total"), SystemInformationViewHelper::formatBytes(m_host, snapshot.memory.totalBytes));
    SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.available"), SystemInformationViewHelper::formatBytes(m_host, snapshot.memory.availableBytes));
    SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.free"), SystemInformationViewHelper::formatBytes(m_host, snapshot.memory.freeBytes));

    for (qsizetype index = 0; index < snapshot.memory.modules.size(); ++index) {
        const auto& module = snapshot.memory.modules.at(index);
        SystemInformationViewHelper::addDeviceTitle(memory, m_host.translate(QStringLiteral("system-information.common.module-name")).arg(index + 1));
        SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.vendor"), SystemInformationViewHelper::availableText(m_host, module.vendor));
        SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.name"), SystemInformationViewHelper::availableText(m_host, module.name));
        SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.model"), SystemInformationViewHelper::availableText(m_host, module.model));
        SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.serial-number"), SystemInformationViewHelper::availableText(m_host, module.serialNumber));
        SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.size"), SystemInformationViewHelper::formatBytes(m_host, module.sizeBytes));
        SystemInformationViewHelper::addRow(memory, m_host, QStringLiteral("system-information.field.frequency"), SystemInformationViewHelper::formatFrequency(m_host, module.frequencyHz));
    }

    m_contentLayout->addWidget(memory.frame);

    Section graphics = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.graphics"), ui::IconName::Graphics);

    if (snapshot.graphicsProcessors.isEmpty()) {
        SystemInformationViewHelper::addEmptyState(graphics, m_host);
    }

    for (qsizetype index = 0; index < snapshot.graphicsProcessors.size(); ++index) {
        const auto& graphicsProcessor = snapshot.graphicsProcessors.at(index);
        SystemInformationViewHelper::addDeviceTitle(graphics, m_host.translate(QStringLiteral("system-information.common.graphics-name")).arg(index + 1));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.vendor"), SystemInformationViewHelper::availableText(m_host, graphicsProcessor.vendor));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.model"), SystemInformationViewHelper::availableText(m_host, graphicsProcessor.name));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.driver"), SystemInformationViewHelper::availableText(m_host, graphicsProcessor.driverVersion));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.vendor-id"), SystemInformationViewHelper::availableText(m_host, graphicsProcessor.vendorId));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.device-id"), SystemInformationViewHelper::availableText(m_host, graphicsProcessor.deviceId));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.dedicated-memory"), SystemInformationViewHelper::formatBytes(m_host, graphicsProcessor.dedicatedMemoryBytes));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.shared-memory"), SystemInformationViewHelper::formatBytes(m_host, graphicsProcessor.sharedMemoryBytes));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.frequency"), SystemInformationViewHelper::formatFrequency(m_host, graphicsProcessor.frequencyHz));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.cores"), SystemInformationViewHelper::formatInteger(m_host, graphicsProcessor.coreCount));
    }

    for (qsizetype index = 0; index < snapshot.displays.size(); ++index) {
        const auto& display = snapshot.displays.at(index);
        SystemInformationViewHelper::addDeviceTitle(graphics, m_host.translate(QStringLiteral("system-information.common.display-name")).arg(index + 1));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.model"), SystemInformationViewHelper::availableText(m_host, display.name));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.resolution"), m_host.translate(QStringLiteral("system-information.common.resolution")).arg(QLocale::system().toString(display.widthPixels)).arg(QLocale::system().toString(display.heightPixels)));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.scale"), m_host.translate(QStringLiteral("system-information.common.percentage")).arg(QLocale::system().toString(display.devicePixelRatio * 100.0, 'f', 0)));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.pixel-density"), m_host.translate(QStringLiteral("system-information.common.dots-per-inch")).arg(QLocale::system().toString(display.logicalDotsPerInch, 'f', 0)));
        SystemInformationViewHelper::addRow(graphics, m_host, QStringLiteral("system-information.field.refresh-rate"), m_host.translate(QStringLiteral("system-information.common.hertz")).arg(QLocale::system().toString(display.refreshHz, 'f', 0)));
    }

    m_contentLayout->addWidget(graphics.frame);

    Section mainboard = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.mainboard"), ui::IconName::Mainboard);
    SystemInformationViewHelper::addRow(mainboard, m_host, QStringLiteral("system-information.field.vendor"), SystemInformationViewHelper::availableText(m_host, snapshot.mainboard.vendor));
    SystemInformationViewHelper::addRow(mainboard, m_host, QStringLiteral("system-information.field.model"), SystemInformationViewHelper::availableText(m_host, snapshot.mainboard.name));
    SystemInformationViewHelper::addRow(mainboard, m_host, QStringLiteral("system-information.field.version"), SystemInformationViewHelper::availableText(m_host, snapshot.mainboard.version));
    SystemInformationViewHelper::addRow(mainboard, m_host, QStringLiteral("system-information.field.serial-number"), SystemInformationViewHelper::availableText(m_host, snapshot.mainboard.serialNumber));
    m_contentLayout->addWidget(mainboard.frame);

    Section storage = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.storage"), ui::IconName::Storage);

    if (snapshot.disks.isEmpty()) {
        SystemInformationViewHelper::addEmptyState(storage, m_host);
    }

    for (qsizetype index = 0; index < snapshot.disks.size(); ++index) {
        const auto& disk = snapshot.disks.at(index);
        SystemInformationViewHelper::addDeviceTitle(storage, m_host.translate(QStringLiteral("system-information.common.disk-name")).arg(index + 1));
        SystemInformationViewHelper::addRow(storage, m_host, QStringLiteral("system-information.field.vendor"), SystemInformationViewHelper::availableText(m_host, disk.vendor));
        SystemInformationViewHelper::addRow(storage, m_host, QStringLiteral("system-information.field.model"), SystemInformationViewHelper::availableText(m_host, disk.model));
        SystemInformationViewHelper::addRow(storage, m_host, QStringLiteral("system-information.field.serial-number"), SystemInformationViewHelper::availableText(m_host, disk.serialNumber));
        SystemInformationViewHelper::addRow(storage, m_host, QStringLiteral("system-information.field.interface"), SystemInformationViewHelper::availableText(m_host, disk.interfaceName));
        SystemInformationViewHelper::addRow(storage, m_host, QStringLiteral("system-information.field.size"), SystemInformationViewHelper::formatBytes(m_host, disk.sizeBytes));
        QStringList volumes;
        for (const auto& volume : disk.volumes) {
            volumes.append(m_host.translate(QStringLiteral("system-information.common.volume-format")).arg(SystemInformationViewHelper::availableText(m_host, volume.mountPoint), SystemInformationViewHelper::formatBytes(m_host, volume.freeBytes)));
        }
        SystemInformationViewHelper::addRow(storage, m_host, QStringLiteral("system-information.field.volumes"), volumes.isEmpty() ? m_host.translate(QStringLiteral("system-information.common.unavailable")) : volumes.join(QLatin1Char('\n')));
    }

    m_contentLayout->addWidget(storage.frame);

    Section batteries = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.batteries"), ui::IconName::Battery);

    if (snapshot.batteries.isEmpty()) {
        SystemInformationViewHelper::addEmptyState(batteries, m_host);
    }

    for (qsizetype index = 0; index < snapshot.batteries.size(); ++index) {
        const auto& battery = snapshot.batteries.at(index);
        SystemInformationViewHelper::addDeviceTitle(batteries, m_host.translate(QStringLiteral("system-information.common.battery-name")).arg(index + 1));
        SystemInformationViewHelper::addRow(batteries, m_host, QStringLiteral("system-information.field.vendor"), SystemInformationViewHelper::availableText(m_host, battery.vendor));
        SystemInformationViewHelper::addRow(batteries, m_host, QStringLiteral("system-information.field.model"), SystemInformationViewHelper::availableText(m_host, battery.model));
        SystemInformationViewHelper::addRow(batteries, m_host, QStringLiteral("system-information.field.serial-number"), SystemInformationViewHelper::availableText(m_host, battery.serialNumber));
        SystemInformationViewHelper::addRow(batteries, m_host, QStringLiteral("system-information.field.technology"), SystemInformationViewHelper::availableText(m_host, battery.technology));
        SystemInformationViewHelper::addRow(batteries, m_host, QStringLiteral("system-information.field.state"), SystemInformationViewHelper::translatedValue(m_host, battery.state));
        if (battery.capacityPercent.has_value()) {
            SystemInformationViewHelper::addProgress(batteries, m_host, QStringLiteral("system-information.field.capacity"), battery.capacityPercent.value() / 100.0);
        }
        SystemInformationViewHelper::addRow(batteries, m_host, QStringLiteral("system-information.field.energy"), m_host.translate(QStringLiteral("system-information.common.energy-format")).arg(SystemInformationViewHelper::formatInteger(m_host, battery.currentEnergy), SystemInformationViewHelper::formatInteger(m_host, battery.fullEnergy)));
    }

    m_contentLayout->addWidget(batteries.frame);

    Section network = SystemInformationViewHelper::createSection(m_content, m_host, QStringLiteral("system-information.section.network"), ui::IconName::Network);

    if (snapshot.networkInterfaces.isEmpty()) {
        SystemInformationViewHelper::addEmptyState(network, m_host);
    }

    for (qsizetype index = 0; index < snapshot.networkInterfaces.size(); ++index) {
        const auto& networkInterface = snapshot.networkInterfaces.at(index);
        SystemInformationViewHelper::addDeviceTitle(network, m_host.translate(QStringLiteral("system-information.common.network-name")).arg(index + 1));
        SystemInformationViewHelper::addRow(network, m_host, QStringLiteral("system-information.field.index"), SystemInformationViewHelper::availableText(m_host, networkInterface.index));
        SystemInformationViewHelper::addRow(network, m_host, QStringLiteral("system-information.field.description"), SystemInformationViewHelper::availableText(m_host, networkInterface.description));
        SystemInformationViewHelper::addRow(network, m_host, QStringLiteral("system-information.field.mac-address"), SystemInformationViewHelper::availableText(m_host, networkInterface.macAddress));
        SystemInformationViewHelper::addRow(network, m_host, QStringLiteral("system-information.field.ipv4-address"), SystemInformationViewHelper::availableText(m_host, networkInterface.ipv4Address));
        SystemInformationViewHelper::addRow(network, m_host, QStringLiteral("system-information.field.ipv6-address"), SystemInformationViewHelper::availableText(m_host, networkInterface.ipv6Address));
    }

    m_contentLayout->addWidget(network.frame);
    m_contentLayout->addStretch();
}

void SystemInformationView::clearContent() {
    m_state = nullptr;

    while (m_contentLayout->count() > 0) {
        QLayoutItem* item = m_contentLayout->takeAt(0);
        if (QWidget* widget = item->widget(); widget != nullptr) {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
}

void SystemInformationView::requestRefresh() {
    const auto result = m_plugin.refresh();

    if (!result.hasValue() && result.error().code != QStringLiteral("systeminformation_refresh_in_progress")) {
        m_host.notify(m_host.translate(QStringLiteral("system-information.error.title")), m_host.translate(QStringLiteral("system-information.error.message")), AlertSeverity::Error);
    }
}

void SystemInformationView::renderSnapshot() {
    if (m_plugin.snapshot().has_value()) {
        render(m_plugin.snapshot().value());
    }
}

void SystemInformationView::updateRefreshState(bool refreshing) {
    m_refresh->setEnabled(!refreshing);

    if (m_plugin.snapshot().has_value()) {
        return;
    }

    m_updated->setText(m_host.translate(refreshing ? QStringLiteral("system-information.view.collecting") : QStringLiteral("system-information.error.message")));

    if (m_state != nullptr) {
        m_state->setText(m_updated->text());
    }
}

} // namespace workpane::plugins::systeminformation
