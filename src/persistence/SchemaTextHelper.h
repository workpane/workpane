#pragma once

#include <string>
#include <string_view>

namespace workpane::persistence {

// The text SQLite keeps for a definition, compared by what it says rather than how it was spelled, since SQLite keeps the spacing it was given and quotes a name it renamed.
class SchemaTextHelper final {
  public:
    [[nodiscard]] static std::string normalized(std::string_view sql);

  private:
    [[nodiscard]] static bool punctuation(char character);
};

} // namespace workpane::persistence
