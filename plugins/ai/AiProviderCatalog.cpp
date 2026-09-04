#include "AiProviderCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <optional>

namespace workpane::plugins::ai {

// The catalog is the only source of what a provider is, so every value below arrives from the files and never from this code.
struct LoadedAiCatalog final {
    AiCatalog catalog;
    Result<void> error = Result<void>::success();
};

class AiProviderCatalogHelper final {
  public:
    static Error invalid(const QString& message, const QString& detail);
    static Result<QJsonObject> parseDocument(const QByteArray& contents);
    static QByteArray resourceContents(const QString& path);
    static std::optional<ModelTrait> traitFromIdentifier(const QString& identifier);
    static std::optional<WireProtocol> protocolFromIdentifier(const QString& identifier);
    static Result<CommandLineDescriptor> commandLineDescriptor(const QJsonObject& document, const QString& providerId);
    static std::optional<ParameterType> parameterTypeFromIdentifier(const QString& identifier);
    static Result<QSet<ModelTrait>> traitSet(const QJsonValue& value, const QString& detail);
    static Result<std::optional<double>> readModelCost(const QJsonObject& entry, const QString& key, const QString& modelId);
    static Result<QStringList> stringList(const QJsonValue& value, const QString& detail);
    static Result<QMap<QString, QString>> stringMap(const QJsonValue& value, const QString& detail);
    static Result<QVector<ParameterOption>> parameterOptions(const QJsonValue& value, const QString& detail);
    static Result<void> boundedNumber(const QJsonObject& document, ParameterDescriptor& descriptor, const QString& detail);
    static Result<ParameterDescriptor> parameter(const QJsonObject& document, const QString& providerId);
    static Result<QVector<ParameterDescriptor>> parameterSet(const QJsonValue& value, const QString& providerId);
    static Result<ProviderDescriptor> provider(const QJsonObject& document);
    static Result<QMap<ModelEndpoint, EndpointDescriptor>> endpointSet(const QJsonValue& value, const QString& providerId);
    static Result<AiLimits> limits(const QJsonObject& document);
    static Result<QStringList> promptTemplates(const QJsonArray& declared);
    static bool lowercaseIdentifier(const QString& identifier);
    static Result<QVector<ModelDescriptor>> importedModels(const QJsonArray& entries, const QString& providerId, bool reachedOverAWire);
    static Result<void> mergeImportedModels(QVector<ProviderDescriptor>& providers, const QByteArray& modelsDocument);
    static LoadedAiCatalog load();
    static const LoadedAiCatalog& loadedCatalog();
};

Error AiProviderCatalogHelper::invalid(const QString& message, const QString& detail) {
    return {"ai_catalog_invalid", message, detail};
}

Result<QJsonObject> AiProviderCatalogHelper::parseDocument(const QByteArray& contents) {
    if (contents.isEmpty()) {
        return Result<QJsonObject>::failure(invalid(QStringLiteral("The AI catalog file is unavailable"), {}));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Result<QJsonObject>::failure(invalid(QStringLiteral("The AI catalog file is not a catalog"), parseError.errorString()));
    }

    return Result<QJsonObject>::success(document.object());
}

QByteArray AiProviderCatalogHelper::resourceContents(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

std::optional<ModelTrait> AiProviderCatalogHelper::traitFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("sampling")) {
        return ModelTrait::Sampling;
    }
    if (identifier == QStringLiteral("reasoning")) {
        return ModelTrait::Reasoning;
    }
    if (identifier == QStringLiteral("function-calling")) {
        return ModelTrait::FunctionCalling;
    }
    if (identifier == QStringLiteral("vision")) {
        return ModelTrait::Vision;
    }
    if (identifier == QStringLiteral("system-prompt")) {
        return ModelTrait::SystemPrompt;
    }

    return std::nullopt;
}

QString ProviderCatalog::modelTraitIdentifier(ModelTrait trait) {
    switch (trait) {
    case ModelTrait::Sampling:
        return QStringLiteral("sampling");
    case ModelTrait::Reasoning:
        return QStringLiteral("reasoning");
    case ModelTrait::FunctionCalling:
        return QStringLiteral("function-calling");
    case ModelTrait::Vision:
        return QStringLiteral("vision");
    case ModelTrait::SystemPrompt:
        return QStringLiteral("system-prompt");
    }

    return QStringLiteral("sampling");
}

std::optional<WireProtocol> AiProviderCatalogHelper::protocolFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("anthropic")) {
        return WireProtocol::Anthropic;
    }
    if (identifier == QStringLiteral("openai-compatible")) {
        return WireProtocol::OpenAiCompatible;
    }
    if (identifier == QStringLiteral("command-line")) {
        return WireProtocol::CommandLine;
    }

    return std::nullopt;
}

// The prompt and the working directory are declared where they go, so neither is spliced into a string a shell would read.
Result<CommandLineDescriptor> AiProviderCatalogHelper::commandLineDescriptor(const QJsonObject& document, const QString& providerId) {
    const QJsonObject declared = document.value(QStringLiteral("command")).toObject();

    if (!document.value(QStringLiteral("command")).isObject() || !SettingsReaders::hasExactKeys(declared, {QStringLiteral("program"), QStringLiteral("arguments"), QStringLiteral("clearedVariables")})) {
        return Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares no program to run"), providerId));
    }

    CommandLineDescriptor commandLine;

    if (!SettingsReaders::readSettingsText(declared, QStringLiteral("program"), commandLine.program) || commandLine.program.isEmpty() || !declared.value(QStringLiteral("arguments")).isArray()) {
        return Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares no program to run"), providerId));
    }

    for (const auto& value : declared.value(QStringLiteral("arguments")).toArray()) {
        if (!value.isString() || value.toString().isEmpty()) {
            return Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares an empty argument"), providerId));
        }

        commandLine.arguments.append(value.toString());
    }

    if (!declared.value(QStringLiteral("clearedVariables")).isArray()) {
        return Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares no variables to clear"), providerId));
    }

    for (const auto& value : declared.value(QStringLiteral("clearedVariables")).toArray()) {
        if (!value.isString() || value.toString().trimmed().isEmpty()) {
            return Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares an empty variable to clear"), providerId));
        }

        commandLine.clearedVariables.append(value.toString());
    }

    if (!commandLine.arguments.contains(commandLinePromptPlaceholder)) {
        return Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider never passes the prompt"), providerId));
    }

    if (!commandLine.arguments.contains(commandLineModelPlaceholder)) {
        return Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider never passes the model"), providerId));
    }

    return Result<CommandLineDescriptor>::success(commandLine);
}

