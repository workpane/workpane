#pragma once

#include <string>

namespace workpane::localization {

// A language is declared together with the key that names it and the separators its numbers are written with.
struct Language final {
    std::string id;
    std::string titleKey;
    char decimalSeparator{'.'};
    char groupSeparator{','};
};

} // namespace workpane::localization
