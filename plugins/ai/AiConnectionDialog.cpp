#include "AiConnectionDialog.h"

#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace workpane::plugins::ai {

constexpr int connectionDialogMinimumWidth = 640;
constexpr int extraParametersMinimumHeight = 120;

class AiConnectionDialogHelper final {
  public:
    static Result<QJsonObject> parseExtraParameters(const QString& text);
};

// An empty editor declares nothing extra, and anything else has to be one JSON object.
Result<QJsonObject> AiConnectionDialogHelper::parseExtraParameters(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return Result<QJsonObject>::success({});
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        return Result<QJsonObject>::failure({"ai_extra_parameter_syntax", parseError.errorString(), QString::number(parseError.offset)});
    }
    if (!document.isObject()) {
        return Result<QJsonObject>::failure({"ai_extra_parameter_shape", "The extra parameters are not an object", {}});
    }

    return ModelConnections::validateExtraParameters(document.object());
}

AiConnectionDialog::AiConnectionDialog(PluginHost& host, const ModelConnection& connection, QStringList takenKeys, QWidget* parent) : QDialog(parent), m_host(host), m_original(connection), m_takenKeys(std::move(takenKeys)) {
    setObjectName(QStringLiteral("aiConnectionDialog"));
    setWindowTitle(m_host.translate(connection.providerId.isEmpty() ? QStringLiteral("ai.connection.add") : QStringLiteral("ai.connection.edit")));
    setMinimumWidth(connectionDialogMinimumWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(12);

    m_form = new QFormLayout();
    m_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_provider = new ui::ComboBox(m_host.theme(), this);
    m_provider->setObjectName(QStringLiteral("aiConnectionProvider"));

    for (const auto* descriptor : ModelConnections::providersAnswering(ModelEndpoint::Chat)) {
        m_provider->addItem(m_host.translate(descriptor->titleKey), descriptor->id);
    }

    ui::Components::sortComboBoxItems(m_provider);

    m_model = new ui::ComboBox(m_host.theme(), this);
    m_model->setObjectName(QStringLiteral("aiConnectionModel"));
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);

    m_refreshModels = ui::Components::toolButton(ui::IconName::Refresh, m_host.theme(), m_host.translate(QStringLiteral("ai.settings.refresh-models")), this);
    m_refreshModels->setObjectName(QStringLiteral("aiConnectionRefreshModels"));
    auto* modelRow = new QWidget(this);
    auto* modelRowLayout = new QHBoxLayout(modelRow);
    modelRowLayout->setContentsMargins(0, 0, 0, 0);
    modelRowLayout->setSpacing(6);
    modelRowLayout->addWidget(m_model, 1);
    modelRowLayout->addWidget(m_refreshModels);

    m_displayName = new QLineEdit(connection.displayName, this);
    m_displayName->setObjectName(QStringLiteral("aiConnectionDisplayName"));
    m_displayName->setPlaceholderText(m_host.translate(QStringLiteral("ai.connection.display-name-placeholder")));

    // clang-format off
    const auto confirmReveal = [this]() { return m_host.confirm(this, m_host.translate(QStringLiteral("ai.settings.reveal-title")), m_host.translate(QStringLiteral("ai.settings.reveal-message")), m_host.translate(QStringLiteral("ai.settings.reveal-detail")), m_host.translate(QStringLiteral("ai.settings.reveal-action")), false); };
    // clang-format on
    m_apiKey = new ui::SecretField(m_host.theme(), m_host.translate(QStringLiteral("ai.settings.api-key-placeholder")), confirmReveal, this);
    m_apiKey->setObjectName(QStringLiteral("aiConnectionApiKey"));

    m_address = new QLineEdit(this);
    m_address->setObjectName(QStringLiteral("aiConnectionAddress"));
    m_address->setPlaceholderText(m_host.translate(QStringLiteral("ai.connection.address-placeholder")));

    m_form->addRow(m_host.translate(QStringLiteral("ai.settings.provider")), m_provider);
    m_form->addRow(m_host.translate(QStringLiteral("ai.settings.model")), modelRow);
    m_form->addRow(m_host.translate(QStringLiteral("ai.connection.display-name")), ui::Components::fieldWithHint(m_displayName, m_host.translate(QStringLiteral("ai.connection.display-name-hint")), this));
    m_form->addRow(m_host.translate(QStringLiteral("ai.settings.api-key")), m_apiKey);
    m_apiKeyRow = m_form->rowCount() - 1;
    m_form->addRow(m_host.translate(QStringLiteral("ai.connection.address")), m_address);
    m_addressRow = m_form->rowCount() - 1;
    layout->addLayout(m_form);

    m_parameterForm = new QFormLayout();
    m_parameterForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addLayout(m_parameterForm);

    m_extraSection = new QWidget(this);
    m_extraSection->setObjectName(QStringLiteral("aiConnectionExtraSection"));
    auto* extraLayout = new QVBoxLayout(m_extraSection);
    extraLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_extraSection);

    extraLayout->addWidget(ui::Components::sectionTitleLabel(m_host.translate(QStringLiteral("ai.connection.extra-parameters")), m_extraSection));
    auto* extraDescription = new QLabel(m_host.translate(QStringLiteral("ai.connection.extra-parameters-description")), m_extraSection);
    extraDescription->setObjectName(QStringLiteral("aiConnectionExtraDescription"));
    extraDescription->setWordWrap(true);
    extraLayout->addWidget(extraDescription);

    m_extraParameters = new ui::TextField(m_host.translate(QStringLiteral("ai.connection.extra-parameters-placeholder")), m_extraSection);
    m_extraParameters->setObjectName(QStringLiteral("aiConnectionExtraParameters"));
    m_extraParameters->setMinimumHeight(extraParametersMinimumHeight);
    extraLayout->addWidget(m_extraParameters);

    m_extraValidation = new QLabel(m_extraSection);
    m_extraValidation->setObjectName(QStringLiteral("aiConnectionExtraValidation"));
    m_extraValidation->setWordWrap(true);
    m_extraValidation->hide();
    extraLayout->addWidget(m_extraValidation);

    m_validation = new QLabel(this);
    m_validation->setObjectName(QStringLiteral("aiTaskValidation"));
    m_validation->setWordWrap(true);
    m_validation->hide();
    layout->addWidget(m_validation);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_save = buttons->button(QDialogButtonBox::Save);
    m_save->setObjectName(QStringLiteral("primaryButton"));
    layout->addWidget(buttons);

    m_loading = true;
    m_provider->setCurrentIndex(std::max(0, m_provider->findData(connection.providerId)));
    rebuildModels();

    if (!connection.modelId.isEmpty()) {
        m_model->setCurrentText(connection.modelId);
    }

    const ProviderDescriptor* descriptor = selectedProvider();
    const ModelConnection declared = descriptor != nullptr ? ModelConnections::declaredConnection(*descriptor, m_model->currentText().trimmed()) : ModelConnection{};
    m_apiKey->setValue(connection.providerId.isEmpty() ? declared.apiKey : connection.apiKey);
    m_address->setText(connection.providerId.isEmpty() ? declared.address : connection.address);

    if (!connection.extraParameters.isEmpty()) {
        m_extraParameters->setPlainText(QString::fromUtf8(QJsonDocument(connection.extraParameters).toJson(QJsonDocument::Indented).trimmed()));
    }

    rebuildParameters();
    applyProviderShape();
    validateExtraParameters();
    m_loading = false;

    // clang-format off
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this]() { selectProvider(); });
    connect(m_model, &QComboBox::activated, this, [this]() { selectModel(); });
    connect(m_model->lineEdit(), &QLineEdit::editingFinished, this, [this]() { selectModel(); });
    connect(m_refreshModels, &QToolButton::clicked, this, [this]() { refreshModels(); });
    connect(m_extraParameters, &QPlainTextEdit::textChanged, this, [this]() { validateExtraParameters(); });
    connect(&m_discovery, &AiModelDiscovery::discovered, this, [this](const QStringList& models) { applyDiscoveredModels(models); });
    connect(&m_discovery, &AiModelDiscovery::failed, this, [this](const Error& error) { m_refreshModels->setEnabled(true); showValidation(validationMessage(error)); });
    // clang-format on
}