std::optional<ParameterType> AiProviderCatalogHelper::parameterTypeFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("integer")) {
        return ParameterType::Integer;
    }
    if (identifier == QStringLiteral("number")) {
        return ParameterType::Number;
    }
    if (identifier == QStringLiteral("enumeration")) {
        return ParameterType::Enumeration;
    }

    return std::nullopt;
}

// A price nobody published is absent rather than free, so a model without one reports no cost at all.
Result<std::optional<double>> AiProviderCatalogHelper::readModelCost(const QJsonObject& entry, const QString& key, const QString& modelId) {
    if (!entry.contains(key)) {
        return Result<std::optional<double>>::success(std::nullopt);
    }

    const QJsonValue value = entry.value(key);

    if (!value.isDouble() || value.toDouble() < 0.0) {
        return Result<std::optional<double>>::failure(invalid(QStringLiteral("A catalog model carries an invalid price"), modelId));
    }

    return Result<std::optional<double>>::success(value.toDouble());
}

Result<QSet<ModelTrait>> AiProviderCatalogHelper::traitSet(const QJsonValue& value, const QString& detail) {
    if (value.isUndefined()) {
        return Result<QSet<ModelTrait>>::success({});
    }
    if (!value.isArray()) {
        return Result<QSet<ModelTrait>>::failure(invalid(QStringLiteral("The declared trait set is not a list"), detail));
    }

    QSet<ModelTrait> traits;

    for (const auto& declared : value.toArray()) {
        const auto trait = traitFromIdentifier(declared.toString());
        if (!trait.has_value()) {
            return Result<QSet<ModelTrait>>::failure(invalid(QStringLiteral("The declared trait is unknown"), detail));
        }
        traits.insert(*trait);
    }
    // A parameter set built for a sampling model excludes the one built for a reasoning model, so no model carries both.
    if (traits.contains(ModelTrait::Sampling) && traits.contains(ModelTrait::Reasoning)) {
        return Result<QSet<ModelTrait>>::failure(invalid(QStringLiteral("The declared traits combine sampling and reasoning"), detail));
    }

    return Result<QSet<ModelTrait>>::success(traits);
}

Result<QStringList> AiProviderCatalogHelper::stringList(const QJsonValue& value, const QString& detail) {
    if (!value.isArray()) {
        return Result<QStringList>::failure(invalid(QStringLiteral("The declared list is not a list"), detail));
    }

    QStringList values;

    for (const auto& entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty() || values.contains(entry.toString())) {
            return Result<QStringList>::failure(invalid(QStringLiteral("The declared list carries an invalid entry"), detail));
        }
        values.append(entry.toString());
    }

    return Result<QStringList>::success(values);
}

// A field a provider does not carry is the declared default, so a command line agent declares only what applies to it.
Result<QMap<QString, QString>> AiProviderCatalogHelper::stringMap(const QJsonValue& value, const QString& detail) {
    if (value.isUndefined()) {
        return Result<QMap<QString, QString>>::success({});
    }
    if (!value.isObject()) {
        return Result<QMap<QString, QString>>::failure(invalid(QStringLiteral("The declared map is not a map"), detail));
    }

    QMap<QString, QString> values;
    const QJsonObject document = value.toObject();

    for (auto entry = document.constBegin(); entry != document.constEnd(); ++entry) {
        if (entry.key().isEmpty() || !entry.value().isString()) {
            return Result<QMap<QString, QString>>::failure(invalid(QStringLiteral("The declared map carries an invalid entry"), detail));
        }
        values.insert(entry.key(), entry.value().toString());
    }

    return Result<QMap<QString, QString>>::success(values);
}

Result<QVector<ParameterOption>> AiProviderCatalogHelper::parameterOptions(const QJsonValue& value, const QString& detail) {
    if (!value.isArray() || value.toArray().isEmpty()) {
        return Result<QVector<ParameterOption>>::failure(invalid(QStringLiteral("The enumeration declares no option"), detail));
    }

    QVector<ParameterOption> options;

    for (const auto& entry : value.toArray()) {
        const QJsonObject document = entry.toObject();
        ParameterOption option;
        const bool typed = entry.isObject() && SettingsReaders::hasExactKeys(document, {QStringLiteral("id"), QStringLiteral("title")}) && SettingsReaders::readSettingsText(document, QStringLiteral("id"), option.id) && SettingsReaders::readSettingsText(document, QStringLiteral("title"), option.titleKey);
        // clang-format off
        const auto duplicate = std::find_if(options.cbegin(), options.cend(), [&option](const ParameterOption& existing) { return existing.id == option.id; });
        // clang-format on
        if (!typed || option.id.isEmpty() || option.titleKey.isEmpty() || duplicate != options.cend()) {
            return Result<QVector<ParameterOption>>::failure(invalid(QStringLiteral("The enumeration option is invalid"), detail));
        }
        options.append(option);
    }

    return Result<QVector<ParameterOption>>::success(options);
}

