#include "json/ObjectReader.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace workpane::json {

ObjectReader::ObjectReader(const Json& value, std::string context) : m_value(value), m_context(std::move(context)) {
    if (!m_value.is_object()) {
        m_failure = Error{"json_not_object", "The value is not a JSON object", m_context};
        return;
    }

    m_consumed.reserve(m_value.size());
}

ObjectReader& ObjectReader::read(std::string_view key, std::string& out, Presence presence) {
    const Json* value = locate(key, presence);

    if (value == nullptr) {
        return *this;
    }

    if (!value->is_string()) {
        fail("json_field_type", "A field carries a value of the wrong type", key);
        return *this;
    }

    out = value->get<std::string>();

    return *this;
}

ObjectReader& ObjectReader::read(std::string_view key, bool& out, Presence presence) {
    const Json* value = locate(key, presence);

    if (value == nullptr) {
        return *this;
    }

    if (!value->is_boolean()) {
        fail("json_field_type", "A field carries a value of the wrong type", key);
        return *this;
    }

    out = value->get<bool>();

    return *this;
}

ObjectReader& ObjectReader::read(std::string_view key, std::vector<std::string>& out, Presence presence) {
    const Json* value = locate(key, presence);

    if (value == nullptr) {
        return *this;
    }

    if (!isList(*value)) {
        fail("json_field_type", "A field carries a value of the wrong type", key);
        return *this;
    }

    std::vector<std::string> items;
    items.reserve(value->size());

    for (const auto& item : *value) {
        if (!item.is_string()) {
            fail("json_field_type", "A list carries an entry of the wrong type", key);
            return *this;
        }

        items.push_back(item.get<std::string>());
    }

    out = std::move(items);

    return *this;
}

ObjectReader& ObjectReader::readText(std::string_view key, std::string& out, Presence presence) {
    const bool wasFailing = m_failure.has_value();
    std::string text;
    read(key, text, presence);

    if (wasFailing || m_failure.has_value() || !contains(key)) {
        return *this;
    }

    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        fail("json_field_empty", "A field that names something is empty", key);
        return *this;
    }

    out = std::move(text);

    return *this;
}

ObjectReader& ObjectReader::readInteger(std::string_view key, std::int64_t& out, std::int64_t minimum, std::int64_t maximum, Presence presence) {
    const Json* value = locate(key, presence);

    if (value == nullptr) {
        return *this;
    }

    // Lua writes a whole number it computed with a fraction, such as half of an even height, as a real, which is read as the integer it is.
    const bool whole = value->is_number_float() && std::isfinite(value->get<double>()) && std::trunc(value->get<double>()) == value->get<double>();

    if (!value->is_number_integer() && !whole) {
        fail("json_field_type", "A field carries a value of the wrong type", key);
        return *this;
    }

    // A value beyond the signed range is out of every range this product declares, so it is refused before any conversion.
    if ((value->is_number_unsigned() && value->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) || (whole && (value->get<double>() < -signedBound || value->get<double>() >= signedBound))) {
        fail("json_field_range", "A field carries a value outside its range", key);
        return *this;
    }

    const auto number = whole ? static_cast<std::int64_t>(value->get<double>()) : value->get<std::int64_t>();

    if (number < minimum || number > maximum) {
        fail("json_field_range", "A field carries a value outside its range", key);
        return *this;
    }

    out = number;

    return *this;
}

ObjectReader& ObjectReader::readNumber(std::string_view key, double& out, double minimum, double maximum, Presence presence) {
    const Json* value = locate(key, presence);

    if (value == nullptr) {
        return *this;
    }

    if (!value->is_number()) {
        fail("json_field_type", "A field carries a value of the wrong type", key);
        return *this;
    }

    const auto number = value->get<double>();

    if (!std::isfinite(number) || number < minimum || number > maximum) {
        fail("json_field_range", "A field carries a value outside its range", key);
        return *this;
    }

    out = number;

    return *this;
}

ObjectReader& ObjectReader::readObject(std::string_view key, const Json*& out, Presence presence) {
    const Json* value = locate(key, presence);

    if (value == nullptr) {
        return *this;
    }

    if (!value->is_object()) {
        fail("json_field_type", "A field carries a value of the wrong type", key);
        return *this;
    }

    out = value;

    return *this;
}

ObjectReader& ObjectReader::readArray(std::string_view key, const Json*& out, Presence presence) {
    const Json* value = locate(key, presence);

    if (value == nullptr) {
        return *this;
    }

    if (!isList(*value)) {
        fail("json_field_type", "A field carries a value of the wrong type", key);
        return *this;
    }

    out = value->is_array() ? value : &emptyList();

    return *this;
}

ObjectReader& ObjectReader::readAny(std::string_view key, const Json*& out, Presence presence) {
    const Json* value = locate(key, presence);

    if (value != nullptr) {
        out = value;
    }

    return *this;
}

// The values a field answers when it is absent or an empty list crossed as an empty object, shared and never changed.
const Json& ObjectReader::emptyList() {
    static const Json empty = Json::array();
    return empty;
}

const Json& ObjectReader::emptyObject() {
    static const Json empty = Json::object();
    return empty;
}

const Json& ObjectReader::absent() {
    static const Json nothing;
    return nothing;
}

// Lua has one table type, so an empty table crosses the bridge as an empty object and a field declared as a list reads it as an empty list.
bool ObjectReader::isList(const Json& value) {
    return value.is_array() || (value.is_object() && value.empty());
}

bool ObjectReader::contains(std::string_view key) const {
    return m_value.is_object() && m_value.find(key) != m_value.end();
}

Result<void> ObjectReader::finish() const {
    if (m_failure.has_value()) {
        return Result<void>::failure(*m_failure);
    }

    if (m_consumed.size() == m_value.size()) {
        return Result<void>::success();
    }

    for (const auto& [key, value] : m_value.items()) {
        if (std::ranges::find(m_consumed, &value) == m_consumed.end()) {
            return Result<void>::failure({"json_field_unknown", "An object carries a field nobody declares", m_context + "." + key});
        }
    }

    return Result<void>::success();
}

const Json* ObjectReader::locate(std::string_view key, Presence presence) {
    if (m_failure.has_value()) {
        return nullptr;
    }

    const auto found = m_value.find(key);

    // A field is remembered by the value it holds, so reading an object never copies the names of its fields.
    if (found != m_value.end()) {
        const Json* value = &*found;

        if (std::ranges::find(m_consumed, value) == m_consumed.end()) {
            m_consumed.push_back(value);
        }

        return value;
    }

    if (presence == Presence::Required) {
        fail("json_field_missing", "A required field is missing", key);
    }

    return nullptr;
}

// The first refusal is kept, because a later one describes something other than what the caller hit first.
void ObjectReader::fail(std::string code, std::string message, std::string_view key) {
    if (m_failure.has_value()) {
        return;
    }

    m_failure = Error{std::move(code), std::move(message), m_context + "." + std::string(key)};
}

} // namespace workpane::json
