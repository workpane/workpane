#include "scripting/StorageHost.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "localization/Localization.h"
#include "persistence/ConfigurationTransfer.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/PluginDatabase.h"
#include "persistence/PreferenceStore.h"
#include "persistence/Statement.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"
#include "scripting/Identifier.h"

#include <set>
#include <utility>

namespace workpane::scripting {

Result<std::vector<persistence::Value>> StorageHost::bindings(const json::Json& values) {
    std::vector<persistence::Value> bound;

    for (const auto& value : values) {
        // The SDK writes SQL NULL as an object holding only a true null flag, because a Lua list has no place for nil.
        if (value.is_object() && value.size() == 1 && value.contains("null") && value["null"] == true) {
            bound.emplace_back(nullptr);
            continue;
        }

        // Lua writes a number that is not finite as a plain null, which no binding of the SDK ever is, so it is refused instead of stored as NULL.
        if (value.is_null()) {
            return Result<std::vector<persistence::Value>>::failure({"database_binding_invalid", "A database value is a number that is not finite", ""});
        }

        if (!value.is_boolean() && !value.is_number() && !value.is_string()) {
            return Result<std::vector<persistence::Value>>::failure({"database_binding_type", "A database value is neither null, a boolean, a number nor a text", value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)});
        }

        bound.push_back(value);
    }

    return Result<std::vector<persistence::Value>>::success(std::move(bound));
}

nlohmann::json StorageHost::execution(const persistence::Execution& execution) {
    return {{"changes", execution.changes}, {"lastInsertRowId", execution.lastInsertRowId}};
}

std::filesystem::path StorageHost::path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

StorageHost::StorageHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies) {}