Result<void> AiProviderCatalogHelper::boundedNumber(const QJsonObject& document, ParameterDescriptor& descriptor, const QString& detail) {
    if (!document.value(QStringLiteral("minimum")).isDouble() || !document.value(QStringLiteral("maximum")).isDouble()) {
        return Result<void>::failure(invalid(QStringLiteral("The numeric parameter declares no bounds"), detail));
    }

    descriptor.minimum = document.value(QStringLiteral("minimum")).toDouble();
    descriptor.maximum = document.value(QStringLiteral("maximum")).toDouble();
    const double value = descriptor.defaultValue.toDouble(descriptor.minimum - 1.0);

    if (descriptor.minimum > descriptor.maximum || !descriptor.defaultValue.isDouble() || value < descriptor.minimum || value > descriptor.maximum) {
        return Result<void>::failure(invalid(QStringLiteral("The numeric parameter default is outside its bounds"), detail));
    }
    if (descriptor.type == ParameterType::Integer && value != static_cast<double>(descriptor.defaultValue.toInteger())) {
        return Result<void>::failure(invalid(QStringLiteral("The integer parameter default is not whole"), detail));
    }

    return Result<void>::success();
}

Result<ParameterDescriptor> AiProviderCatalogHelper::parameter(const QJsonObject& document, const QString& providerId) {
    const QSet<QString> known{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("type"), QStringLiteral("field"), QStringLiteral("trait"), QStringLiteral("boundByModelOutput"), QStringLiteral("modelMaximumWhenZero"), QStringLiteral("minimum"), QStringLiteral("maximum"), QStringLiteral("default"), QStringLiteral("options")};

    if (!SettingsReaders::hasKnownKeys(document, known)) {
        return Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A declared parameter carries an unknown value"), providerId));
    }

    ParameterDescriptor descriptor;
    QString type;
    QString trait;
    const bool typed = SettingsReaders::readSettingsText(document, QStringLiteral("id"), descriptor.id) && SettingsReaders::readSettingsText(document, QStringLiteral("title"), descriptor.titleKey) && SettingsReaders::readSettingsText(document, QStringLiteral("field"), descriptor.field) && SettingsReaders::readSettingsText(document, QStringLiteral("type"), type) && SettingsReaders::readSettingsText(document, QStringLiteral("trait"), trait) && SettingsReaders::readSettingsBool(document, QStringLiteral("boundByModelOutput"), descriptor.boundByModelOutput) && SettingsReaders::readSettingsBool(document, QStringLiteral("modelMaximumWhenZero"), descriptor.modelMaximumWhenZero);
    const auto parsedType = parameterTypeFromIdentifier(type);
    const QString detail = providerId + QLatin1Char('.') + descriptor.id;

    if (!typed || descriptor.id.isEmpty() || descriptor.titleKey.isEmpty() || descriptor.field.isEmpty() || !parsedType.has_value() || descriptor.field.split(QLatin1Char('.')).contains(QString{})) {
        return Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A declared parameter is invalid"), detail));
    }

    if (document.contains(QStringLiteral("trait"))) {
        const auto parsedTrait = traitFromIdentifier(trait);
        if (!parsedTrait.has_value()) {
            return Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A declared parameter names an unknown trait"), detail));
        }
        descriptor.requiredTrait = parsedTrait;
    }

    descriptor.type = *parsedType;
    descriptor.defaultValue = document.value(QStringLiteral("default"));
    const bool numeric = descriptor.type == ParameterType::Integer || descriptor.type == ParameterType::Number;

    if (!numeric && (document.contains(QStringLiteral("minimum")) || document.contains(QStringLiteral("maximum")))) {
        return Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A parameter that is not numeric declares bounds"), detail));
    }
    if (descriptor.type != ParameterType::Enumeration && document.contains(QStringLiteral("options"))) {
        return Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A parameter that is not an enumeration declares options"), detail));
    }

    if (numeric) {
        const auto bounds = boundedNumber(document, descriptor, detail);
        return bounds.hasValue() ? Result<ParameterDescriptor>::success(descriptor) : Result<ParameterDescriptor>::failure(bounds.error());
    }

    const auto options = parameterOptions(document.value(QStringLiteral("options")), detail);

    if (!options.hasValue()) {
        return Result<ParameterDescriptor>::failure(options.error());
    }

    descriptor.options = options.value();
    // clang-format off
    const auto selected = std::find_if(descriptor.options.cbegin(), descriptor.options.cend(), [&descriptor](const ParameterOption& option) { return option.id == descriptor.defaultValue.toString(); });
    // clang-format on

    if (selected == descriptor.options.cend()) {
        return Result<ParameterDescriptor>::failure(invalid(QStringLiteral("The enumeration default is not one of its options"), detail));
    }

    return Result<ParameterDescriptor>::success(descriptor);
}

// Two entries may share one identifier only when a model reaches exactly one of them, which is what distinct required traits guarantee.
Result<QVector<ParameterDescriptor>> AiProviderCatalogHelper::parameterSet(const QJsonValue& value, const QString& providerId) {
    if (!value.isArray() || value.toArray().isEmpty()) {
        return Result<QVector<ParameterDescriptor>>::failure(invalid(QStringLiteral("The provider declares no parameter"), providerId));
    }

    QVector<ParameterDescriptor> parameters;

    for (const auto& entry : value.toArray()) {
        if (!entry.isObject()) {
            return Result<QVector<ParameterDescriptor>>::failure(invalid(QStringLiteral("A declared parameter is not an object"), providerId));
        }
        const auto descriptor = parameter(entry.toObject(), providerId);
        if (!descriptor.hasValue()) {
            return Result<QVector<ParameterDescriptor>>::failure(descriptor.error());
        }
        for (const auto& existing : parameters) {
            if (existing.id != descriptor.value().id) {
                continue;
            }
            if (!existing.requiredTrait.has_value() || !descriptor.value().requiredTrait.has_value() || existing.requiredTrait == descriptor.value().requiredTrait) {
                return Result<QVector<ParameterDescriptor>>::failure(invalid(QStringLiteral("A parameter identifier is declared twice for the same model"), providerId + QLatin1Char('.') + existing.id));
            }
        }
        parameters.append(descriptor.value());
    }

    return Result<QVector<ParameterDescriptor>>::success(parameters);
}

