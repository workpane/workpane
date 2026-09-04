#include "AiModelConnection.h"

#include <QSet>
#include <QUrl>

#include <algorithm>
#include <limits>

namespace workpane::plugins::ai {

class AiModelConnectionHelper final {
  public:
    static bool integerValue(const QJsonValue& value, qint64& output);
    static Result<void> validateParameter(const ParameterDescriptor& descriptor, const QJsonValue& value);
};

bool AiModelConnectionHelper::integerValue(const QJsonValue& value, qint64& output) {
    if (!value.isDouble()) {
        return false;
    }

    output = value.toInteger(std::numeric_limits<qint64>::min());
    return output != std::numeric_limits<qint64>::min() && value.toDouble() == static_cast<double>(output);
}

Result<void> AiModelConnectionHelper::validateParameter(const ParameterDescriptor& descriptor, const QJsonValue& value) {
    const Error invalid{"ai_parameter_invalid", "The provider parameter value is invalid", descriptor.id};

    if (descriptor.type == ParameterType::Enumeration) {
        if (!value.isString()) {
            return Result<void>::failure(invalid);
        }
        for (const auto& option : descriptor.options) {
            if (option.id == value.toString()) {
                return Result<void>::success();
            }
        }
        return Result<void>::failure(invalid);
    }

    if (descriptor.type == ParameterType::Integer) {
        qint64 parsed = 0;
        if (!integerValue(value, parsed) || static_cast<double>(parsed) < descriptor.minimum || static_cast<double>(parsed) > descriptor.maximum) {
            return Result<void>::failure(invalid);
        }
        return Result<void>::success();
    }

    if (!value.isDouble() || value.toDouble() < descriptor.minimum || value.toDouble() > descriptor.maximum) {
        return Result<void>::failure(invalid);
    }

    return Result<void>::success();
}

QString ModelConnections::connectionKey(const QString& providerId, const QString& modelId) {
    return providerId + QLatin1Char('/') + modelId;
}

QString ModelConnections::connectionKey(const ModelConnection& connection) {
    return connectionKey(connection.providerId, connection.modelId);
}

// The display name is what the user reads, and the key is what they see when they named nothing.
QString ModelConnections::connectionLabel(const ModelConnection& connection) {
    return connection.displayName.trimmed().isEmpty() ? ModelConnections::connectionKey(connection) : connection.displayName.trimmed();
}

// Only a self-hosted service carries its own address, so every other connection answers with the address its provider publishes.
std::optional<ResolvedEndpoint> ModelConnections::resolveEndpoint(const QString& providerId, const QString& address, ModelEndpoint endpoint) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(providerId);

    if (provider == nullptr || !provider->endpoints.contains(endpoint)) {
        return std::nullopt;
    }

    // The caller already decided which address this connection speaks to, so this only says where on it the modality answers.
    const QString base = address.isEmpty() ? provider->baseUrl : address;

    if (base.isEmpty()) {
        return std::nullopt;
    }

    return ResolvedEndpoint{base + provider->endpoints.value(endpoint).path, provider->apiKeyVariable, provider->httpHeaders};
}

// A command line agent answers a conversation by being invoked rather than at an address, so it declares no endpoint and still answers this one.
QString ModelConnections::defaultProviderId(ModelEndpoint endpoint) {
    const auto answering = providersAnswering(endpoint);
    return answering.isEmpty() ? QString{} : answering.first()->id;
}

QVector<const ProviderDescriptor*> ModelConnections::providersAnswering(ModelEndpoint endpoint) {
    QVector<const ProviderDescriptor*> answering;

    for (const auto& provider : ProviderCatalog::providerCatalog()) {
        const bool invoked = endpoint == ModelEndpoint::Chat && provider.protocol == WireProtocol::CommandLine;

        if (invoked || provider.endpoints.contains(endpoint)) {
            answering.append(&provider);
        }
    }

    return answering;
}

