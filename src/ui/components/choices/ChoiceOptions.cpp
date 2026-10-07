#include "ui/components/choices/ChoiceOptions.h"

#include "ui/model/TextValue.h"

#include <functional>
#include <set>
#include <string>
#include <utility>

namespace workpane::ui {

Result<std::vector<ChoiceOption>> ChoiceOptions::parse(const json::Json& options, std::string_view context) {
    std::vector<ChoiceOption> parsed;
    std::set<std::string, std::less<>> seen;

    for (const auto& entry : options) {
        ChoiceOption option;
        const json::Json* text = &json::ObjectReader::absent();
        json::ObjectReader reader(entry, std::string(context) + ".options");
        reader.readText("value", option.value).readAny("text", text);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<ChoiceOption>>::failure(finished.error());
        }

        auto label = TextValue::parse(*text, std::string(context) + ".options.text");

        if (!label.hasValue()) {
            return Result<std::vector<ChoiceOption>>::failure(label.error());
        }

        if (!seen.insert(option.value).second) {
            return Result<std::vector<ChoiceOption>>::failure({"ui_option_duplicate", "Two options share one value", option.value});
        }

        option.text = std::move(label.value());
        parsed.push_back(std::move(option));
    }

    return Result<std::vector<ChoiceOption>>::success(std::move(parsed));
}

} // namespace workpane::ui
