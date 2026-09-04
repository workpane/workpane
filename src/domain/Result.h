#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace workpane {

struct Error final {
    QString code;
    QString message;
    QString detail;
};

template <typename T> class [[nodiscard]] Result final {
  public:
    static Result success(T value) {
        return Result(std::move(value));
    }
    static Result failure(Error error) {
        return Result(std::move(error));
    }

    [[nodiscard]] bool hasValue() const {
        return m_value.has_value();
    }
    [[nodiscard]] const T& value() const {
        return m_value.value();
    }
    [[nodiscard]] T& value() {
        return m_value.value();
    }
    [[nodiscard]] const Error& error() const {
        return m_error.value();
    }

  private:
    explicit Result(T value) : m_value(std::move(value)) {}
    explicit Result(Error error) : m_error(std::move(error)) {}

    std::optional<T> m_value;
    std::optional<Error> m_error;
};

template <> class [[nodiscard]] Result<void> final {
  public:
    static Result success() {
        return Result();
    }
    static Result failure(Error error) {
        return Result(std::move(error));
    }

    [[nodiscard]] bool hasValue() const {
        return !m_error.has_value();
    }
    [[nodiscard]] const Error& error() const {
        return m_error.value();
    }

  private:
    Result() = default;
    explicit Result(Error error) : m_error(std::move(error)) {}

    std::optional<Error> m_error;
};

} // namespace workpane