std::optional<ModelEndpoint> ProviderCatalog::modelEndpointFromName(const QString& name) {
    if (name == QStringLiteral("chat")) {
        return ModelEndpoint::Chat;
    }
    if (name == QStringLiteral("image")) {
        return ModelEndpoint::Image;
    }
    if (name == QStringLiteral("speech")) {
        return ModelEndpoint::Speech;
    }

    return std::nullopt;
}

Result<QMap<ModelEndpoint, EndpointDescriptor>> AiProviderCatalogHelper::endpointSet(const QJsonValue& value, const QString& providerId) {
    QMap<ModelEndpoint, EndpointDescriptor> endpoints;

    if (value.isUndefined()) {
        return Result<QMap<ModelEndpoint, EndpointDescriptor>>::success(endpoints);
    }
    if (!value.isObject()) {
        return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("A provider declares endpoints that are not a set"), providerId));
    }

    const QJsonObject declared = value.toObject();

    for (auto entry = declared.constBegin(); entry != declared.constEnd(); ++entry) {
        const auto endpoint = ProviderCatalog::modelEndpointFromName(entry.key());

        if (!endpoint.has_value() || !entry.value().isObject()) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("A provider declares an endpoint nobody answers"), entry.key()));
        }

        const QJsonObject document = entry.value().toObject();

        if (!SettingsReaders::hasKnownKeys(document, {QStringLiteral("path"), QStringLiteral("voices"), QStringLiteral("defaultVoice"), QStringLiteral("voiceCatalogPath"), QStringLiteral("authHeader"), QStringLiteral("authPrefix"), QStringLiteral("textField"), QStringLiteral("voiceField"), QStringLiteral("model"), QStringLiteral("body")})) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("A declared endpoint carries an unknown value"), entry.key()));
        }

        EndpointDescriptor descriptor;

        if (!SettingsReaders::readSettingsText(document, QStringLiteral("path"), descriptor.path) || descriptor.path.isEmpty() || !descriptor.path.startsWith(QLatin1Char('/'))) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("A declared endpoint carries no path"), entry.key()));
        }

        if (document.contains(QStringLiteral("voices"))) {
            const auto voices = stringList(document.value(QStringLiteral("voices")), providerId);

            if (!voices.hasValue()) {
                return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(voices.error());
            }

            descriptor.voices = voices.value();
        }
        SettingsReaders::readSettingsText(document, QStringLiteral("defaultVoice"), descriptor.defaultVoice);
        SettingsReaders::readSettingsText(document, QStringLiteral("voiceCatalogPath"), descriptor.voiceCatalogPath);
        SettingsReaders::readSettingsText(document, QStringLiteral("authHeader"), descriptor.authHeader);
        SettingsReaders::readSettingsText(document, QStringLiteral("authPrefix"), descriptor.authPrefix);
        SettingsReaders::readSettingsText(document, QStringLiteral("textField"), descriptor.textField);
        SettingsReaders::readSettingsText(document, QStringLiteral("voiceField"), descriptor.voiceField);
        SettingsReaders::readSettingsText(document, QStringLiteral("model"), descriptor.model);
        SettingsReaders::readSettingsObject(document, QStringLiteral("body"), descriptor.body);

        // A voice belongs to the endpoint that speaks, so no other one declares any.
        if (endpoint.value() != ModelEndpoint::Speech && (!descriptor.voices.isEmpty() || !descriptor.defaultVoice.isEmpty() || !descriptor.voiceCatalogPath.isEmpty() || !descriptor.voiceField.isEmpty())) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("An endpoint that does not speak declares a voice"), entry.key()));
        }
        if (!descriptor.voices.isEmpty() && !descriptor.voices.contains(descriptor.defaultVoice)) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("A speaking endpoint declares a default voice it does not offer"), providerId));
        }

        // A voice is either offered from a closed set or read from the account catalog of the service, so a speaking endpoint declares exactly one of them.
        if (endpoint.value() == ModelEndpoint::Speech && descriptor.voices.isEmpty() == descriptor.voiceCatalogPath.isEmpty()) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("A speaking endpoint declares neither a voice set nor a voice catalog, or declares both"), providerId));
        }

        // A conversation is written by the client that holds it, so only an endpoint answering a single request declares how that request is built.
        const bool carriesARequest = endpoint.value() != ModelEndpoint::Chat;

        if (carriesARequest != (!descriptor.authHeader.isEmpty() && !descriptor.textField.isEmpty())) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("An endpoint disagrees with itself about carrying a request of its own"), entry.key()));
        }
        if (!carriesARequest && (!descriptor.body.isEmpty() || !descriptor.model.isEmpty())) {
            return Result<QMap<ModelEndpoint, EndpointDescriptor>>::failure(invalid(QStringLiteral("A conversation endpoint declares a request body"), entry.key()));
        }

        endpoints.insert(endpoint.value(), descriptor);
    }

    return Result<QMap<ModelEndpoint, EndpointDescriptor>>::success(endpoints);
}

