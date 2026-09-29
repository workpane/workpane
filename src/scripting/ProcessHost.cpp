#include "scripting/ProcessHost.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "json/ObjectReader.h"
#include "platform/PathText.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessLauncher.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"

#include <filesystem>
#include <string>
#include <utility>

namespace workpane::scripting {

ProcessHost::ProcessHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies) {}

// Every program still running is asked to end at once and then waited for, so none of them outlives the product.
ProcessHost::~ProcessHost() {
    for (auto& [id, running] : m_running) {
        running.process->stop();
    }

    m_running.clear();
}

Result<void> ProcessHost::registerFunctions() {
    // clang-format off
    const std::vector<std::pair<std::string, ScriptRuntime::HostFunction>> functions{
        {"workpane_process_start", [this](const nlohmann::json& argument) { return start(argument); }},
        {"workpane_process_write", [this](const nlohmann::json& argument) { return write(argument); }},
        {"workpane_process_stop", [this](const nlohmann::json& argument) { return stop(argument); }},
        {"workpane_process_forget", [this](const nlohmann::json& argument) { return forget(argument); }},
        {"workpane_process_find", [this](const nlohmann::json& argument) { return find(argument); }},
    };
    // clang-format on

    for (const auto& [name, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, ScriptRuntime::Effect::Background, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

std::string ProcessHost::streamName(process::ProcessStream stream) {
    return stream == process::ProcessStream::Output ? "output" : "error";
}

// A program is named by an absolute path and starts without a shell, so nothing a plugin passes is ever read as a command line.
nlohmann::json ProcessHost::start(const nlohmann::json& argument) {
    std::string plugin;
    std::string program;
    std::string directory;
    std::vector<std::string> arguments;
    std::vector<std::string> cleared;
    std::string input;
    const bool inputGiven = argument.contains("input");
    const json::Json* variables = nullptr;
    json::ObjectReader reader(argument, "process.start");
    reader.readText("plugin", plugin).readText("program", program).read("arguments", arguments).readText("directory", directory).readObject("variables", variables).read("cleared", cleared).read("input", input, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    process::ProcessLaunch launch{std::filesystem::path(std::u8string(program.begin(), program.end())), arguments, std::filesystem::path(std::u8string(directory.begin(), directory.end())), {}, cleared, inputGiven ? std::optional<std::string>(std::move(input)) : std::nullopt};

    if (!launch.program.is_absolute() || !launch.directory.is_absolute() || arguments.size() > maximumArguments || (launch.input.has_value() && launch.input->size() > maximumInput)) {
        return HostReply::failure({"process_launch_invalid", "A program is started from an absolute path in an absolute directory with a bounded list of arguments and a bounded input", program});
    }

    for (const auto& [name, value] : variables->items()) {
        if (!value.is_string() || name.empty() || name.find('=') != std::string::npos) {
            return HostReply::failure({"process_launch_invalid", "An environment variable has a name without an equals sign and a text value", name});
        }

        launch.variables.emplace_back(name, value.get<std::string>());
    }

    for (const auto& name : cleared) {
        if (name.empty() || name.find('=') != std::string::npos) {
            return HostReply::failure({"process_launch_invalid", "A cleared variable names a variable", name});
        }
    }

    const std::int64_t identity = ++m_next;
    const std::shared_ptr<Mailbox> mailbox = m_mailbox;
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    const auto posted = [mailbox, alive, this, mainThread = &m_services.mainThread]() {
        if (std::exchange(mailbox->posted, true)) {
            return;
        }

        mainThread->post([alive, this]() {
            if (!alive.expired()) {
                deliver();
            }
        });
    };

    process::ProcessEvents events{
        [mailbox, identity, posted](process::ProcessStream stream, std::string bytes) {
            const std::lock_guard lock(mailbox->mutex);
            auto& chunks = mailbox->pending[identity].chunks;

            if (!chunks.empty() && chunks.back().first == stream) {
                chunks.back().second += bytes;
            } else {
                chunks.emplace_back(stream, std::move(bytes));
            }

            posted();
        },
        [mailbox, identity, posted](process::ProcessEnd end) {
            const std::lock_guard lock(mailbox->mutex);
            mailbox->pending[identity].exit = end;
            posted();
        },
    };
    // clang-format on

    auto started = m_services.processes.start(launch, std::move(events));

    if (!started.hasValue()) {
        return HostReply::failure(started.error());
    }

    m_running.emplace(identity, Running{plugin, std::move(started.value()), {}, {}, false});

    return HostReply::success({{"process", identity}});
}

nlohmann::json ProcessHost::write(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t identity = 0;
    std::string text;
    json::ObjectReader reader(argument, "process.write");
    reader.readText("plugin", plugin).readInteger("process", identity, 1, largestRequest).read("text", text);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    auto running = owned(plugin, identity);

    if (!running.hasValue()) {
        return HostReply::failure(running.error());
    }

    if (running.value()->retiring || !running.value()->process->write(text)) {
        return HostReply::failure({"process_input_refused", "The program has ended or has not read what it was already sent", std::to_string(identity)});
    }

    return HostReply::success();
}

// A stopped program still reports its end, which is when it is released.
nlohmann::json ProcessHost::stop(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t identity = 0;
    json::ObjectReader reader(argument, "process.stop");
    reader.readText("plugin", plugin).readInteger("process", identity, 1, largestRequest);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    auto running = owned(plugin, identity);

    if (!running.hasValue()) {
        return HostReply::failure(running.error());
    }

    running.value()->process->stop();

    return HostReply::success();
}

// A plugin that is withdrawn loses every program it started, whose ends are no longer delivered to it.
nlohmann::json ProcessHost::forget(const nlohmann::json& argument) {
    std::string plugin;
    json::ObjectReader reader(argument, "process.forget");
    reader.readText("plugin", plugin);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    for (auto& [id, running] : m_running) {
        if (running.owner == plugin) {
            running.retiring = true;
            running.process->stop();
        }
    }

    return HostReply::success();
}

// Looking through the search path touches the disk, so it runs on a worker and answers the request with the path or nothing.
nlohmann::json ProcessHost::find(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string name;
    std::vector<std::string> directories;
    json::ObjectReader reader(argument, "process.find");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("name", name).read("directories", directories);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    std::vector<std::filesystem::path> searched;

    for (const auto& directory : directories) {
        searched.emplace_back(std::u8string(directory.begin(), directory.end()));
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, request, name, searched, launcher = &m_services.processes, mainThread = &m_services.mainThread]() {
        const auto found = launcher->find(name, searched);
        mainThread->post([this, alive, request, found]() {
            if (!alive.expired()) {
                m_replies.reply(request, Result<nlohmann::json>::success(found.has_value() ? nlohmann::json{{"found", true}, {"path", platform::PathText::generic(*found)}} : nlohmann::json{{"found", false}}));
            }
        });
    });
    // clang-format on

    return HostReply::success();
}

Result<ProcessHost::Running*> ProcessHost::owned(const std::string& plugin, std::int64_t process) {
    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return Result<Running*>::failure(known.error());
    }

    const auto found = m_running.find(process);

    if (found == m_running.end() || found->second.owner != plugin) {
        return Result<Running*>::failure({"process_unknown", "The program has ended or belongs to another plugin", std::to_string(process)});
    }

    return Result<Running*>::success(&found->second);
}

// What arrived since the last turn reaches each owner in one batch, the end of a program comes after its last output, and an ended program is released on a worker.
void ProcessHost::deliver() {
    std::map<std::int64_t, Pending> pending;

    {
        const std::lock_guard lock(m_mailbox->mutex);
        pending.swap(m_mailbox->pending);
        m_mailbox->posted = false;
    }

    for (auto& [identity, arrived] : pending) {
        const auto found = m_running.find(identity);

        if (found == m_running.end()) {
            continue;
        }

        Running& running = found->second;
        nlohmann::json chunks = nlohmann::json::array();

        for (auto& [stream, bytes] : arrived.chunks) {
            std::string text = (stream == process::ProcessStream::Output ? running.output : running.error).take(bytes);

            if (!text.empty()) {
                chunks.push_back({{"stream", streamName(stream)}, {"text", std::move(text)}});
            }
        }

        if (arrived.exit.has_value()) {
            for (const auto stream : {process::ProcessStream::Output, process::ProcessStream::Error}) {
                if (std::string rest = (stream == process::ProcessStream::Output ? running.output : running.error).finish(); !rest.empty()) {
                    chunks.push_back({{"stream", streamName(stream)}, {"text", std::move(rest)}});
                }
            }
        }

        if (!chunks.empty() && !running.retiring) {
            m_runtime.emit("workpane.process.output", {{"plugin", running.owner}, {"process", identity}, {"chunks", std::move(chunks)}});
        }

        if (!arrived.exit.has_value()) {
            continue;
        }

        if (!running.retiring) {
            m_runtime.emit("workpane.process.exit", {{"plugin", running.owner}, {"process", identity}, {"code", arrived.exit->code}, {"crashed", arrived.exit->crashed}});
        }

        retire(std::move(running.process));
        m_running.erase(found);
    }
}

// The serving threads of an ended program are joined on a worker, so the interface never waits for them.
void ProcessHost::retire(std::unique_ptr<process::Process> process) {
    // clang-format off
    m_services.workers.post([ended = std::shared_ptr<process::Process>(std::move(process))]() mutable { ended.reset(); });
    // clang-format on
}

} // namespace workpane::scripting
