#include "persistence/SchemaTextHelper.h"

namespace workpane::persistence {

bool SchemaTextHelper::punctuation(char character) {
    return character == '(' || character == ')' || character == ',';
}

// Whitespace is folded and dropped beside punctuation, and quotes around a name are dropped, which leaves what the definition says.
std::string SchemaTextHelper::normalized(std::string_view sql) {
    std::string result;
    bool space = false;

    for (const char character : sql) {
        const bool blank = character == ' ' || character == '\n' || character == '\r' || character == '\t';

        if (character == '"') {
            continue;
        }

        if (blank) {
            space = !result.empty();
            continue;
        }

        if (space && !punctuation(result.back()) && !punctuation(character)) {
            result.push_back(' ');
        }

        space = false;
        result.push_back(character);
    }

    return result;
}

} // namespace workpane::persistence
