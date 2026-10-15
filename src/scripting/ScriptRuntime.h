#pragma once

#include "Result.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct varn_runtime;

namespace workpane::scripting {

// The Lua runtime of the product, driven one step at a time from the frame loop so every Lua callback runs on the interface thread.
class ScriptRuntime final {
  public:
    using HostFunction = std::function<nlohmann::json(const nlohmann::json& argument)>;
    using ConsoleSink = std::function<void(int level, std::string message)>;
    using WakeHandler = std::function<void()>;
    using Observer = std::function<void(std::string_view name, const nlohmann::json& message, const nlohmann::json* reply)>;
    enum class Effect { Screen, Background };

    [[nodiscard]] static Result<std::unique_ptr<ScriptRuntime>> create();

    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    [[nodiscard]] Result<void> registerFunction(const std::string& name, Effect effect, HostFunction function);
    [[nodiscard]] Result<void> loadFile(const std::filesystem::path& file);
    [[nodiscard]] Result<void> loadString(std::string_view source, std::string_view chunkName);
    void emit(std::string_view name, const nlohmann::json& payload);
    void setWakeHandler(WakeHandler handler);
    void observe(Observer observer);
    void close();
    void poll();
    [[nodiscard]] double idleSeconds() const;
    [[nodiscard]] std::uint64_t changes() const;
    static void setConsole(ConsoleSink sink);

  private:
    static constexpr int deepestArgument{64};
    static constexpr long long pollBudgetNanoseconds{4'000'000};

    static void console(int level, const char* message, void* userdata);
    static void wake(void* userdata);
    [[nodiscard]] static bool deeper(const nlohmann::json& value, int levels);
    [[nodiscard]] static ConsoleSink& sink();
    [[nodiscard]] static std::mutex& sinkMutex();

    struct Binding final {
        std::string name;
        HostFunction function;
        std::string reply;
        std::uint64_t* changes{nullptr};
        const bool* closed{nullptr};
        const Observer* observer{nullptr};
    };

    explicit ScriptRuntime(varn_runtime* runtime);
    static const char* trampoline(const char* argument, void* userdata);

    varn_runtime* m_runtime{nullptr};
    std::vector<std::unique_ptr<Binding>> m_bindings;
    std::uint64_t m_changes{0};
    bool m_closed{false};
    WakeHandler m_wake;
    Observer m_observer;
};

} // namespace workpane::scripting
