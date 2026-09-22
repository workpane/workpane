#pragma once

#include "plugins/PluginInterface.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace workpane::plugins::ai {

enum class WireProtocol { OpenAiCompatible, Anthropic, CommandLine };

inline constexpr auto commandLinePromptPlaceholder = "{prompt}";
inline constexpr auto commandLineWorkdirPlaceholder = "{workdir}";
inline constexpr auto commandLineModelPlaceholder = "{model}";
// A service that names the voice inside its own address writes this where the voice goes.
inline constexpr auto endpointVoicePlaceholder = "{voice}";

// A command line agent runs its own tools and answers plain text, so it is invoked rather than requested.
struct CommandLineDescriptor final {
    QString program;
    QStringList arguments;
    // A credential in the environment makes these agents bill the key instead of the subscription the reader pays for, so the run starts without them.
    QStringList clearedVariables;
};

enum class ModelTrait { Sampling, Reasoning, FunctionCalling, Vision, SystemPrompt };

enum class ParameterType { Integer, Number, Enumeration };

struct ParameterOption final {
    QString id;
    QString titleKey;
};

struct ParameterDescriptor final {
    QString id;
    QString titleKey;
    // The field the provider names on the wire, written as a dotted path when the protocol nests it.
    QString field;
    ParameterType type{ParameterType::Number};
    std::optional<ModelTrait> requiredTrait;
    // The parameter that carries the answer budget is bounded by what the selected model accepts.
    bool boundByModelOutput{false};
    // A zero means the maximum the selected model declares, which is what reaches the service.
    bool modelMaximumWhenZero{false};
    double minimum{0.0};
    double maximum{0.0};
    QJsonValue defaultValue;
    QVector<ParameterOption> options;
};

struct ModelDescriptor final {
    QString id;
    QString displayName;
    QSet<ModelTrait> traits;
    int contextWindow{0};
    int maximumOutputTokens{0};
    // The price a service publishes per token, absent for a model nobody published one for.
    std::optional<double> inputCostPerToken;
    std::optional<double> outputCostPerToken;
};

// A provider answers one or more of these, and which of them it answers is data rather than code.
enum class ModelEndpoint { Chat, Image, Speech };

struct EndpointDescriptor final {
    // The voice a service names in its own path is written as this placeholder.
    QString path;
    // A voice belongs to the service that speaks, so the endpoint that speaks is what declares one.
    QStringList voices;
    QString defaultVoice;
    // A service that publishes no closed voice set answers its own catalog here.
    QString voiceCatalogPath;
    // Each service reads its credential from a header of its own, so the header and what precedes the key are declared rather than branched on.
    QString authHeader;
    QString authPrefix;
    // The body a service accepts is named field by field, so a second service is one more entry rather than one more branch.
    QString textField;
    QString voiceField;
    QString model;
    // The fields every request to this endpoint carries unchanged, such as the form the answer is asked to arrive in.
    QJsonObject body;
};

struct ProviderDescriptor final {
    QString id;
    // The models a provider opens with, resolved from the catalog file that owns what each one is.
    QStringList preferredModels;
    QString titleKey;
    int requestMaxRetries{2};
    int streamIdleTimeoutMs{60000};
    QMap<QString, QString> httpHeaders;
    QMap<QString, QString> queryParameters;
    WireProtocol protocol{WireProtocol::OpenAiCompatible};
    QString baseUrl;
    // A self-hosted service is the only one whose address the user owns.
    bool addressConfigurable{false};
    QString apiKeyVariable;
    bool requiresApiKey{true};
    CommandLineDescriptor commandLine;
    QSet<ModelTrait> userDefinedModelTraits;
    QVector<ModelDescriptor> models;
    QVector<ParameterDescriptor> parameters;
    QMap<ModelEndpoint, EndpointDescriptor> endpoints;
};

// What the agent may be tuned with lives with the catalog, while the caps that keep a payload from filling memory stay in the code that enforces them.
struct AiLimits final {
    int repeatedToolCallLimit{0};
    int summaryMaximumTokens{0};
    int toolDeadlineMs{0};
    int requestTimeoutMs{0};
    int discoveryTimeoutMs{0};
    int serverStartTimeoutMs{0};
    int scheduleWakeupMs{0};
    int maximumAgentIterations{0};
    int maximumCommandTimeoutSeconds{0};
    int maximumParallelExecutions{0};
    int maximumSamplingTokens{0};
    int maximumRequestDelayMs{0};
    int maximumRequestsPerMinute{0};
    int maximumConcurrentRequests{0};
    int retryBackoffMs{0};
    int maximumRetryBackoffMs{0};
};

// The catalog is the parsed form of the two files the plugin carries, and loading it from text is what makes it testable.
struct AiCatalog final {
    QVector<ProviderDescriptor> providers;
    AiLimits limits;
    // A template is a stable identifier that names the three translated keys carrying its name, its description and its body.
    QStringList promptTemplates;
};

class ProviderCatalog final {
  public:
    [[nodiscard]] static std::optional<ModelEndpoint> modelEndpointFromName(const QString& name);
    [[nodiscard]] static Result<AiCatalog> loadAiCatalog(const QByteArray& providersDocument, const QByteArray& modelsDocument);
    [[nodiscard]] static const QVector<ProviderDescriptor>& providerCatalog();
    [[nodiscard]] static const AiLimits& aiLimits();
    [[nodiscard]] static const QStringList& promptTemplates();
    [[nodiscard]] static const Result<void>& aiCatalogError();
    [[nodiscard]] static const ProviderDescriptor* findProvider(const QString& providerId);
    [[nodiscard]] static const ModelDescriptor* findModel(const ProviderDescriptor& provider, const QString& modelId);
    // What a run of that many tokens cost, absent when the model or its price is not declared.
    [[nodiscard]] static std::optional<double> runCost(const QString& providerId, const QString& modelId, qint64 inputTokens, qint64 outputTokens);
    [[nodiscard]] static QSet<ModelTrait> modelTraits(const ProviderDescriptor& provider, const QString& modelId);
    // The trait is named as the catalog file spells it, because that is the name the reader of a prompt already knows.
    [[nodiscard]] static QString modelTraitIdentifier(ModelTrait trait);
    [[nodiscard]] static QVector<ParameterDescriptor> applicableParameters(const ProviderDescriptor& provider, const QString& modelId);
    [[nodiscard]] static QJsonObject defaultParameters(const ProviderDescriptor& provider, const QString& modelId);
    [[nodiscard]] static std::optional<ParameterDescriptor> outputBudgetParameter(const ProviderDescriptor& provider, const QString& modelId);
};

} // namespace workpane::plugins::ai