const ProviderDescriptor* AiConnectionDialog::selectedProvider() const {
    return ProviderCatalog::findProvider(m_provider->currentData().toString());
}

// The credential and the address belong to the provider, so the fields the selected one does not own are closed.
void AiConnectionDialog::applyProviderShape() {
    const ProviderDescriptor* descriptor = selectedProvider();

    if (descriptor == nullptr) {
        return;
    }

    // A provider that signs in on its own has nothing to do with a credential, so the field is absent rather than present and closed.
    m_form->setRowVisible(m_apiKeyRow, descriptor->requiresApiKey);
    m_form->setRowVisible(m_addressRow, descriptor->addressConfigurable);

    // A command line agent is not reached over a wire, so it has no catalog to list and no request body to merge a field into.
    const bool reachedOverWire = descriptor->protocol != WireProtocol::CommandLine;
    m_refreshModels->setVisible(reachedOverWire);
    m_extraSection->setVisible(reachedOverWire);
}

// Moving to another provider starts from what that provider declares, because the credential of the previous one does not open it.
void AiConnectionDialog::selectProvider() {
    const ProviderDescriptor* descriptor = selectedProvider();

    if (descriptor == nullptr || m_loading) {
        return;
    }

    applyProviderShape();
    m_discoveredModels.clear();
    rebuildModels();
    const ModelConnection declared = ModelConnections::declaredConnection(*descriptor, m_model->currentText().trimmed());
    m_apiKey->setValue(declared.apiKey);
    m_address->setText(declared.address);
    rebuildParameters();
    validateExtraParameters();
}

