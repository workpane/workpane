#pragma once

#include "Result.h"

#include <gio/gio.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::platform {

class LinuxSecretService final {
  public:
    [[nodiscard]] static Result<std::unique_ptr<LinuxSecretService>> open();
    ~LinuxSecretService();

    LinuxSecretService(const LinuxSecretService&) = delete;
    LinuxSecretService& operator=(const LinuxSecretService&) = delete;

    [[nodiscard]] Result<std::optional<std::string>> read(std::string_view service, std::string_view account);
    [[nodiscard]] Result<void> write(std::string_view service, std::string_view account, std::string_view value);
    [[nodiscard]] Result<void> erase(std::string_view service, std::string_view account);

  private:
    using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;

    struct Prompt final {
        bool completed{false};
        bool dismissed{false};
    };

    explicit LinuxSecretService(GDBusConnection* bus);
    [[nodiscard]] Variant call(std::string_view path, const char* interface, const char* method, GVariant* parameters, const char* replyType);
    [[nodiscard]] Result<void> prompt(std::string_view path);
    [[nodiscard]] Result<void> unlock(std::string_view path);
    [[nodiscard]] Result<std::optional<std::string>> find(std::string_view service, std::string_view account);
    [[nodiscard]] static GVariant* attributes(std::string_view service, std::string_view account);
    [[nodiscard]] static Error unavailable();
    [[nodiscard]] static std::string childString(GVariant* value, std::size_t index);

    GDBusConnection* m_bus;
    std::string m_session;
};

} // namespace workpane::platform
