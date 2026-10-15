#include "ui/components/views/CodeLanguage.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace workpane::ui {

Result<const TextEditor::Language*> CodeLanguage::named(std::string_view name) {
    if (name == "none") {
        return Result<const TextEditor::Language*>::success(nullptr);
    }

    // clang-format off
    const std::array<std::pair<std::string_view, const TextEditor::Language* (*)()>, 11> languages{{{"c", &TextEditor::Language::C}, {"cpp", &TextEditor::Language::Cpp}, {"cs", &TextEditor::Language::Cs}, {"angelscript", &TextEditor::Language::AngelScript}, {"lua", &TextEditor::Language::Lua}, {"python", &TextEditor::Language::Python}, {"glsl", &TextEditor::Language::Glsl}, {"hlsl", &TextEditor::Language::Hlsl}, {"json", &TextEditor::Language::Json}, {"markdown", &TextEditor::Language::Markdown}, {"sql", &TextEditor::Language::Sql}}};
    // clang-format on

    for (const auto& [known, factory] : languages) {
        if (known == name) {
            return Result<const TextEditor::Language*>::success(factory());
        }
    }

    static const std::array scripts{
        script("javascript", "//", "async await break case catch class const continue debugger default delete do else export extends false finally for from function get if import in instanceof let new null of return set static super switch this throw true try typeof undefined var void while with yield", true), script("typescript", "//", "abstract any as asserts async await boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object of private protected public readonly require return set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield", true), script("rust", "//", "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while", false), script("go", "//", "break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var false true nil", true), script("shell", "#", "if then else elif fi case esac for while until do done in function select time coproc return break continue export local readonly declare typeset unset shift exit", true),
    };

    for (const auto& language : scripts) {
        if (language.name == name) {
            return Result<const TextEditor::Language*>::success(&language);
        }
    }

    return Result<const TextEditor::Language*>::failure({"editor_language_unknown", "A code editor names a language it does not highlight", std::string(name)});
}

TextEditor::Language CodeLanguage::script(std::string_view name, std::string_view comment, std::string_view keywords, bool templates) {
    TextEditor::Language language;
    language.name = name;
    language.singleLineComment = comment;
    language.commentStart = comment == "//" ? "/*" : "";
    language.commentEnd = comment == "//" ? "*/" : "";
    language.hasSingleQuotedStrings = true;
    language.hasDoubleQuotedStrings = true;
    language.stringEscape = '\\';
    language.otherStringStart = templates ? "`" : "";
    language.otherStringEnd = language.otherStringStart;
    const TextEditor::Language& tokens = *TextEditor::Language::C();
    language.isPunctuation = tokens.isPunctuation;
    language.getIdentifier = tokens.getIdentifier;
    language.getNumber = tokens.getNumber;

    while (!keywords.empty()) {
        const std::size_t end = std::min(keywords.find(' '), keywords.size());
        language.keywords.emplace(keywords.substr(0, end));
        keywords.remove_prefix(std::min(end + 1, keywords.size()));
    }

    return language;
}