QString ModelConnections::connectionAddress(const ModelConnection& connection) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);

    if (provider == nullptr) {
        return {};
    }

    return provider->addressConfigurable && !connection.address.isEmpty() ? connection.address : provider->baseUrl;
}

const ModelConnection* ModelConnections::findConnection(const QVector<ModelConnection>& connections, const QString& key) {
    for (const auto& connection : connections) {
        if (ModelConnections::connectionKey(connection) == key) {
            return &connection;
        }
    }

    return nullptr;
}

Result<QJsonObject> ModelConnections::validateParameters(const ProviderDescriptor& provider, const QString& modelId, const QJsonObject& parameters) {
    const QVector<ParameterDescriptor> applicable = ProviderCatalog::applicableParameters(provider, modelId);

    QJsonObject validated;

    for (const auto& descriptor : applicable) {
        if (!parameters.contains(descriptor.id)) {
            return Result<QJsonObject>::failure({"ai_parameter_missing", "The provider parameter is missing", descriptor.id});
        }
        const auto result = AiModelConnectionHelper::validateParameter(descriptor, parameters.value(descriptor.id));
        if (!result.hasValue()) {
            return Result<QJsonObject>::failure(result.error());
        }
        validated.insert(descriptor.id, parameters.value(descriptor.id));
    }

    for (auto entry = parameters.constBegin(); entry != parameters.constEnd(); ++entry) {
        if (!validated.contains(entry.key())) {
            return Result<QJsonObject>::failure({"ai_parameter_unknown", "The provider parameter is not declared for this model", entry.key()});
        }
    }

    return Result<QJsonObject>::success(validated);
}

// An extra parameter is whatever the service accepts and this project does not know, so only its shape is checked.
Result<QJsonObject> ModelConnections::validateExtraParameters(const QJsonObject& parameters) {
    for (auto entry = parameters.constBegin(); entry != parameters.constEnd(); ++entry) {
        if (entry.key().trimmed().isEmpty() || entry.value().isUndefined()) {
            return Result<QJsonObject>::failure({"ai_extra_parameter_invalid", "The extra provider parameter is invalid", entry.key()});
        }
    }

    return Result<QJsonObject>::success(parameters);
}

// Every provider exists with the configuration its own descriptor declares, and storage only holds what the user changed.
ModelConnection ModelConnections::declaredConnection(const ProviderDescriptor& provider, const QString& modelId) {
    const QString model = modelId.isEmpty() && !provider.models.isEmpty() ? provider.models.first().id : modelId;
    ModelConnection connection;
    connection.providerId = provider.id;
    connection.modelId = model;
    connection.apiKey = Secrets::defaultSecretReference(provider.apiKeyVariable);
    connection.address = provider.addressConfigurable ? provider.baseUrl : QString{};
    connection.parameters = ProviderCatalog::defaultParameters(provider, model);
    return connection;
}

// A run speaks the protocol of the connection it declares, which is not necessarily the one the default connection speaks.
WireProtocol ModelConnections::connectionProtocol(const ModelConnection& connection) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);
    return provider == nullptr ? WireProtocol::OpenAiCompatible : provider->protocol;
}

// A zero budget lets the service answer with everything the model allows, so what it really is worth is that model own maximum.
qint64 ModelConnections::outputBudget(const ModelConnection& connection) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);

    if (provider == nullptr) {
        return 0;
    }

    const auto budget = ProviderCatalog::outputBudgetParameter(*provider, connection.modelId);
    const qint64 declared = budget.has_value() ? connection.parameters.value(budget->id).toInteger(0) : 0;

    if (declared > 0) {
        return declared;
    }

    const ModelDescriptor* model = ProviderCatalog::findModel(*provider, connection.modelId);
    return model == nullptr ? 0 : model->maximumOutputTokens;
}