Result<ProviderDescriptor> AiProviderCatalogHelper::provider(const QJsonObject& document) {
    const QSet<QString> known{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("protocol"), QStringLiteral("baseUrl"), QStringLiteral("addressConfigurable"), QStringLiteral("apiKeyVariable"), QStringLiteral("requiresApiKey"), QStringLiteral("userDefinedTraits"), QStringLiteral("preferredModels"), QStringLiteral("requestMaxRetries"), QStringLiteral("streamIdleTimeoutMs"), QStringLiteral("headers"), QStringLiteral("queryParameters"), QStringLiteral("parameters"), QStringLiteral("command"), QStringLiteral("endpoints")};

    if (!SettingsReaders::hasKnownKeys(document, known)) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A declared provider carries an unknown value"), {}));
    }

    ProviderDescriptor descriptor;
    QString protocol;
    const bool typed = SettingsReaders::readSettingsText(document, QStringLiteral("id"), descriptor.id) && SettingsReaders::readSettingsText(document, QStringLiteral("title"), descriptor.titleKey) && SettingsReaders::readSettingsText(document, QStringLiteral("protocol"), protocol) && SettingsReaders::readSettingsText(document, QStringLiteral("baseUrl"), descriptor.baseUrl) && SettingsReaders::readSettingsText(document, QStringLiteral("apiKeyVariable"), descriptor.apiKeyVariable) && SettingsReaders::readSettingsBool(document, QStringLiteral("addressConfigurable"), descriptor.addressConfigurable) && SettingsReaders::readSettingsBool(document, QStringLiteral("requiresApiKey"), descriptor.requiresApiKey) && SettingsReaders::readSettingsInteger(document, QStringLiteral("requestMaxRetries"), descriptor.requestMaxRetries) && SettingsReaders::readSettingsInteger(document, QStringLiteral("streamIdleTimeoutMs"), descriptor.streamIdleTimeoutMs);
    const auto parsedProtocol = protocolFromIdentifier(protocol);
    // A protocol says how a conversation is held, so a provider that holds none declares one only to say something untrue.
    const bool holdsAConversation = document.contains(QStringLiteral("protocol"));

    if (!typed || descriptor.id.isEmpty() || descriptor.titleKey.isEmpty() || (holdsAConversation && !parsedProtocol.has_value()) || descriptor.requestMaxRetries < 0 || descriptor.requestMaxRetries > 10 || descriptor.streamIdleTimeoutMs < 1000 || descriptor.streamIdleTimeoutMs > 600000) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A declared provider is invalid"), descriptor.id));
    }
    // A provider that requires a credential names the variable that credential officially lives in.
    if (descriptor.requiresApiKey && descriptor.apiKeyVariable.isEmpty()) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider requiring a credential names no environment variable"), descriptor.id));
    }

    // A provider that declares no protocol holds no conversation, so nothing ever asks it how one is held.
    if (holdsAConversation) {
        descriptor.protocol = *parsedProtocol;
    }

    if (descriptor.protocol == WireProtocol::CommandLine) {
        const auto commandLine = commandLineDescriptor(document, descriptor.id);

        if (!commandLine.hasValue()) {
            return Result<ProviderDescriptor>::failure(commandLine.error());
        }

        descriptor.commandLine = commandLine.value();
    } else {
        const QUrl address(descriptor.baseUrl);

        if (!address.isValid() || address.scheme().isEmpty() || document.contains(QStringLiteral("command"))) {
            return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider reached over a wire declares no address or declares a program"), descriptor.id));
        }
    }

    const bool converses = holdsAConversation && descriptor.protocol != WireProtocol::CommandLine;
    const auto traits = traitSet(document.value(QStringLiteral("userDefinedTraits")), descriptor.id);

    if (!traits.hasValue()) {
        return Result<ProviderDescriptor>::failure(traits.error());
    }
    // A trait belongs to a model, and only a provider that holds a conversation over a wire is asked for one, so a command line agent and a media service declare none.
    if (converses && !traits.value().contains(ModelTrait::FunctionCalling)) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider declares a user-defined model that calls no tool"), descriptor.id));
    }

    descriptor.userDefinedModelTraits = traits.value();

    // A model answers a conversation, so a provider that holds none opens with no model and declares no trait for one.
    const auto preferred = holdsAConversation ? stringList(document.value(QStringLiteral("preferredModels")), descriptor.id) : Result<QStringList>::success({});
    const auto headers = stringMap(document.value(QStringLiteral("headers")), descriptor.id);
    const auto queries = stringMap(document.value(QStringLiteral("queryParameters")), descriptor.id);
    // A parameter tunes a conversation, so only a provider that holds one over a wire declares any.
    const auto parameters = converses ? parameterSet(document.value(QStringLiteral("parameters")), descriptor.id) : Result<QVector<ParameterDescriptor>>::success({});

    if (!preferred.hasValue() || !headers.hasValue() || !queries.hasValue() || !parameters.hasValue()) {
        return Result<ProviderDescriptor>::failure(!preferred.hasValue() ? preferred.error() : (!headers.hasValue() ? headers.error() : (!queries.hasValue() ? queries.error() : parameters.error())));
    }

    descriptor.preferredModels = preferred.value();
    descriptor.httpHeaders = headers.value();
    descriptor.queryParameters = queries.value();
    descriptor.parameters = parameters.value();

    const auto endpoints = endpointSet(document.value(QStringLiteral("endpoints")), descriptor.id);

    if (!endpoints.hasValue()) {
        return Result<ProviderDescriptor>::failure(endpoints.error());
    }

    descriptor.endpoints = endpoints.value();

    if (!converses && document.contains(QStringLiteral("parameters"))) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider that holds no conversation declares a parameter"), descriptor.id));
    }
    if (!holdsAConversation && (document.contains(QStringLiteral("preferredModels")) || document.contains(QStringLiteral("userDefinedTraits")))) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider that holds no conversation declares a model or a trait"), descriptor.id));
    }

    // A conversation is held either at a declared endpoint or by invoking a program, so a provider that declares a protocol answers one of the two.
    if (holdsAConversation != (descriptor.protocol == WireProtocol::CommandLine || descriptor.endpoints.contains(ModelEndpoint::Chat))) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider disagrees with itself about holding a conversation"), descriptor.id));
    }

    // A provider nobody can reach for anything answers nothing, so every one of them declares at least one modality.
    if (!holdsAConversation && descriptor.endpoints.isEmpty()) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider answers no modality at all"), descriptor.id));
    }

    // A command line agent is invoked rather than reached, so it answers no endpoint of its own.
    if (descriptor.protocol == WireProtocol::CommandLine && !descriptor.endpoints.isEmpty()) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A command line provider declares an endpoint"), descriptor.id));
    }
    if (descriptor.protocol == WireProtocol::CommandLine && (!descriptor.parameters.isEmpty() || descriptor.requiresApiKey || descriptor.addressConfigurable || !descriptor.baseUrl.isEmpty())) {
        return Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A command line provider declares an address, a credential or a parameter"), descriptor.id));
    }

    return Result<ProviderDescriptor>::success(descriptor);
}

