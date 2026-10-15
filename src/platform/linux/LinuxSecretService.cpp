#include "platform/linux/LinuxSecretService.h"

#include "platform/NativeCredentialHelper.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace workpane::platform {

LinuxSecretService::LinuxSecretService(GDBusConnection* bus) : m_bus(bus) {}

LinuxSecretService::~LinuxSecretService() {
    if (!m_session.empty()) {
        std::ignore = call(m_session, "org.freedesktop.Secret.Session", "Close", nullptr, "()");
    }

    g_object_unref(m_bus);
}

Error LinuxSecretService::unavailable() {
    return {"credential_store_unavailable", "The system credential store is unavailable", ""};
}

Result<std::unique_ptr<LinuxSecretService>> LinuxSecretService::open() {
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);

    if (bus == nullptr) {
        g_clear_error(&error);

        return Result<std::unique_ptr<LinuxSecretService>>::failure(unavailable());
    }

    auto store = std::unique_ptr<LinuxSecretService>(new LinuxSecretService(bus));
    const auto answer = store->call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "OpenSession", g_variant_new("(sv)", "plain", g_variant_new_string("")), "(vo)");

    if (answer == nullptr) {
        return Result<std::unique_ptr<LinuxSecretService>>::failure(unavailable());
    }

    store->m_session = childString(answer.get(), 1);

    return Result<std::unique_ptr<LinuxSecretService>>::success(std::move(store));
}

LinuxSecretService::Variant LinuxSecretService::call(std::string_view path, const char* interface, const char* method, GVariant* parameters, const char* replyType) {
    GError* error = nullptr;
    GVariant* answer = g_dbus_connection_call_sync(m_bus, "org.freedesktop.secrets", std::string(path).c_str(), interface, method, parameters, G_VARIANT_TYPE(replyType), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);
    g_clear_error(&error);

    return Variant(answer, &g_variant_unref);
}

std::string LinuxSecretService::childString(GVariant* value, std::size_t index) {
    const Variant child(g_variant_get_child_value(value, index), &g_variant_unref);
    return g_variant_get_string(child.get(), nullptr);
}

GVariant* LinuxSecretService::attributes(std::string_view service, std::string_view account) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_add(&builder, "{ss}", "application", "workpane");
    g_variant_builder_add(&builder, "{ss}", "service", std::string(service).c_str());
    g_variant_builder_add(&builder, "{ss}", "account", std::string(account).c_str());

    return g_variant_builder_end(&builder);
}

Result<void> LinuxSecretService::prompt(std::string_view path) {
    if (path == "/") {
        return Result<void>::success();
    }

    GMainContext* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    Prompt state;
    // clang-format off
    const auto completed = [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer data) {
        auto& waiting = *static_cast<Prompt*>(data);
        gboolean dismissed = FALSE;
        GVariant* result = nullptr;
        g_variant_get(parameters, "(b@v)", &dismissed, &result);
        g_variant_unref(result);
        waiting.dismissed = dismissed != FALSE;
        waiting.completed = true;
    };
    // clang-format on

    const guint subscription = g_dbus_connection_signal_subscribe(m_bus, "org.freedesktop.secrets", "org.freedesktop.Secret.Prompt", "Completed", std::string(path).c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, completed, &state, nullptr);
    const auto started = call(path, "org.freedesktop.Secret.Prompt", "Prompt", g_variant_new("(s)", ""), "()");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while (started != nullptr && !state.completed && std::chrono::steady_clock::now() < deadline) {
        std::ignore = g_main_context_iteration(context, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (started != nullptr && !state.completed) {
        std::ignore = call(path, "org.freedesktop.Secret.Prompt", "Dismiss", nullptr, "()");
    }

    g_dbus_connection_signal_unsubscribe(m_bus, subscription);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);

    if (!state.completed || state.dismissed) {
        return Result<void>::failure({"credential_store_locked", "The system credential store was not unlocked", ""});
    }

    return Result<void>::success();
}

Result<void> LinuxSecretService::unlock(std::string_view path) {
    const char* objects[] = {path.data(), nullptr};
    const auto answer = call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "Unlock", g_variant_new("(^ao)", objects), "(aoo)");

    if (answer == nullptr) {
        return Result<void>::failure(unavailable());
    }

    return prompt(childString(answer.get(), 1));
}

