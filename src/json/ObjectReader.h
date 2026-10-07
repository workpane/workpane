#pragma once

#include "Result.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::json {

using Json = nlohmann::json;

enum class Presence { Required, Optional };

// Reads the fields of one JSON object strictly, so a missing, mistyped, out of range or unknown field refuses the whole object.
// A closed set of choices is read by name, so a value outside it is refused rather than mapped to a default.
// An object, a list or any other value is answered as a pointer into the value read, which lives as long as that value, so reading never copies a tree.
class ObjectReader final {
  public:
    ObjectReader(const Json& value, std::string context);
    ObjectReader(Json&& value, std::string context) = delete;

    ObjectReader& read(std::string_view key, std::string& out, Presence presence = Presence::Required);
    ObjectReader& read(std::string_view key, bool& out, Presence presence = Presence::Required);
    ObjectReader& read(std::string_view key, std::vector<std::string>& out, Presence presence = Presence::Required);
    ObjectReader& readText(std::string_view key, std::string& out, Presence presence = Presence::Required);
    ObjectReader& readInteger(std::string_view key, std::int64_t& out, std::int64_t minimum, std::int64_t maximum, Presence presence = Presence::Required);
    ObjectReader& readNumber(std::string_view key, double& out, double minimum, double maximum, Presence presence = Presence::Required);
    ObjectReader& readObject(std::string_view key, const Json*& out, Presence presence = Presence::Required);
    ObjectReader& readArray(std::string_view key, const Json*& out, Presence presence = Presence::Required);
    ObjectReader& readAny(std::string_view key, const Json*& out, Presence presence = Presence::Required);

    template <typename E> ObjectReader& readChoice(std::string_view key, E& out, std::initializer_list<std::pair<std::string_view, E>> options, Presence presence = Presence::Required) {
        const Json* value = locate(key, presence);

        if (value == nullptr) {
            return *this;
        }

        if (!value->is_string()) {
            fail("json_field_type", "A field carries a value of the wrong type", key);
            return *this;
        }

        const auto& name = value->get_ref<const std::string&>();

        for (const auto& option : options) {
            if (option.first == name) {
                out = option.second;
                return *this;
            }
        }

        fail("json_field_choice", "A field carries a value outside its closed set", key);

        return *this;
    }

    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] Result<void> finish() const;
    [[nodiscard]] static bool isList(const Json& value);
    [[nodiscard]] static const Json& emptyList();
    [[nodiscard]] static const Json& emptyObject();
    [[nodiscard]] static const Json& absent();

  private:
    static constexpr double signedBound{9223372036854775808.0};

    [[nodiscard]] const Json* locate(std::string_view key, Presence presence);
    void fail(std::string code, std::string message, std::string_view key);

    const Json& m_value;
    std::string m_context;
    std::vector<const Json*> m_consumed;
    std::optional<Error> m_failure;
};

} // namespace workpane::json