bool AiProviderCatalogHelper::lowercaseIdentifier(const QString& identifier) {
    if (identifier.isEmpty() || identifier.startsWith(QLatin1Char('-')) || identifier.endsWith(QLatin1Char('-'))) {
        return false;
    }

    for (const QChar character : identifier) {
        const bool allowed = (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('0') && character <= QLatin1Char('9')) || character == QLatin1Char('-');

        if (!allowed) {
            return false;
        }
    }

    return true;
}

// A template identifier names the keys that carry its text, so it is spelled the way every other identifier of the project is.
Result<QStringList> AiProviderCatalogHelper::promptTemplates(const QJsonArray& declared) {
    QStringList templates;

    for (const auto& entry : declared) {
        if (!entry.isString()) {
            return Result<QStringList>::failure(invalid(QStringLiteral("A declared prompt template is not a string"), {}));
        }

        const QString identifier = entry.toString();

        if (!lowercaseIdentifier(identifier)) {
            return Result<QStringList>::failure(invalid(QStringLiteral("A declared prompt template identifier is invalid"), identifier));
        }
        if (templates.contains(identifier)) {
            return Result<QStringList>::failure(invalid(QStringLiteral("A prompt template identifier is declared twice"), identifier));
        }

        templates.append(identifier);
    }

    if (templates.isEmpty()) {
        return Result<QStringList>::failure(invalid(QStringLiteral("The catalog declares no prompt template"), {}));
    }

    return Result<QStringList>::success(templates);
}

Result<AiLimits> AiProviderCatalogHelper::limits(const QJsonObject& document) {
    const QSet<QString> known{QStringLiteral("repeatedToolCallLimit"), QStringLiteral("summaryMaximumTokens"), QStringLiteral("toolDeadlineMs"), QStringLiteral("requestTimeoutMs"), QStringLiteral("discoveryTimeoutMs"), QStringLiteral("serverStartTimeoutMs"), QStringLiteral("scheduleWakeupMs"), QStringLiteral("maximumAgentIterations"), QStringLiteral("maximumCommandTimeoutSeconds"), QStringLiteral("maximumParallelExecutions"), QStringLiteral("maximumSamplingTokens"), QStringLiteral("maximumRequestDelayMs"), QStringLiteral("maximumRequestsPerMinute"), QStringLiteral("maximumConcurrentRequests"), QStringLiteral("retryBackoffMs"), QStringLiteral("maximumRetryBackoffMs")};

    if (!SettingsReaders::hasExactKeys(document, known)) {
        return Result<AiLimits>::failure(invalid(QStringLiteral("The AI catalog limits are incomplete"), {}));
    }

    bool valid = true;
    // clang-format off
    const auto read = [&document, &valid](const QString& key, int minimum, int maximum) {
        const QJsonValue value = document.value(key);
        if (!value.isDouble() || value.toInteger() < minimum || value.toInteger() > maximum) {
            valid = false;
            return 0;
        }
        return static_cast<int>(value.toInteger());
    };
    // clang-format on

    AiLimits values;
    values.repeatedToolCallLimit = read(QStringLiteral("repeatedToolCallLimit"), 1, 100);
    values.summaryMaximumTokens = read(QStringLiteral("summaryMaximumTokens"), 128, 100000);
    values.toolDeadlineMs = read(QStringLiteral("toolDeadlineMs"), 1000, 3600000);
    values.requestTimeoutMs = read(QStringLiteral("requestTimeoutMs"), 1000, 3600000);
    values.discoveryTimeoutMs = read(QStringLiteral("discoveryTimeoutMs"), 1000, 600000);
    values.serverStartTimeoutMs = read(QStringLiteral("serverStartTimeoutMs"), 1000, 600000);
    values.scheduleWakeupMs = read(QStringLiteral("scheduleWakeupMs"), 1000, 3600000);
    values.maximumAgentIterations = read(QStringLiteral("maximumAgentIterations"), 1, 100000);
    values.maximumCommandTimeoutSeconds = read(QStringLiteral("maximumCommandTimeoutSeconds"), 1, 604800);
    values.maximumParallelExecutions = read(QStringLiteral("maximumParallelExecutions"), 1, 1024);
    values.maximumSamplingTokens = read(QStringLiteral("maximumSamplingTokens"), 128, 10000000);
    values.maximumRequestDelayMs = read(QStringLiteral("maximumRequestDelayMs"), 1000, 3600000);
    values.maximumRequestsPerMinute = read(QStringLiteral("maximumRequestsPerMinute"), 1, 1000000);
    values.maximumConcurrentRequests = read(QStringLiteral("maximumConcurrentRequests"), 1, 1024);
    values.retryBackoffMs = read(QStringLiteral("retryBackoffMs"), 100, 60000);
    values.maximumRetryBackoffMs = read(QStringLiteral("maximumRetryBackoffMs"), 1000, 600000);

    if (!valid) {
        return Result<AiLimits>::failure(invalid(QStringLiteral("The AI catalog limits are invalid"), {}));
    }

    return Result<AiLimits>::success(values);
}

