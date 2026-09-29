#include "process/ProcessEnvironment.h"

#include <algorithm>
#include <cctype>

namespace workpane::process {

// Windows spells one variable in any case, so a variable is replaced or cleared there whatever its case.
std::vector<std::string> ProcessEnvironment::build(const std::vector<std::string>& inherited, const ProcessLaunch& launch, bool caseInsensitive) {
    std::vector<std::string> environment;

    for (const auto& entry : inherited) {
        const std::string_view name = std::string_view(entry).substr(0, entry.find('='));
        // clang-format off
        const auto matches = [&](std::string_view other) { return sameName(name, other, caseInsensitive); };
        const bool cleared = std::ranges::any_of(launch.cleared, matches);
        const bool replaced = std::ranges::any_of(launch.variables, [&](const auto& variable) { return matches(variable.first); });
        // clang-format on

        if (!cleared && !replaced) {
            environment.push_back(entry);
        }
    }

    for (const auto& [name, value] : launch.variables) {
        environment.push_back(name + "=" + value);
    }

    return environment;
}

bool ProcessEnvironment::sameName(std::string_view first, std::string_view second, bool caseInsensitive) {
    if (!caseInsensitive) {
        return first == second;
    }

    // clang-format off
    return first.size() == second.size() && std::ranges::equal(first, second, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); });
    // clang-format on
}

} // namespace workpane::process
