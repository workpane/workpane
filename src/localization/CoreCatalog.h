#pragma once

#include "localization/Localization.h"

namespace workpane::localization {

// The texts of the core in every language the product speaks, registered before any plugin so the shell and every component can be read from the first frame.
class CoreCatalog final {
  public:
    [[nodiscard]] static TranslationCatalog catalog();

  private:
    [[nodiscard]] static TranslationEntries english();
    [[nodiscard]] static TranslationEntries portuguese();
};

} // namespace workpane::localization