// The model field is editable, so its parameters are rebuilt when an edit ends rather than on every keystroke.
void AiConnectionDialog::selectModel() {
    rebuildParameters();
}

void AiConnectionDialog::rebuildModels() {
    const ProviderDescriptor* descriptor = selectedProvider();

    if (descriptor == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_model);
    const QString previous = m_model->currentText();
    m_model->clear();

    for (const auto& model : descriptor->models) {
        m_model->addItem(model.displayName, model.id);
    }

    for (const auto& model : m_discoveredModels) {
        if (ProviderCatalog::findModel(*descriptor, model) == nullptr) {
            m_model->addItem(model, model);
        }
    }

    ui::Components::sortComboBoxItems(m_model);

    if (!previous.isEmpty() && (ProviderCatalog::findModel(*descriptor, previous) != nullptr || m_discoveredModels.contains(previous))) {
        m_model->setCurrentText(previous);
        return;
    }

    // The provider declares which model it opens with, and presenting the list alphabetically does not change that.
    for (const auto& preferred : descriptor->preferredModels) {
        if (const int row = m_model->findData(preferred); row >= 0) {
            m_model->setCurrentIndex(row);
            return;
        }
    }

    m_model->setCurrentIndex(m_model->count() > 0 ? 0 : -1);
}

// A refreshed catalog belongs to the provider that answered, so it is discarded when the selection moves elsewhere.
void AiConnectionDialog::refreshModels() {
    const ProviderDescriptor* descriptor = selectedProvider();

    if (descriptor == nullptr || m_discovery.running()) {
        return;
    }

    m_refreshModels->setEnabled(false);
    m_validation->hide();
    m_discovery.discover(descriptor->id, m_apiKey->value(), descriptor->addressConfigurable ? m_address->text().trimmed() : descriptor->baseUrl);
}

void AiConnectionDialog::applyDiscoveredModels(const QStringList& models) {
    m_refreshModels->setEnabled(true);
    m_discoveredModels = models;
    rebuildModels();
    rebuildParameters();
}

// The rebuild can be reached from the editing that ends when the user clicks the very editor being replaced, so the old ones outlive that click.
void AiConnectionDialog::clearParameterForm() {
    while (m_parameterForm->rowCount() > 0) {
        const QFormLayout::TakeRowResult row = m_parameterForm->takeRow(0);
        for (QLayoutItem* item : {row.labelItem, row.fieldItem}) {
            if (item == nullptr) {
                continue;
            }
            if (QWidget* widget = item->widget(); widget != nullptr) {
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }
    }

    m_parameterEditors.clear();
}

void AiConnectionDialog::rebuildParameters() {
    clearParameterForm();

    const ProviderDescriptor* descriptor = selectedProvider();

    if (descriptor == nullptr) {
        return;
    }

    const QString modelId = m_model->currentText().trimmed();
    const bool sameModel = m_original.providerId == descriptor->id && m_original.modelId == modelId;
    const QJsonObject stored = sameModel ? m_original.parameters : QJsonObject{};

    for (const auto& parameter : ProviderCatalog::applicableParameters(*descriptor, modelId)) {
        const QJsonValue value = stored.contains(parameter.id) ? stored.value(parameter.id) : parameter.defaultValue;
        QWidget* editor = nullptr;
        if (parameter.type == ParameterType::Enumeration) {
            auto* comboBox = new ui::ComboBox(m_host.theme(), this);
            for (const auto& option : parameter.options) {
                comboBox->addItem(m_host.translate(option.titleKey), option.id);
            }
            comboBox->setCurrentIndex(std::max(0, comboBox->findData(value.toString())));
            editor = comboBox;
        } else if (parameter.type == ParameterType::Integer) {
            auto* spinBox = new QSpinBox(this);
            spinBox->setRange(static_cast<int>(parameter.minimum), static_cast<int>(parameter.maximum));
            spinBox->setValue(static_cast<int>(value.toInteger(static_cast<qint64>(parameter.minimum))));
            editor = spinBox;
        } else {
            auto* spinBox = new QDoubleSpinBox(this);
            spinBox->setDecimals(2);
            spinBox->setSingleStep(0.1);
            spinBox->setRange(parameter.minimum, parameter.maximum);
            spinBox->setValue(value.toDouble(parameter.minimum));
            editor = spinBox;
        }
        editor->setObjectName(QStringLiteral("aiParameter.") + parameter.id);
        m_parameterEditors.insert(parameter.id, editor);
        auto* numericEditor = qobject_cast<QAbstractSpinBox*>(editor);
        m_parameterForm->addRow(m_host.translate(parameter.titleKey), numericEditor == nullptr ? editor : ui::Components::stepperRow(numericEditor, m_host.theme(), this));
    }
}

QJsonObject AiConnectionDialog::collectParameters() const {
    const ProviderDescriptor* descriptor = selectedProvider();

    if (descriptor == nullptr) {
        return {};
    }

    QJsonObject parameters;

    for (const auto& parameter : ProviderCatalog::applicableParameters(*descriptor, m_model->currentText().trimmed())) {
        QWidget* editor = m_parameterEditors.value(parameter.id, nullptr);
        if (editor == nullptr) {
            continue;
        }
        if (auto* comboBox = qobject_cast<QComboBox*>(editor); comboBox != nullptr) {
            parameters.insert(parameter.id, comboBox->currentData().toString());
        } else if (auto* spinBox = qobject_cast<QSpinBox*>(editor); spinBox != nullptr) {
            parameters.insert(parameter.id, spinBox->value());
        } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(editor); doubleSpinBox != nullptr) {
            parameters.insert(parameter.id, doubleSpinBox->value());
        }
    }

    return parameters;
}

