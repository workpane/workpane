#pragma once

#include "plugins/PluginInterface.h"

namespace workpane::plugins::logs::translations {

class LogsCatalog final {
  public:
    static TranslationEntries english() {
        return {{QStringLiteral("logs.plugin.title"), QStringLiteral("Logs")}, {QStringLiteral("logs.error.clear-message"), QStringLiteral("The log entries could not be cleared")}, {QStringLiteral("logs.error.read-message"), QStringLiteral("The log entries could not be read")}, {QStringLiteral("logs.navigation.viewer"), QStringLiteral("Logs")}, {QStringLiteral("logs.settings.general"), QStringLiteral("General")}, {QStringLiteral("logs.settings.storage"), QStringLiteral("Log storage")}, {QStringLiteral("logs.viewer.title"), QStringLiteral("Application Logs")}, {QStringLiteral("logs.viewer.search"), QStringLiteral("Search logs")}, {QStringLiteral("logs.viewer.all-levels"), QStringLiteral("All levels")}, {QStringLiteral("logs.viewer.refresh"), QStringLiteral("Refresh")}, {QStringLiteral("logs.viewer.load-older"), QStringLiteral("Load Older")}, {QStringLiteral("logs.viewer.clear"), QStringLiteral("Clear Logs")}, {QStringLiteral("logs.viewer.time"), QStringLiteral("Date and time")}, {QStringLiteral("logs.viewer.level"), QStringLiteral("Level")}, {QStringLiteral("logs.viewer.source"), QStringLiteral("Source")}, {QStringLiteral("logs.viewer.category"), QStringLiteral("Category")}, {QStringLiteral("logs.viewer.message"), QStringLiteral("Message")}, {QStringLiteral("logs.viewer.empty"), QStringLiteral("No logs are available")}, {QStringLiteral("logs.viewer.clear-title"), QStringLiteral("Clear Application Logs")}, {QStringLiteral("logs.viewer.clear-message"), QStringLiteral("Clear every stored application log?")}, {QStringLiteral("logs.viewer.clear-detail"), QStringLiteral("Deleted logs cannot be recovered")}, {QStringLiteral("logs.viewer.clear-action"), QStringLiteral("Clear Logs")}};
    }

    static TranslationEntries portuguese() {
        return {{QStringLiteral("logs.plugin.title"), QStringLiteral("Logs")}, {QStringLiteral("logs.error.clear-message"), QStringLiteral("Não foi possível limpar as entradas de log")}, {QStringLiteral("logs.error.read-message"), QStringLiteral("Não foi possível ler as entradas de log")}, {QStringLiteral("logs.navigation.viewer"), QStringLiteral("Logs")}, {QStringLiteral("logs.settings.general"), QStringLiteral("Geral")}, {QStringLiteral("logs.settings.storage"), QStringLiteral("Armazenamento de logs")}, {QStringLiteral("logs.viewer.title"), QStringLiteral("Logs do Aplicativo")}, {QStringLiteral("logs.viewer.search"), QStringLiteral("Buscar logs")}, {QStringLiteral("logs.viewer.all-levels"), QStringLiteral("Todos os níveis")}, {QStringLiteral("logs.viewer.refresh"), QStringLiteral("Atualizar")}, {QStringLiteral("logs.viewer.load-older"), QStringLiteral("Carregar Anteriores")}, {QStringLiteral("logs.viewer.clear"), QStringLiteral("Limpar Logs")}, {QStringLiteral("logs.viewer.time"), QStringLiteral("Data e hora")}, {QStringLiteral("logs.viewer.level"), QStringLiteral("Nível")}, {QStringLiteral("logs.viewer.source"), QStringLiteral("Origem")}, {QStringLiteral("logs.viewer.category"), QStringLiteral("Categoria")}, {QStringLiteral("logs.viewer.message"), QStringLiteral("Mensagem")}, {QStringLiteral("logs.viewer.empty"), QStringLiteral("Nenhum log disponível")}, {QStringLiteral("logs.viewer.clear-title"), QStringLiteral("Limpar Logs do Aplicativo")}, {QStringLiteral("logs.viewer.clear-message"), QStringLiteral("Limpar todos os logs armazenados do aplicativo?")}, {QStringLiteral("logs.viewer.clear-detail"), QStringLiteral("Os logs removidos não poderão ser recuperados")}, {QStringLiteral("logs.viewer.clear-action"), QStringLiteral("Limpar Logs")}};
    }

    static TranslationCatalog catalog() {
        return {{QStringLiteral("en"), english()}, {QStringLiteral("pt"), portuguese()}};
    }
};

} // namespace workpane::plugins::logs::translations
