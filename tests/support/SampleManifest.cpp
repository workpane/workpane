#include "support/SampleManifest.h"

#include "scripting/PluginRegistry.h"

namespace workpane::tests {

nlohmann::json SampleManifest::create(const std::string& id, const std::filesystem::path& plugins) {
    return {
        {"id", id}, {"titleKey", id + ".plugin.title"}, {"directory", (plugins / id).string()}, {"dependencies", nlohmann::json::array()}, {"navigation", nlohmann::json::array({{{"id", "main"}, {"titleKey", id + ".navigation.main"}, {"icon", "home"}, {"placement", "primary"}, {"order", 10}}})}, {"settings", nlohmann::json::array()},
    };
}

// Installs the catalog of the sample the way the SDK installs the catalog of a plugin it discovered.
Result<void> SampleManifest::install(localization::Localization& localization, const std::string& id) {
    const nlohmann::json translations{{"en", {{id + ".plugin.title", "Sample"}, {id + ".navigation.main", "Main"}, {id + ".band.strip", "Strip"}}}, {"pt", {{id + ".plugin.title", "Amostra"}, {id + ".navigation.main", "Principal"}, {id + ".band.strip", "Faixa"}}}};
    const auto catalog = scripting::PluginRegistry::parseCatalog(translations);

    if (!catalog.hasValue()) {
        return Result<void>::failure(catalog.error());
    }

    return localization.registerCatalog(id, catalog.value());
}

} // namespace workpane::tests
