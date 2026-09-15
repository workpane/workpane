#include "AiServiceSettingsView.h"

#include "AiPlugin.h"
#include "AiSecret.h"
#include "ui/Components.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace workpane::plugins::ai {

constexpr int instanceRow = 1;
constexpr int keyRow = 2;
constexpr int voiceRow = 1;
constexpr int declaredVoiceRow = 2;

AiSearchSettingsView::AiSearchSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host) {
    setObjectName(QStringLiteral("aiSearchSettings"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const auto [page, layout] = ui::Components::settingsSectionPage(this);
    m_form = ui::Components::settingsForm();

    const SearchSettings settings = m_plugin.effectiveSearchSettings();
    m_service = new ui::ComboBox(m_host.theme(), page);
    m_service->setObjectName(QStringLiteral("aiSearchProvider"));
    m_service->addItem(m_host.translate(QStringLiteral("ai.search.brave")), TaskContracts::searchProviderIdentifier(SearchProvider::Brave));
    m_service->addItem(m_host.translate(QStringLiteral("ai.search.tavily")), TaskContracts::searchProviderIdentifier(SearchProvider::Tavily));
    m_service->addItem(m_host.translate(QStringLiteral("ai.search.searxng")), TaskContracts::searchProviderIdentifier(SearchProvider::SearxNg));
    ui::Components::sortComboBoxItems(m_service);
    m_service->setCurrentIndex(m_service->findData(TaskContracts::searchProviderIdentifier(settings.provider)));

    m_instance = new QLineEdit(settings.instanceUrl, page);
    m_instance->setObjectName(QStringLiteral("aiSearchInstance"));
    m_instance->setPlaceholderText(m_host.translate(QStringLiteral("ai.settings.search-instance-placeholder")));

    // clang-format off
    const auto confirmReveal = [this]() { return m_host.confirm(this, m_host.translate(QStringLiteral("ai.settings.reveal-title")), m_host.translate(QStringLiteral("ai.settings.reveal-message")), m_host.translate(QStringLiteral("ai.settings.reveal-detail")), m_host.translate(QStringLiteral("ai.settings.reveal-action")), false); };
    // clang-format on
    m_apiKey = new ui::SecretField(m_host.theme(), m_host.translate(QStringLiteral("ai.settings.api-key-placeholder")), confirmReveal, page);
    m_apiKey->setObjectName(QStringLiteral("aiSearchKey"));
    m_apiKey->setValue(settings.apiKey);

    ui::Components::addSettingsRow(m_form, m_host.translate(QStringLiteral("ai.settings.search-provider")), m_service);
    ui::Components::addSettingsRow(m_form, m_host.translate(QStringLiteral("ai.settings.search-instance")), m_instance);
    ui::Components::addSettingsRow(m_form, m_host.translate(QStringLiteral("ai.settings.search-key")), m_apiKey);
    layout->addLayout(m_form);
    layout->addStretch(1);
    root->addWidget(page, 1);

    // clang-format off
    connect(m_service, &QComboBox::currentIndexChanged, this, [this]() { applyService(); persist(); });
    connect(m_instance, &QLineEdit::editingFinished, this, [this]() { persist(); });
    connect(m_apiKey, &ui::SecretField::editingFinished, this, [this]() { persist(); });
    // clang-format on
    applyService();
}

// A hosted service publishes one endpoint, so only a self-hosted instance is asked for its address.
void AiSearchSettingsView::applyService() {
    const auto selected = TaskContracts::searchProviderFromIdentifier(m_service->currentData().toString());
    const bool selfHosted = selected.has_value() && selected.value() == SearchProvider::SearxNg;
    m_form->setRowVisible(instanceRow, selfHosted);
    m_form->setRowVisible(keyRow, !selfHosted);

    if (selected.has_value() && !selfHosted && m_apiKey->value().isEmpty()) {
        m_apiKey->setValue(Secrets::defaultSecretReference(TaskContracts::searchProviderKeyVariable(selected.value())));
    }
}

void AiSearchSettingsView::persist() {
    const auto selected = TaskContracts::searchProviderFromIdentifier(m_service->currentData().toString());

    if (!selected.has_value()) {
        return;
    }

    const bool selfHosted = selected.value() == SearchProvider::SearxNg;
    const SearchSettings settings{selected.value(), selfHosted ? m_instance->text().trimmed() : QString{}, selfHosted ? QString{} : m_apiKey->value()};
    auto future = m_plugin.saveSearchSettings(settings);
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.service-save")), AlertSeverity::Error); } });
    // clang-format on
}

AiSpeechSettingsView::AiSpeechSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host) {
    setObjectName(QStringLiteral("aiSpeechSettings"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const auto [page, layout] = ui::Components::settingsSectionPage(this);
    m_form = ui::Components::settingsForm();

    const SpeechSettings settings = m_plugin.effectiveSpeechSettings();
    m_service = new ui::ComboBox(m_host.theme(), page);
    m_service->setObjectName(QStringLiteral("aiSpeechProvider"));

    for (const auto* provider : ModelConnections::providersAnswering(ModelEndpoint::Speech)) {
        m_service->addItem(m_host.translate(provider->titleKey), provider->id);
    }

    ui::Components::sortComboBoxItems(m_service);
    m_service->setCurrentIndex(m_service->findData(settings.providerId));

    m_voice = new QLineEdit(settings.voiceId, page);
    m_voice->setObjectName(QStringLiteral("aiSpeechVoice"));
    m_voice->setPlaceholderText(m_host.translate(QStringLiteral("ai.settings.speech-voice-placeholder")));

    m_declaredVoice = new ui::ComboBox(m_host.theme(), page);
    m_declaredVoice->setObjectName(QStringLiteral("aiSpeechDeclaredVoice"));

    // clang-format off
    const auto confirmReveal = [this]() { return m_host.confirm(this, m_host.translate(QStringLiteral("ai.settings.reveal-title")), m_host.translate(QStringLiteral("ai.settings.reveal-message")), m_host.translate(QStringLiteral("ai.settings.reveal-detail")), m_host.translate(QStringLiteral("ai.settings.reveal-action")), false); };
    // clang-format on
    m_apiKey = new ui::SecretField(m_host.theme(), m_host.translate(QStringLiteral("ai.settings.api-key-placeholder")), confirmReveal, page);
    m_apiKey->setObjectName(QStringLiteral("aiSpeechKey"));
    m_apiKey->setValue(settings.apiKey);

    ui::Components::addSettingsRow(m_form, m_host.translate(QStringLiteral("ai.settings.speech-provider")), m_service);
    ui::Components::addSettingsRow(m_form, m_host.translate(QStringLiteral("ai.settings.speech-voice")), m_voice);
    ui::Components::addSettingsRow(m_form, m_host.translate(QStringLiteral("ai.settings.speech-voice")), m_declaredVoice);
    ui::Components::addSettingsRow(m_form, m_host.translate(QStringLiteral("ai.settings.speech-key")), m_apiKey);
    layout->addLayout(m_form);
    layout->addStretch(1);
    root->addWidget(page, 1);

    // clang-format off
    connect(m_service, &QComboBox::currentIndexChanged, this, [this]() { applyService(); persist(); });
    connect(m_declaredVoice, &QComboBox::currentIndexChanged, this, [this]() { if (!m_loading) { persist(); } });
    connect(m_voice, &QLineEdit::editingFinished, this, [this]() { persist(); });
    connect(m_apiKey, &ui::SecretField::editingFinished, this, [this]() { persist(); });
    // clang-format on
    applyService();
}

QString AiSpeechSettingsView::selectedService() const {
    return m_service->currentData().toString();
}

// A service with a closed voice set is chosen from that set, while a service with an account catalog is typed and listed by the agent tool.
void AiSpeechSettingsView::applyService() {
    const SpeechSettings selected = TaskContracts::declaredSpeechSettings(selectedService());
    const EndpointDescriptor* endpoint = TaskContracts::speechEndpoint(selectedService());
    const QStringList declared = endpoint == nullptr ? QStringList{} : endpoint->voices;
    m_loading = true;
    m_declaredVoice->clear();

    for (const auto& voice : declared) {
        m_declaredVoice->addItem(voice, voice);
    }

    ui::Components::sortComboBoxItems(m_declaredVoice);
    const int stored = m_declaredVoice->findData(m_plugin.effectiveSpeechSettings().voiceId);
    m_declaredVoice->setCurrentIndex(stored >= 0 ? stored : m_declaredVoice->findData(selected.voiceId));
    m_loading = false;

    m_form->setRowVisible(voiceRow, declared.isEmpty());
    m_form->setRowVisible(declaredVoiceRow, !declared.isEmpty());

    if (m_apiKey->value().isEmpty()) {
        m_apiKey->setValue(selected.apiKey);
    }
}

QString AiSpeechSettingsView::selectedVoice() const {
    const EndpointDescriptor* endpoint = TaskContracts::speechEndpoint(selectedService());
    return endpoint == nullptr || endpoint->voices.isEmpty() ? m_voice->text().trimmed() : m_declaredVoice->currentData().toString();
}

void AiSpeechSettingsView::persist() {
    auto future = m_plugin.saveSpeechSettings({selectedService(), selectedVoice(), m_apiKey->value()});
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.service-save")), AlertSeverity::Error); } });
    // clang-format on
}

} // namespace workpane::plugins::ai