// A borrowed budget still has to fit what the model accepts, so it is clamped instead of making the connection invalid.
void ModelConnections::setOutputBudget(ModelConnection& connection, qint64 tokens) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);

    if (provider == nullptr) {
        return;
    }

    const auto budget = ProviderCatalog::outputBudgetParameter(*provider, connection.modelId);

    if (!budget.has_value()) {
        return;
    }

    connection.parameters.insert(budget->id, std::clamp<qint64>(tokens, static_cast<qint64>(budget->minimum), static_cast<qint64>(budget->maximum)));
}

Result<ModelConnection> ModelConnections::validateConnection(const ModelConnection& connection) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);

    if (provider == nullptr) {
        return Result<ModelConnection>::failure({"ai_provider_unknown", "The selected AI provider is not supported", connection.providerId});
    }

    ModelConnection validated = connection;
    validated.modelId = connection.modelId.trimmed();
    validated.displayName = connection.displayName.trimmed();
    validated.address = connection.address.trimmed();

    if (validated.modelId.isEmpty()) {
        return Result<ModelConnection>::failure({"ai_model_invalid", "The AI model is required", provider->id});
    }
    if (provider->requiresApiKey && validated.apiKey.isEmpty()) {
        return Result<ModelConnection>::failure({"ai_api_key_missing", "The provider requires an API key", provider->id});
    }
    if (!provider->addressConfigurable && !validated.address.isEmpty()) {
        return Result<ModelConnection>::failure({"ai_address_not_configurable", "The provider publishes its own address", provider->id});
    }

    const QUrl address(validated.address);

    if (provider->addressConfigurable && (validated.address.isEmpty() || !address.isValid() || (address.scheme() != QStringLiteral("http") && address.scheme() != QStringLiteral("https")))) {
        return Result<ModelConnection>::failure({"ai_address_invalid", "The service address is invalid", provider->id});
    }

    const auto parameters = ModelConnections::validateParameters(*provider, validated.modelId, validated.parameters);

    if (!parameters.hasValue()) {
        return Result<ModelConnection>::failure(parameters.error());
    }
    // Asking for the maximum of a model the catalog does not declare has no answer, so it is refused where it is typed.
    const auto budget = ProviderCatalog::outputBudgetParameter(*provider, validated.modelId);
    const ModelDescriptor* chosen = ProviderCatalog::findModel(*provider, validated.modelId);
    const bool asksForTheMaximum = budget.has_value() && budget->modelMaximumWhenZero && validated.parameters.value(budget->id).toInteger(0) == 0;

    if (asksForTheMaximum && chosen == nullptr) {
        return Result<ModelConnection>::failure({"ai_output_budget_unknown", "The catalog does not declare this model, so its answer budget has to be a number", validated.modelId});
    }
    // A service may publish the same number for what a model reads and for what it may answer, and asking for all of it leaves the conversation none.
    if (asksForTheMaximum && chosen != nullptr && chosen->maximumOutputTokens >= chosen->contextWindow) {
        return Result<ModelConnection>::failure({"ai_output_budget_whole_window", "The maximum this model declares is its whole window, so its answer budget has to be a number", validated.modelId});
    }

    const auto extra = ModelConnections::validateExtraParameters(validated.extraParameters);

    if (!extra.hasValue()) {
        return Result<ModelConnection>::failure(extra.error());
    }

    validated.parameters = parameters.value();
    validated.extraParameters = extra.value();
    return Result<ModelConnection>::success(validated);
}

// One key names one configuration, so the same provider and model pair is configured once.
Result<void> ModelConnections::validateConnectionSet(const QVector<ModelConnection>& connections) {
    QSet<QString> keys;

    for (const auto& connection : connections) {
        const auto validated = ModelConnections::validateConnection(connection);
        if (!validated.hasValue()) {
            return Result<void>::failure(validated.error());
        }
        const QString key = ModelConnections::connectionKey(validated.value());
        if (keys.contains(key)) {
            return Result<void>::failure({"ai_connection_duplicate", "The provider and model pair is already configured", key});
        }
        keys.insert(key);
    }

    return Result<void>::success();
}

} // namespace workpane::plugins::ai