// A defined language reads words, numbers and punctuation the way C does, and a language that ignores case keeps its word lists in lower case as the library expects.
Result<std::unique_ptr<TextEditor::Language>> CodeLanguage::defined(const json::Json& definition, std::string_view context) {
    auto language = std::make_unique<TextEditor::Language>();
    std::string escape;
    std::string preprocessor;
    std::vector<std::string> keywords;
    std::vector<std::string> declarations;
    std::vector<std::string> identifiers;
    std::vector<std::string> named;
    const json::Json* block = &json::ObjectReader::emptyObject();
    json::ObjectReader reader(definition, std::string(context));
    reader.readText("name", language->name).read("caseSensitive", language->caseSensitive, json::Presence::Optional).read("lineComment", language->singleLineComment, json::Presence::Optional).read("lineCommentAlternative", language->singleLineCommentAlt, json::Presence::Optional).readObject("blockComment", block, json::Presence::Optional);
    reader.read("singleQuotes", language->hasSingleQuotedStrings, json::Presence::Optional).read("doubleQuotes", language->hasDoubleQuotedStrings, json::Presence::Optional).read("escape", escape, json::Presence::Optional).read("preprocessor", preprocessor, json::Presence::Optional);
    reader.read("keywords", keywords, json::Presence::Optional).read("declarations", declarations, json::Presence::Optional).read("identifiers", identifiers, json::Presence::Optional).read("constructs", named, json::Presence::Optional);

    if (auto finished = reader.finish(); !finished.hasValue()) {
        return Result<std::unique_ptr<TextEditor::Language>>::failure(finished.error());
    }

    if (!block->empty()) {
        json::ObjectReader blockReader(*block, std::string(context) + ".blockComment");
        blockReader.readText("start", language->commentStart).readText("end", language->commentEnd);

        if (auto finished = blockReader.finish(); !finished.hasValue()) {
            return Result<std::unique_ptr<TextEditor::Language>>::failure(finished.error());
        }
    }

    if (escape.size() > 1 || preprocessor.size() > 1 || keywords.size() + declarations.size() + identifiers.size() > largestWordList) {
        return Result<std::unique_ptr<TextEditor::Language>>::failure({"editor_language_invalid", "An escape and a preprocessor mark are one character, and the word lists are bounded", std::string(context)});
    }

    language->stringEscape = escape.empty() ? 0 : static_cast<ImWchar>(escape.front());
    language->preprocess = preprocessor.empty() ? 0 : static_cast<ImWchar>(preprocessor.front());
    // clang-format off
    const auto fill = [&language](const std::vector<std::string>& words, std::unordered_set<std::string>& into) {
        for (std::string word : words) {
            if (!language->caseSensitive) {
                std::ranges::transform(word, word.begin(), [](char character) { return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character; });
            }

            into.insert(std::move(word));
        }
    };
    // clang-format on

    fill(keywords, language->keywords);
    fill(declarations, language->declarations);
    fill(identifiers, language->identifiers);

    std::vector<Construct> constructs;

    for (const std::string& name : named) {
        // clang-format off
        const auto known = std::ranges::find_if(constructNames, [&name](const auto& entry) { return entry.first == name; });
        // clang-format on

        if (known == constructNames.end()) {
            return Result<std::unique_ptr<TextEditor::Language>>::failure({"editor_language_invalid", "A language names a construct the editor does not color", name});
        }

        constructs.push_back(known->second);
    }

    const TextEditor::Language& c = *TextEditor::Language::C();
    language->isPunctuation = c.isPunctuation;
    language->getIdentifier = c.getIdentifier;
    language->getNumber = c.getNumber;

    if (!constructs.empty()) {
        // clang-format off
        language->customTokenizer = [constructs](TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color) { return construct(constructs, start, end, color); };
        // clang-format on
    }

    return Result<std::unique_ptr<TextEditor::Language>>::success(std::move(language));
}

bool CodeLanguage::letter(ImWchar character) {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '_';
}

bool CodeLanguage::wordCharacter(ImWchar character) {
    return letter(character) || (character >= '0' && character <= '9');
}

// A decorator is an at sign followed by a dotted name, colored like the preprocessor.
TextEditor::Iterator CodeLanguage::decorator(TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color) {
    TextEditor::Iterator position = start;

    if (position == end || *position != '@' || ++position == end || !letter(*position)) {
        return start;
    }

    while (position != end && (wordCharacter(*position) || *position == '.')) {
        ++position;
    }

    color = TextEditor::Color::preprocessor;

    return position;
}

// A tag opens with its name or closes with its bracket, colored like a keyword, and a name followed by an equals sign is an attribute, colored like a declaration.
TextEditor::Iterator CodeLanguage::tag(TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color) {
    TextEditor::Iterator position = start;

    if (*position == '>' || (*position == '/' && ++position != end && *position == '>')) {
        color = TextEditor::Color::keyword;
        return ++position;
    }

    position = start;

    if (*position == '<') {
        ++position;
        position = position != end && *position == '/' ? ++position : position;

        if (position == end || !letter(*position)) {
            return start;
        }

        while (position != end && (wordCharacter(*position) || *position == '-' || *position == ':' || *position == '.')) {
            ++position;
        }

        color = TextEditor::Color::keyword;
        return position;
    }

    if (!letter(*position)) {
        return start;
    }

    while (position != end && (wordCharacter(*position) || *position == '-' || *position == ':' || *position == '.')) {
        ++position;
    }

    TextEditor::Iterator after = position;

    while (after != end && (*after == ' ' || *after == '\t')) {
        ++after;
    }

    if (after == end || *after != '=') {
        return start;
    }

    color = TextEditor::Color::declaration;

    return position;
}

// The constructs a language names are tried in their order, and the first that recognises the text at the position colors it.
TextEditor::Iterator CodeLanguage::construct(const std::vector<Construct>& constructs, TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color) {
    if (start == end) {
        return start;
    }

    for (const Construct kind : constructs) {
        const TextEditor::Iterator found = kind == Construct::Markdown ? TextEditor::Language::Markdown()->customTokenizer(start, end, color) : kind == Construct::Decorators ? decorator(start, end, color) : tag(start, end, color);

        if (found != start) {
            return found;
        }
    }

    return start;
}

} // namespace workpane::ui