// The model list is data, so a model is added by one line in the catalog file and never by interface code.
Result<QVector<ModelDescriptor>> AiProviderCatalogHelper::importedModels(const QJsonArray& entries, const QString& providerId, bool reachedOverAWire) {
    QVector<ModelDescriptor> models;

    for (const auto& value : entries) {
        const QJsonObject entry = value.toObject();
        if (!SettingsReaders::hasKnownKeys(entry, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("context"), QStringLiteral("output"), QStringLiteral("traits"), QStringLiteral("inputCost"), QStringLiteral("outputCost")})) {
            return Result<QVector<ModelDescriptor>>::failure(invalid(QStringLiteral("A catalog model carries an unknown value"), providerId));
        }

        ModelDescriptor model;
        model.displayName = entry.value(QStringLiteral("id")).toString();
        const bool typed = SettingsReaders::readSettingsText(entry, QStringLiteral("id"), model.id) && SettingsReaders::readSettingsText(entry, QStringLiteral("name"), model.displayName) && SettingsReaders::readSettingsInteger(entry, QStringLiteral("context"), model.contextWindow) && SettingsReaders::readSettingsInteger(entry, QStringLiteral("output"), model.maximumOutputTokens);
        if (!typed || model.id.isEmpty() || model.contextWindow <= 0 || model.maximumOutputTokens <= 0) {
            return Result<QVector<ModelDescriptor>>::failure(invalid(QStringLiteral("A catalog model is invalid"), model.id.isEmpty() ? providerId : model.id));
        }

        const auto inputCost = readModelCost(entry, QStringLiteral("inputCost"), model.id);
        const auto outputCost = readModelCost(entry, QStringLiteral("outputCost"), model.id);
        if (!inputCost.hasValue()) {
            return Result<QVector<ModelDescriptor>>::failure(inputCost.error());
        }
        if (!outputCost.hasValue()) {
            return Result<QVector<ModelDescriptor>>::failure(outputCost.error());
        }

        model.inputCostPerToken = inputCost.value();
        model.outputCostPerToken = outputCost.value();

        const auto traits = traitSet(entry.value(QStringLiteral("traits")), model.id);
        if (!traits.hasValue()) {
            return Result<QVector<ModelDescriptor>>::failure(traits.error());
        }
        model.traits = traits.value();
        // A model reached over a wire that calls no tool cannot run a task, while a command line agent runs its own.
        if (reachedOverAWire && !model.traits.contains(ModelTrait::FunctionCalling)) {
            return Result<QVector<ModelDescriptor>>::failure(invalid(QStringLiteral("A catalog model declares no tool calling"), model.id));
        }
        models.append(model);
    }

    return Result<QVector<ModelDescriptor>>::success(models);
}

Result<void> AiProviderCatalogHelper::mergeImportedModels(QVector<ProviderDescriptor>& providers, const QByteArray& modelsDocument) {
    const auto document = parseDocument(modelsDocument);

    if (!document.hasValue()) {
        return Result<void>::failure(document.error());
    }
    if (!SettingsReaders::hasExactKeys(document.value(), {QStringLiteral("providers")}) || !document.value().value(QStringLiteral("providers")).isObject()) {
        return Result<void>::failure(invalid(QStringLiteral("The AI model catalog is not a catalog"), {}));
    }

    const QJsonObject declared = document.value().value(QStringLiteral("providers")).toObject();

    for (auto entry = declared.constBegin(); entry != declared.constEnd(); ++entry) {
        // clang-format off
        const auto position = std::find_if(providers.begin(), providers.end(), [&entry](const ProviderDescriptor& provider) { return provider.id == entry.key(); });
        // clang-format on
        if (position == providers.end()) {
            return Result<void>::failure(invalid(QStringLiteral("The AI model catalog names an unknown provider"), entry.key()));
        }
        if (!entry.value().isArray()) {
            return Result<void>::failure(invalid(QStringLiteral("The AI model catalog entry is not a list"), entry.key()));
        }

        const auto imported = importedModels(entry.value().toArray(), entry.key(), position->protocol != WireProtocol::CommandLine);
        if (!imported.hasValue()) {
            return Result<void>::failure(imported.error());
        }

        // The file owns what every model is, while the provider declares only which of them it opens with.
        const QVector<ModelDescriptor> models = imported.value();
        for (const auto& preferred : position->preferredModels) {
            // clang-format off
            const auto model = std::find_if(models.cbegin(), models.cend(), [&preferred](const ModelDescriptor& candidate) { return candidate.id == preferred; });
            // clang-format on
            if (model == models.cend()) {
                return Result<void>::failure(invalid(QStringLiteral("A provider prefers a model the catalog does not declare"), preferred));
            }
            position->models.append(*model);
        }
        for (const auto& model : models) {
            if (!position->preferredModels.contains(model.id)) {
                position->models.append(model);
            }
        }
    }

    return Result<void>::success();
}

Result<AiCatalog> ProviderCatalog::loadAiCatalog(const QByteArray& providersDocument, const QByteArray& modelsDocument) {
    const auto document = AiProviderCatalogHelper::parseDocument(providersDocument);

    if (!document.hasValue()) {
        return Result<AiCatalog>::failure(document.error());
    }
    if (!SettingsReaders::hasExactKeys(document.value(), {QStringLiteral("limits"), QStringLiteral("promptTemplates"), QStringLiteral("providers")}) || !document.value().value(QStringLiteral("providers")).isArray() || !document.value().value(QStringLiteral("limits")).isObject() || !document.value().value(QStringLiteral("promptTemplates")).isArray()) {
        return Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("The AI provider catalog is not a catalog"), {}));
    }

    const auto declaredTemplates = AiProviderCatalogHelper::promptTemplates(document.value().value(QStringLiteral("promptTemplates")).toArray());

    if (!declaredTemplates.hasValue()) {
        return Result<AiCatalog>::failure(declaredTemplates.error());
    }

    const auto declaredLimits = AiProviderCatalogHelper::limits(document.value().value(QStringLiteral("limits")).toObject());

    if (!declaredLimits.hasValue()) {
        return Result<AiCatalog>::failure(declaredLimits.error());
    }

    AiCatalog catalog;
    catalog.limits = declaredLimits.value();
    catalog.promptTemplates = declaredTemplates.value();

    for (const auto& entry : document.value().value(QStringLiteral("providers")).toArray()) {
        if (!entry.isObject()) {
            return Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("A declared provider is not an object"), {}));
        }
        const auto descriptor = AiProviderCatalogHelper::provider(entry.toObject());
        if (!descriptor.hasValue()) {
            return Result<AiCatalog>::failure(descriptor.error());
        }
        // clang-format off
        const auto duplicate = std::find_if(catalog.providers.cbegin(), catalog.providers.cend(), [&descriptor](const ProviderDescriptor& existing) { return existing.id == descriptor.value().id; });
        // clang-format on
        if (duplicate != catalog.providers.cend()) {
            return Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("A provider identifier is declared twice"), descriptor.value().id));
        }
        catalog.providers.append(descriptor.value());
    }

    if (catalog.providers.isEmpty()) {
        return Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("The AI provider catalog declares no provider"), {}));
    }

    const auto merged = AiProviderCatalogHelper::mergeImportedModels(catalog.providers, modelsDocument);
    return merged.hasValue() ? Result<AiCatalog>::success(catalog) : Result<AiCatalog>::failure(merged.error());
}

