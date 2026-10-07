#include "scripting/ScriptRuntime.h"

#include "platform/PathTextHelper.h"
#include "scripting/HostReply.h"

#include <varn/varn.h>

#include <limits>
#include <tuple>
#include <utility>

namespace workpane::scripting {

ScriptRuntime::ConsoleSink& ScriptRuntime::sink() {
    static ScriptRuntime::ConsoleSink handler;
    return handler;
}

std::mutex& ScriptRuntime::sinkMutex() {
    static std::mutex mutex;
    return mutex;
}

void ScriptRuntime::console(int level, const char* message, void*) {
    const std::scoped_lock lock(sinkMutex());

    if (sink()) {
        sink()(level, message == nullptr ? std::string() : std::string(message));
    }
}

Result<std::unique_ptr<ScriptRuntime>> ScriptRuntime::create() {
    varn_runtime* runtime = varn_runtime_new();

    if (runtime == nullptr) {
        return Result<std::unique_ptr<ScriptRuntime>>::failure({"script_runtime_unavailable", "The Lua runtime could not be created", varn_version()});
    }

    return Result<std::unique_ptr<ScriptRuntime>>::success(std::unique_ptr<ScriptRuntime>(new ScriptRuntime(runtime)));
}

ScriptRuntime::ScriptRuntime(varn_runtime* runtime) : m_runtime(runtime) {}

ScriptRuntime::~ScriptRuntime() {
    varn_runtime_set_wake(m_runtime, nullptr, nullptr);
    varn_runtime_stop(m_runtime);
    varn_runtime_free(m_runtime);
}

// Every function is registered before the first chunk runs, which is when the runtime publishes the host table.
// A function whose calls can change what the window shows is registered with the screen effect, so each of its calls asks the frame loop for a frame.
Result<void> ScriptRuntime::registerFunction(const std::string& name, Effect effect, HostFunction function) {
    auto binding = std::make_unique<Binding>();
    binding->name = name;
    binding->function = std::move(function);
    binding->changes = effect == Effect::Screen ? &m_changes : nullptr;
    binding->closed = &m_closed;
    binding->observer = &m_observer;

    if (varn_runtime_register(m_runtime, name.c_str(), &ScriptRuntime::trampoline, binding.get()) != 0) {
        return Result<void>::failure({"script_function_refused", "The Lua runtime refused a host function", name});
    }

    m_bindings.push_back(std::move(binding));

    return Result<void>::success();
}

Result<void> ScriptRuntime::loadFile(const std::filesystem::path& file) {
    const std::string path = platform::PathTextHelper::utf8(file);

    if (varn_runtime_load_file(m_runtime, path.c_str()) != 0) {
        return Result<void>::failure({"script_load_failed", "A Lua chunk failed while loading", path});
    }

    return Result<void>::success();
}

Result<void> ScriptRuntime::loadString(std::string_view source, std::string_view chunkName) {
    const std::string text(source);
    const std::string name(chunkName);

    if (varn_runtime_load_string(m_runtime, text.c_str(), name.c_str()) != 0) {
        return Result<void>::failure({"script_load_failed", "A Lua chunk failed while loading", name});
    }

    return Result<void>::success();
}

// Posts an event to the Lua handlers subscribed to it, from any thread, delivered on the next poll.
void ScriptRuntime::emit(std::string_view name, const nlohmann::json& payload) {
    if (m_observer) {
        m_observer(name, payload, nullptr);
    }

    const std::string event(name);
    const std::string argument = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    varn_runtime_emit(m_runtime, event.c_str(), argument.c_str());
}

// The observer sees every message that crosses the bridge on the thread that sends it, a host call with its reply and an event without one, which is how the product suite reads what the plugins declared.
void ScriptRuntime::observe(Observer observer) {
    m_observer = std::move(observer);
}

// The handler hears from any thread that work reached the runtime, such as a promise a worker settled, and the runtime stops calling the previous one before it changes.
void ScriptRuntime::setWakeHandler(WakeHandler handler) {
    varn_runtime_set_wake(m_runtime, nullptr, nullptr);
    m_wake = std::move(handler);

    if (m_wake) {
        varn_runtime_set_wake(m_runtime, &ScriptRuntime::wake, this);
    }
}

void ScriptRuntime::wake(void* userdata) {
    static_cast<ScriptRuntime*>(userdata)->m_wake();
}

// Every host function answers a structured error from here on without reaching its host, so a call Lua makes later, such as one from a finalizer, never meets a host that is gone.
void ScriptRuntime::close() {
    m_closed = true;
}

// Advances the runtime without blocking, repeating passes while they make progress within a bounded time, so a chain of promises settles in one turn of the loop and never holds a frame longer than that bound.
void ScriptRuntime::poll() {
    std::ignore = varn_runtime_poll_budget(m_runtime, pollBudgetNanoseconds, nullptr);
}

// Answers how long the runtime can wait before it has work of its own, which is forever when only a wake can bring work.
double ScriptRuntime::idleSeconds() const {
    const long long idle = varn_runtime_idle(m_runtime);
    return idle < 0 ? std::numeric_limits<double>::infinity() : static_cast<double>(idle) / 1000.0;
}

// Counts the calls Lua made into the host that can change what the window shows, which is how the frame loop learns that a poll left something to draw.
std::uint64_t ScriptRuntime::changes() const {
    return m_changes;
}

// The engine writes every console line through one process wide sink, which reaches the handler given here.
void ScriptRuntime::setConsole(ConsoleSink sink) {
    {
        const std::scoped_lock lock(sinkMutex());
        ScriptRuntime::sink() = std::move(sink);
    }

    varn_set_console(ScriptRuntime::sink() ? &ScriptRuntime::console : nullptr, nullptr);
}

bool ScriptRuntime::deeper(const nlohmann::json& value, int levels) {
    if (!value.is_structured()) {
        return false;
    }

    if (levels == 0) {
        return true;
    }

    for (const auto& item : value) {
        if (deeper(item, levels - 1)) {
            return true;
        }
    }

    return false;
}

// The engine copies the answer before Lua resumes, so the buffer only has to outlive the call it answers.
const char* ScriptRuntime::trampoline(const char* argument, void* userdata) {
    auto& binding = *static_cast<Binding*>(userdata);

    if (*binding.closed) {
        binding.reply = HostReply::failure({"bridge_closed", "The product closed the bridge to the host", {}}).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return binding.reply.c_str();
    }

    if (binding.changes != nullptr) {
        ++*binding.changes;
    }

    const auto parsed = nlohmann::json::parse(argument == nullptr ? "null" : argument, nullptr, false);

    if (parsed.is_discarded()) {
        binding.reply = HostReply::failure({"script_argument_invalid", "A host function received an argument that is not JSON", {}}).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return binding.reply.c_str();
    }

    // The engine reads back far fewer levels than it writes, so a value deeper than it reads again is refused before the host keeps it.
    if (deeper(parsed, deepestArgument)) {
        binding.reply = HostReply::failure({"json_field_depth", "A value sent to the host is nested too deeply", {}}).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return binding.reply.c_str();
    }

    const nlohmann::json reply = binding.function(parsed);

    if (*binding.observer) {
        (*binding.observer)(binding.name, parsed, &reply);
    }

    binding.reply = reply.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    return binding.reply.c_str();
}

} // namespace workpane::scripting