Result<void> StorageHost::registerFunctions() {
    // clang-format off
    const std::vector<std::pair<std::string, ScriptRuntime::HostFunction>> functions{
        {"workpane_preferences_read", [this](const nlohmann::json& argument) { return readPreferences(argument); }},
        {"workpane_preferences_write", [this](const nlohmann::json& argument) { return writePreferences(argument); }},
        {"workpane_plugin_data", [this](const nlohmann::json& argument) { return pluginData(argument); }},
        {"workpane_plugin_data_erase", [this](const nlohmann::json& argument) { return erasePluginData(argument); }},
        {"workpane_database_migrate", [this](const nlohmann::json& argument) { return migrate(argument); }},
        {"workpane_database_query", [this](const nlohmann::json& argument) { return query(argument); }},
        {"workpane_database_run", [this](const nlohmann::json& argument) { return run(argument); }},
        {"workpane_database_transaction", [this](const nlohmann::json& argument) { return transaction(argument); }},
        {"workpane_configuration_export", [this](const nlohmann::json& argument) { return exportConfiguration(argument); }},
        {"workpane_configuration_import", [this](const nlohmann::json& argument) { return importConfiguration(argument); }},
    };
    // clang-format on

    for (const auto& [name, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, ScriptRuntime::Effect::Background, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

Result<std::string> StorageHost::owner(const std::string& plugin, const std::string& document) {
    if (document.empty()) {
        return Result<std::string>::success(plugin);
    }

    if (!Identifier::valid(document)) {
        return Result<std::string>::failure({"preferences_document_invalid", "A preference document is named with lowercase letters, numbers and single hyphens", document});
    }

    return Result<std::string>::success(plugin + ":" + document);
}

nlohmann::json StorageHost::readPreferences(const nlohmann::json& argument) const {
    std::string plugin;
    std::string document;
    json::ObjectReader reader(argument, "preferences.read");
    reader.readText("plugin", plugin).readText("document", document, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto kept = owner(plugin, document);

    if (!kept.hasValue()) {
        return HostReply::failure(kept.error());
    }

    return HostReply::success(m_services.preferences.document(kept.value()));
}

nlohmann::json StorageHost::writePreferences(const nlohmann::json& argument) {
    std::string plugin;
    std::string document;
    std::int64_t request = 0;
    const json::Json* values = nullptr;
    json::ObjectReader reader(argument, "preferences.write");
    reader.readText("plugin", plugin).readText("document", document, json::Presence::Optional).readInteger("request", request, 1, largestRequest).readObject("values", values);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto kept = owner(plugin, document);

    if (!kept.hasValue()) {
        return HostReply::failure(kept.error());
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.preferences.write(kept.value(), *values, [this, alive, request](Result<void> result) {
        if (alive.expired()) {
            return;
        }

        m_replies.reply(request, result.hasValue() ? Result<nlohmann::json>::success(nullptr) : Result<nlohmann::json>::failure(result.error()));
    });
    // clang-format on

    return HostReply::success();
}

// Answers every plugin that keeps tables or preferences, installed or not, so the core offers to erase what a removed plugin left behind.
nlohmann::json StorageHost::pluginData(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    json::ObjectReader reader(argument, "plugin.data");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (plugin != localization::Localization::coreOwner) {
        return HostReply::failure({"plugin_data_owner_invalid", "Only the core reads and erases the data of plugins", plugin});
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    std::function<Result<std::vector<std::string>>(persistence::Database&)> work = [](persistence::Database& database) { return persistence::PluginDatabase::plugins(database); };
    std::function<void(Result<std::vector<std::string>>)> done = [this, alive, request](Result<std::vector<std::string>> versioned) {
        if (alive.expired()) {
            return;
        }

        if (!versioned.hasValue()) {
            m_replies.reply(request, Result<nlohmann::json>::failure(versioned.error()));
            return;
        }

        std::set<std::string> owners(versioned.value().begin(), versioned.value().end());

        for (const auto& owner : m_services.preferences.plugins()) {
            if (owner != localization::Localization::coreOwner) {
                owners.insert(owner);
            }
        }

        m_replies.reply(request, Result<nlohmann::json>::success({{"plugins", owners}}));
    };
    // clang-format on

    m_services.database.submit<std::vector<std::string>>(std::move(work), std::move(done));

    return HostReply::success();
}

// Erases the tables, the schema version and the preferences of a plugin that does not run, all together or not at all.
nlohmann::json StorageHost::erasePluginData(const nlohmann::json& argument) {
    std::string plugin;
    std::string target;
    std::int64_t request = 0;
    json::ObjectReader reader(argument, "plugin.data.erase");
    reader.readText("plugin", plugin).readText("target", target).readInteger("request", request, 1, largestRequest);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (plugin != localization::Localization::coreOwner) {
        return HostReply::failure({"plugin_data_owner_invalid", "Only the core reads and erases the data of plugins", plugin});
    }

    if (!Identifier::valid(target) || target == localization::Localization::coreOwner) {
        return HostReply::failure({"plugin_identifier_invalid", "The data of a plugin is named by the identifier of the plugin", target});
    }

    if (m_services.plugins.find(target) != nullptr) {
        return HostReply::failure({"plugin_running", "The data of a running plugin cannot be erased", target});
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    std::function<Result<void>(persistence::Database&)> work = [target](persistence::Database& database) { return persistence::PluginDatabase::erase(database, target); };
    std::function<void(Result<void>)> done = [this, alive, request, target](Result<void> erased) {
        if (alive.expired()) {
            return;
        }

        if (erased.hasValue()) {
            m_services.preferences.discard(target);
        }

        m_replies.reply(request, erased.hasValue() ? Result<nlohmann::json>::success(nullptr) : Result<nlohmann::json>::failure(erased.error()));
    };
    // clang-format on

    m_services.database.submit<void>(std::move(work), std::move(done));

    return HostReply::success();
}

nlohmann::json StorageHost::migrate(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    const json::Json* migrations = nullptr;
    json::ObjectReader reader(argument, "database.migrate");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readArray("migrations", migrations);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (m_services.plugins.find(plugin) == nullptr) {
        return HostReply::failure({"plugin_unknown", "Only a registered plugin owns scoped tables", plugin});
    }

    std::vector<std::vector<std::string>> declared;

    for (const auto& migration : *migrations) {
        if (!migration.is_array()) {
            return HostReply::failure({"database_migration_invalid", "A migration is a list of statements", plugin});
        }

        std::vector<std::string> statements;

        for (const auto& statement : migration) {
            if (!statement.is_string()) {
                return HostReply::failure({"database_migration_invalid", "A migration statement is a text", plugin});
            }

            statements.push_back(statement.get<std::string>());
        }

        declared.push_back(std::move(statements));
    }

    // clang-format off
    submit(request, [plugin, declared = std::move(declared)](persistence::Database& database) {
        auto version = persistence::PluginDatabase::migrate(database, plugin, declared);
        return version.hasValue() ? Result<nlohmann::json>::success({{"version", version.value()}}) : Result<nlohmann::json>::failure(version.error());
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json StorageHost::query(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string sql;
    const json::Json* values = &json::ObjectReader::emptyList();
    json::ObjectReader reader(argument, "database.query");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("sql", sql).readArray("bindings", values, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (m_services.plugins.find(plugin) == nullptr) {
        return HostReply::failure({"plugin_unknown", "Only a registered plugin owns scoped tables", plugin});
    }

    auto bindings = StorageHost::bindings(*values);

    if (!bindings.hasValue()) {
        return HostReply::failure(bindings.error());
    }

    // clang-format off
    submit(request, [plugin, sql, bindings = std::move(bindings.value())](persistence::Database& database) {
        auto rows = persistence::PluginDatabase::query(database, plugin, sql, bindings);
        return rows.hasValue() ? Result<nlohmann::json>::success(rows.value()) : Result<nlohmann::json>::failure(rows.error());
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json StorageHost::run(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string sql;
    const json::Json* values = &json::ObjectReader::emptyList();
    json::ObjectReader reader(argument, "database.run");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("sql", sql).readArray("bindings", values, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (m_services.plugins.find(plugin) == nullptr) {
        return HostReply::failure({"plugin_unknown", "Only a registered plugin owns scoped tables", plugin});
    }

    auto bindings = StorageHost::bindings(*values);

    if (!bindings.hasValue()) {
        return HostReply::failure(bindings.error());
    }

    // clang-format off
    submit(request, [plugin, sql, bindings = std::move(bindings.value())](persistence::Database& database) {
        auto executed = persistence::PluginDatabase::run(database, plugin, sql, bindings);
        return executed.hasValue() ? Result<nlohmann::json>::success(execution(executed.value())) : Result<nlohmann::json>::failure(executed.error());
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json StorageHost::transaction(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    const json::Json* entries = nullptr;
    json::ObjectReader reader(argument, "database.transaction");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readArray("statements", entries);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (m_services.plugins.find(plugin) == nullptr) {
        return HostReply::failure({"plugin_unknown", "Only a registered plugin owns scoped tables", plugin});
    }

    std::vector<persistence::Statement> statements;

    for (const auto& entry : *entries) {
        persistence::Statement statement;
        const json::Json* values = &json::ObjectReader::emptyList();
        json::ObjectReader statementReader(entry, "database.transaction.statements");
        statementReader.readText("sql", statement.sql).readArray("bindings", values, json::Presence::Optional);

        if (const auto finished = statementReader.finish(); !finished.hasValue()) {
            return HostReply::failure(finished.error());
        }

        auto bindings = StorageHost::bindings(*values);

        if (!bindings.hasValue()) {
            return HostReply::failure(bindings.error());
        }

        statement.bindings = std::move(bindings.value());
        statements.push_back(std::move(statement));
    }

    // clang-format off
    submit(request, [plugin, statements = std::move(statements)](persistence::Database& database) {
        auto executed = persistence::PluginDatabase::transaction(database, plugin, statements);

        if (!executed.hasValue()) {
            return Result<nlohmann::json>::failure(executed.error());
        }

        nlohmann::json results = nlohmann::json::array();

        for (const auto& execution : executed.value()) {
            results.push_back(StorageHost::execution(execution));
        }

        return Result<nlohmann::json>::success(std::move(results));
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json StorageHost::exportConfiguration(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    json::ObjectReader reader(argument, "configuration.export");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    // clang-format off
    const auto work = [](persistence::Database& database, const std::filesystem::path& destination) { return persistence::ConfigurationTransfer::exportTo(database, destination); };
    // clang-format on
    return transfer(plugin, request, path, work);
}

// An import names the version every discovered plugin declares, so a schema newer than the plugin installed here is refused before it is staged.
nlohmann::json StorageHost::importConfiguration(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    const json::Json* schemas = nullptr;
    json::ObjectReader reader(argument, "configuration.import");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path).readObject("schemas", schemas);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    auto installed = schemaVersions(*schemas);

    if (!installed.hasValue()) {
        return HostReply::failure(installed.error());
    }

    // clang-format off
    const auto work = [data = m_services.info.data, installed = std::move(installed.value())](persistence::Database&, const std::filesystem::path& source) { return persistence::ConfigurationTransfer::stage(source, data, installed); };
    // clang-format on
    return transfer(plugin, request, path, work);
}

Result<persistence::ConfigurationTransfer::SchemaVersions> StorageHost::schemaVersions(const json::Json& schemas) {
    persistence::ConfigurationTransfer::SchemaVersions versions;

    for (const auto& [plugin, version] : schemas.items()) {
        if (!Identifier::valid(plugin) || !version.is_number_integer() || version.get<std::int64_t>() < 0) {
            return Result<persistence::ConfigurationTransfer::SchemaVersions>::failure({"configuration_schemas_invalid", "The schema versions of an import name each plugin with a count of migrations", plugin});
        }

        versions.emplace(plugin, version.get<std::int64_t>());
    }

    return Result<persistence::ConfigurationTransfer::SchemaVersions>::success(std::move(versions));
}

// Only the core moves the whole configuration, because it belongs to every plugin at once, and transfers run one after the other on the database thread.
nlohmann::json StorageHost::transfer(const std::string& plugin, std::int64_t request, const std::string& path, std::function<Result<void>(persistence::Database&, const std::filesystem::path&)> work) {
    if (plugin != localization::Localization::coreOwner) {
        return HostReply::failure({"configuration_owner_invalid", "Only the core moves the whole configuration", plugin});
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    std::function<void(Result<nlohmann::json>)> done = [this, alive, request](Result<nlohmann::json> result) {
        if (alive.expired()) {
            return;
        }

        m_replies.reply(request, result);
    };

    auto job = [work = std::move(work), file = StorageHost::path(path)](persistence::Database& database) {
        const auto moved = work(database, file);
        return moved.hasValue() ? Result<nlohmann::json>::success(nullptr) : Result<nlohmann::json>::failure(moved.error());
    };
    // clang-format on

    m_services.database.submit<nlohmann::json>(std::move(job), std::move(done));

    return HostReply::success();
}

void StorageHost::submit(std::int64_t request, std::function<Result<nlohmann::json>(persistence::Database&)> work) {
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    std::function<void(Result<nlohmann::json>)> done = [this, alive, request](Result<nlohmann::json> result) {
        if (alive.expired()) {
            return;
        }

        m_replies.reply(request, result);
    };
    // clang-format on

    m_services.database.submit<nlohmann::json>(std::move(work), std::move(done));
}

} // namespace workpane::scripting