LoadedAiCatalog AiProviderCatalogHelper::load() {
    const auto loaded = ProviderCatalog::loadAiCatalog(resourceContents(QStringLiteral(":/workpane/ai/assets/providers.json")), resourceContents(QStringLiteral(":/workpane/ai/assets/models.json")));

    if (!loaded.hasValue()) {
        return {{}, Result<void>::failure(loaded.error())};
    }

    return {loaded.value(), Result<void>::success()};
}

// The catalog is built once, so the plugin asks here whether the files it was built from were well formed.
const LoadedAiCatalog& AiProviderCatalogHelper::loadedCatalog() {
    static const LoadedAiCatalog loaded = AiProviderCatalogHelper::load();
    return loaded;
}

const QVector<ProviderDescriptor>& ProviderCatalog::providerCatalog() {
    return AiProviderCatalogHelper::loadedCatalog().catalog.providers;
}

const AiLimits& ProviderCatalog::aiLimits() {
    return AiProviderCatalogHelper::loadedCatalog().catalog.limits;
}

const QStringList& ProviderCatalog::promptTemplates() {
    return AiProviderCatalogHelper::loadedCatalog().catalog.promptTemplates;
}

const Result<void>& ProviderCatalog::aiCatalogError() {
    return AiProviderCatalogHelper::loadedCatalog().error;
}

const ProviderDescriptor* ProviderCatalog::findProvider(const QString& providerId) {
    for (const auto& provider : ProviderCatalog::providerCatalog()) {
        if (provider.id == providerId) {
            return &provider;
        }
    }

    return nullptr;
}

const ModelDescriptor* ProviderCatalog::findModel(const ProviderDescriptor& provider, const QString& modelId) {
    for (const auto& model : provider.models) {
        if (model.id == modelId) {
            return &model;
        }
    }

    return nullptr;
}

std::optional<double> ProviderCatalog::runCost(const QString& providerId, const QString& modelId, qint64 inputTokens, qint64 outputTokens) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(providerId);

    if (provider == nullptr || inputTokens < 0 || outputTokens < 0) {
        return std::nullopt;
    }

    const ModelDescriptor* model = ProviderCatalog::findModel(*provider, modelId);

    if (model == nullptr || !model->inputCostPerToken.has_value() || !model->outputCostPerToken.has_value()) {
        return std::nullopt;
    }

    return static_cast<double>(inputTokens) * model->inputCostPerToken.value() + static_cast<double>(outputTokens) * model->outputCostPerToken.value();
}

// A model outside the catalog keeps the trait set the provider declares for its own models instead of an inferred capability.
QSet<ModelTrait> ProviderCatalog::modelTraits(const ProviderDescriptor& provider, const QString& modelId) {
    const ModelDescriptor* model = ProviderCatalog::findModel(provider, modelId);
    return model != nullptr ? model->traits : provider.userDefinedModelTraits;
}

// The output budget a model accepts is its own, so the declared maximum bounds the parameter instead of a shared ceiling.
QVector<ParameterDescriptor> ProviderCatalog::applicableParameters(const ProviderDescriptor& provider, const QString& modelId) {
    const QSet<ModelTrait> traits = ProviderCatalog::modelTraits(provider, modelId);
    const ModelDescriptor* model = ProviderCatalog::findModel(provider, modelId);
    QVector<ParameterDescriptor> applicable;

    for (const auto& parameter : provider.parameters) {
        if (parameter.requiredTrait.has_value() && !traits.contains(parameter.requiredTrait.value())) {
            continue;
        }

        ParameterDescriptor descriptor = parameter;
        if (descriptor.boundByModelOutput && model != nullptr && model->maximumOutputTokens > 0) {
            descriptor.maximum = model->maximumOutputTokens;
            const qint64 declared = descriptor.defaultValue.toInteger();
            descriptor.defaultValue = QJsonValue(declared == 0 ? 0 : std::min<qint64>(declared, model->maximumOutputTokens));
        }
        applicable.append(descriptor);
    }

    return applicable;
}

QJsonObject ProviderCatalog::defaultParameters(const ProviderDescriptor& provider, const QString& modelId) {
    QJsonObject defaults;

    for (const auto& parameter : ProviderCatalog::applicableParameters(provider, modelId)) {
        defaults.insert(parameter.id, parameter.defaultValue);
    }

    return defaults;
}

// The answer budget is the one value the conversation fitter and the summary request have to know by name.
std::optional<ParameterDescriptor> ProviderCatalog::outputBudgetParameter(const ProviderDescriptor& provider, const QString& modelId) {
    for (const auto& parameter : ProviderCatalog::applicableParameters(provider, modelId)) {
        if (parameter.boundByModelOutput) {
            return parameter;
        }
    }

    return std::nullopt;
}

} // namespace workpane::plugins::ai