// A document that cannot be sent is reported while it is typed, so the confirm action never saves one.
void AiConnectionDialog::validateExtraParameters() {
    const auto parsed = AiConnectionDialogHelper::parseExtraParameters(m_extraParameters->toPlainText());
    m_extraValidation->setText(parsed.hasValue() ? QString{} : validationMessage(parsed.error()));
    m_extraValidation->setVisible(!parsed.hasValue());
    ui::Components::growDialogToContents(this);
    m_save->setEnabled(parsed.hasValue());
}

ModelConnection AiConnectionDialog::connection() const {
    const ProviderDescriptor* descriptor = selectedProvider();
    ModelConnection connection;
    connection.providerId = m_provider->currentData().toString();
    connection.modelId = m_model->currentText().trimmed();
    connection.displayName = m_displayName->text().trimmed();
    connection.apiKey = m_apiKey->value();
    connection.address = descriptor != nullptr && descriptor->addressConfigurable ? m_address->text().trimmed() : QString{};
    connection.parameters = collectParameters();
    const auto extra = AiConnectionDialogHelper::parseExtraParameters(m_extraParameters->toPlainText());

    if (extra.hasValue()) {
        connection.extraParameters = extra.value();
    }

    return connection;
}

void AiConnectionDialog::accept() {
    const auto validated = ModelConnections::validateConnection(connection());

    if (!validated.hasValue()) {
        showValidation(validationMessage(validated.error()));
        return;
    }

    if (m_takenKeys.contains(ModelConnections::connectionKey(validated.value()))) {
        showValidation(m_host.translate(QStringLiteral("ai.validation.connection-duplicate")));
        return;
    }

    QDialog::accept();
}

void AiConnectionDialog::showValidation(const QString& message) {
    m_validation->setText(message);
    m_validation->setVisible(!message.isEmpty());
    ui::Components::growDialogToContents(this);
}

QString AiConnectionDialog::validationMessage(const Error& error) const {
    if (error.code == QStringLiteral("ai_api_key_missing")) {
        return m_host.translate(QStringLiteral("ai.validation.api-key"));
    }
    if (error.code == QStringLiteral("ai_model_invalid")) {
        return m_host.translate(QStringLiteral("ai.validation.model"));
    }
    if (error.code == QStringLiteral("ai_output_budget_unknown")) {
        return m_host.translate(QStringLiteral("ai.validation.output-budget-unknown")).arg(error.detail);
    }
    if (error.code == QStringLiteral("ai_output_budget_whole_window")) {
        return m_host.translate(QStringLiteral("ai.validation.output-budget-whole-window")).arg(error.detail);
    }
    if (error.code == QStringLiteral("ai_address_invalid")) {
        return m_host.translate(QStringLiteral("ai.validation.connection-address"));
    }
    if (error.code == QStringLiteral("ai_model_discovery_empty")) {
        return m_host.translate(QStringLiteral("ai.validation.no-model-published"));
    }
    if (error.code == QStringLiteral("ai_extra_parameter_shape")) {
        return m_host.translate(QStringLiteral("ai.validation.extra-parameters-object"));
    }
    if (error.code == QStringLiteral("ai_extra_parameter_syntax")) {
        return m_host.translate(QStringLiteral("ai.validation.extra-parameters-syntax")).arg(error.message, error.detail);
    }
    if (error.code == QStringLiteral("ai_extra_parameter_invalid")) {
        return m_host.translate(QStringLiteral("ai.validation.extra-parameters-key"));
    }

    return error.message;
}

} // namespace workpane::plugins::ai