Result<std::optional<std::string>> LinuxSecretService::find(std::string_view service, std::string_view account) {
    const auto answer = call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "SearchItems", g_variant_new("(@a{ss})", attributes(service, account)), "(aoao)");

    if (answer == nullptr) {
        return Result<std::optional<std::string>>::failure(unavailable());
    }

    for (std::size_t index = 0; index < 2; ++index) {
        const Variant items(g_variant_get_child_value(answer.get(), index), &g_variant_unref);

        if (g_variant_n_children(items.get()) == 0) {
            continue;
        }

        const std::string item = childString(items.get(), 0);

        if (index == 1) {
            if (const auto unlocked = unlock(item); !unlocked.hasValue()) {
                return Result<std::optional<std::string>>::failure(unlocked.error());
            }
        }

        return Result<std::optional<std::string>>::success(item);
    }

    return Result<std::optional<std::string>>::success(std::nullopt);
}

Result<std::optional<std::string>> LinuxSecretService::read(std::string_view service, std::string_view account) {
    const auto item = find(service, account);

    if (!item.hasValue() || !item.value().has_value()) {
        return item;
    }

    const auto answer = call(*item.value(), "org.freedesktop.Secret.Item", "GetSecret", g_variant_new("(o)", m_session.c_str()), "((oayays))");

    if (answer == nullptr) {
        return Result<std::optional<std::string>>::failure(unavailable());
    }

    const Variant secret(g_variant_get_child_value(answer.get(), 0), &g_variant_unref);
    const Variant data(g_variant_get_child_value(secret.get(), 2), &g_variant_unref);
    gsize size = 0;
    const auto bytes = static_cast<const char*>(g_variant_get_fixed_array(data.get(), &size, 1));

    if (size > NativeCredentialHelper::maximumValueBytes) {
        return Result<std::optional<std::string>>::failure({"credential_value_invalid", "The stored credential is invalid", ""});
    }

    return Result<std::optional<std::string>>::success(size == 0 ? std::string() : std::string(bytes, size));
}

Result<void> LinuxSecretService::write(std::string_view service, std::string_view account, std::string_view value) {
    const auto alias = call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "ReadAlias", g_variant_new("(s)", "default"), "(o)");

    if (alias == nullptr) {
        return Result<void>::failure(unavailable());
    }

    const std::string collection = childString(alias.get(), 0);

    if (collection == "/") {
        return Result<void>::failure(unavailable());
    }

    if (const auto unlocked = unlock(collection); !unlocked.hasValue()) {
        return unlocked;
    }

    GVariantBuilder properties;
    g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&properties, "{sv}", "org.freedesktop.Secret.Item.Label", g_variant_new_string(std::string(service).c_str()));
    g_variant_builder_add(&properties, "{sv}", "org.freedesktop.Secret.Item.Attributes", attributes(service, account));
    GVariant* secret = g_variant_new("(o@ay@ays)", m_session.c_str(), g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, nullptr, 0, 1), g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, value.data(), value.size(), 1), "text/plain; charset=utf-8");
    const auto created = call(collection, "org.freedesktop.Secret.Collection", "CreateItem", g_variant_new("(@a{sv}@(oayays)b)", g_variant_builder_end(&properties), secret, TRUE), "(oo)");

    if (created == nullptr) {
        return Result<void>::failure(unavailable());
    }

    return prompt(childString(created.get(), 1));
}

Result<void> LinuxSecretService::erase(std::string_view service, std::string_view account) {
    const auto item = find(service, account);

    if (!item.hasValue()) {
        return Result<void>::failure(item.error());
    }

    if (!item.value().has_value()) {
        return Result<void>::success();
    }

    const auto answer = call(*item.value(), "org.freedesktop.Secret.Item", "Delete", nullptr, "(o)");

    if (answer == nullptr) {
        return Result<void>::failure(unavailable());
    }

    return prompt(childString(answer.get(), 0));
}

} // namespace workpane::platform
