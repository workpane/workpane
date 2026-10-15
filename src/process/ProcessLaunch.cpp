#include "process/ProcessLaunch.h"

#include "text/Utf8Helper.h"

namespace workpane::process {

bool ProcessLaunch::text(std::string_view value, std::size_t maximum) {
    return value.size() <= maximum && value.find('\0') == std::string_view::npos && text::Utf8Helper::valid(value);
}

bool ProcessLaunch::variable(std::string_view value) {
    return !value.empty() && text(value, maximumText) && value.find('=') == std::string_view::npos;
}

Result<void> ProcessLaunch::validate() const {
    const Error invalid{"process_launch_invalid", "The program launch contains an invalid or oversized path, argument, environment or input", ""};

    if (!program.is_absolute() || !directory.is_absolute() || program.native().size() > maximumPath || directory.native().size() > maximumPath || program.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || directory.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || arguments.size() > maximumItems || variables.size() > maximumItems || cleared.size() > maximumItems || (input.has_value() && input->size() > maximumInputBytes)) {
        return Result<void>::failure(invalid);
    }

    for (const auto& argument : arguments) {
        if (!text(argument, maximumText)) {
            return Result<void>::failure(invalid);
        }
    }

    for (const auto& [name, value] : variables) {
        if (!variable(name) || !text(value, maximumText)) {
            return Result<void>::failure(invalid);
        }
    }

    for (const auto& name : cleared) {
        if (!variable(name)) {
            return Result<void>::failure(invalid);
        }
    }

    return Result<void>::success();
}

} // namespace workpane::process
