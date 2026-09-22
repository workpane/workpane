#include "ui/SettingsView.h"

#include "ui/Components.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace workpane::ui {

constexpr int settingsPageVerticalPadding = 22;

SettingsView::SettingsView(plugins::PluginManager& pluginManager, QVector<CoreSettingsContribution> coreSettings, QWidget* parent) : QWidget(parent), m_pluginManager(pluginManager) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new PageHeader(m_pluginManager.theme(), m_pluginManager.translate(QStringLiteral("workpane.settings.title")), this);
    header->addStretch();
    root->addWidget(header);

    auto* searchBand = new QWidget(this);
    searchBand->setObjectName(QStringLiteral("settingsSearchBand"));
    searchBand->setAttribute(Qt::WA_StyledBackground, true);
    auto* searchLayout = new QHBoxLayout(searchBand);
    searchLayout->setContentsMargins(12, 8, 12, 8);
    auto* search = new QLineEdit(searchBand);
    search->setObjectName(QStringLiteral("settingsSearch"));
    search->setClearButtonEnabled(true);
    search->setPlaceholderText(m_pluginManager.translate(QStringLiteral("workpane.settings.search")));
    searchLayout->addWidget(search);
    root->addWidget(searchBand);

    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    m_plugins = new QListWidget(body);
    m_plugins->setObjectName(QStringLiteral("settingsCategories"));
    m_plugins->setFixedWidth(210);
    m_pages = new QStackedWidget(body);
    m_noResults = Components::emptyStateLabel(m_pluginManager.translate(QStringLiteral("workpane.settings.no-results")), body);
    m_noResults->hide();
    bodyLayout->addWidget(m_plugins);
    bodyLayout->addWidget(m_pages, 1);
    bodyLayout->addWidget(m_noResults, 1);
    root->addWidget(body, 1);

    for (const auto& coreContribution : coreSettings) {
        if (!appendGroup(coreContribution.group, coreContribution.createSection)) {
            m_valid = false;
            return;
        }
    }

    for (const auto& pluginSettings : m_pluginManager.settings()) {
        // clang-format off
        const auto createSection = [this, pluginId = pluginSettings.pluginId](const QString& groupId, const QString& sectionId, QWidget* sectionParent) { return m_pluginManager.createSettingsSection(pluginId, groupId, sectionId, sectionParent); };
        // clang-format on
        if (!appendGroup(pluginSettings.group, createSection)) {
            m_valid = false;
            return;
        }
    }

    if (m_plugins->count() > 0) {
        m_plugins->setCurrentRow(0);
    }

    connect(m_plugins, &QListWidget::currentRowChanged, this, &SettingsView::selectPlugin);
    connect(search, &QLineEdit::textChanged, this, &SettingsView::filterSettings);
}

bool SettingsView::isValid() const {
    return m_valid;
}

bool SettingsView::appendGroup(const plugins::SettingsGroup& group, const SettingsSectionFactory& createSection) {
    auto* item = new QListWidgetItem(m_pluginManager.translate(group.titleKey), m_plugins);
    item->setData(Qt::UserRole, group.id);
    QStringList searchable{item->text()};

    auto* scrollArea = new QScrollArea(m_pages);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scrollArea);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, settingsPageVerticalPadding, 0, settingsPageVerticalPadding);
    pageLayout->setSpacing(22);

    for (const auto& section : group.sections) {
        if (&section != &group.sections.first()) {
            pageLayout->addWidget(Components::horizontalDivider(page));
        }
        auto* sectionHost = new QWidget(page);
        auto* sectionLayout = new QVBoxLayout(sectionHost);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(10);
        sectionLayout->setAlignment(Qt::AlignTop);
        auto* sectionTitle = Components::sectionTitleLabel(m_pluginManager.translate(section.titleKey), sectionHost);
        const int inset = m_pluginManager.theme().metric(ThemeMetric::SettingsHorizontalPadding);
        sectionTitle->setContentsMargins(inset, 0, inset, 0);
        sectionLayout->addWidget(sectionTitle);
        auto* content = createSection(group.id, section.id, sectionHost);
        if (content == nullptr) {
            return false;
        }
        if (auto* contentLayout = content->layout(); contentLayout != nullptr) {
            contentLayout->setContentsMargins(0, 0, 0, 0);
        }
        sectionLayout->addWidget(content);
        pageLayout->addWidget(sectionHost);
        searchable.append(sectionTitle->text());
        for (const auto& key : section.searchKeys) {
            searchable.append(m_pluginManager.translate(key));
        }
    }

    pageLayout->addStretch(1);
    scrollArea->setWidget(page);
    m_pages->addWidget(scrollArea);
    item->setData(Qt::UserRole + 1, searchable.join(QLatin1Char(' ')));
    return true;
}

void SettingsView::filterSettings(const QString& query) {
    const QString normalized = query.trimmed();
    int firstVisible = -1;

    for (int index = 0; index < m_plugins->count(); ++index) {
        auto* item = m_plugins->item(index);
        const bool visible = normalized.isEmpty() || item->data(Qt::UserRole + 1).toString().contains(normalized, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && firstVisible < 0) {
            firstVisible = index;
        }
    }

    const auto* current = m_plugins->item(m_plugins->currentRow());

    if (firstVisible >= 0 && (current == nullptr || current->isHidden())) {
        m_plugins->setCurrentRow(firstVisible);
    }

    m_pages->setVisible(firstVisible >= 0);
    m_noResults->setVisible(firstVisible < 0);
}

void SettingsView::selectPlugin(int row) {
    if (row >= 0) {
        m_pages->setCurrentIndex(row);
    }
}

} // namespace workpane::ui
