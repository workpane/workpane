#include "scripting/ProcessHost.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "json/ObjectReader.h"
#include "platform/PathTextHelper.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessLauncher.h"
#include "process/TerminalProcess.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"
#include "text/Base64Helper.h"
#include "text/Utf8Helper.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace workpane::scripting {

ProcessHost::ProcessHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies) {}

// Every program still running is asked to end at once and then waited for, so none of them outlives the product.
ProcessHost::~ProcessHost() {
    m_alive.reset();

    {
        const std::lock_guard lock(m_mailbox->mutex);
        m_mailbox->closed = true;
        m_mailbox->pending.clear();
    }

    m_mailbox->ready.notify_all();

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
        {"workpane_process_end_input", [this](const nlohmann::json& argument) { return endInput(argument); }},
        {"workpane_process_read", [this](const nlohmann::json& argument) { return read(argument); }},
        {"workpane_process_resize", [this](const nlohmann::json& argument) { return resize(argument); }},
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
    std::string encodedInput;
    bool binaryOutput = false;
    bool pullOutput = false;
    const bool inputGiven = argument.contains("inputBase64");
    const json::Json* variables = nullptr;
    const json::Json* terminal = nullptr;
    json::ObjectReader reader(argument, "process.start");
    reader.readText("plugin", plugin).readText("program", program).read("arguments", arguments).readText("directory", directory).readObject("variables", variables).read("cleared", cleared).read("inputBase64", encodedInput, json::Presence::Optional).read("binaryOutput", binaryOutput, json::Presence::Optional).read("pullOutput", pullOutput, json::Presence::Optional).readObject("terminal", terminal, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    if (program.size() > maximumPath || directory.size() > maximumPath || program.find('\0') != std::string::npos || directory.find('\0') != std::string::npos || !text::Utf8Helper::valid(program) || !text::Utf8Helper::valid(directory)) {
        return HostReply::failure({"process_launch_invalid", "The program launch contains an invalid path", ""});
    }

    auto decodedInput = decodeInput(encodedInput);

    if (!decodedInput.hasValue()) {
        return HostReply::failure(decodedInput.error());
    }

    process::ProcessLaunch launch{std::filesystem::path(std::u8string(program.begin(), program.end())), std::move(arguments), std::filesystem::path(std::u8string(directory.begin(), directory.end())), {}, std::move(cleared), inputGiven ? std::optional<std::string>(std::move(decodedInput.value())) : std::nullopt};

    for (const auto& [name, value] : variables->items()) {
        if (!value.is_string()) {
            return HostReply::failure({"process_launch_invalid", "An environment variable has a text value", ""});
        }

        launch.variables.emplace_back(name, value.get<std::string>());
    }

    if (const auto checked = launch.validate(); !checked.hasValue()) {
        return HostReply::failure(checked.error());
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
        [mailbox, identity, posted, pullOutput](process::ProcessStream stream, std::string bytes) {
            for (std::size_t offset = 0; offset < bytes.size();) {
                const std::size_t length = std::min(bytes.size() - offset, pullOutput ? maximumPullBytes : maximumPendingBytes);
                std::unique_lock lock(mailbox->mutex);
                mailbox->ready.wait(lock, [&mailbox, length, identity, stream, pullOutput]() {
                    const auto found = mailbox->pending.find(identity);
                    const bool merged = found != mailbox->pending.end() && !found->second.chunks.empty() && found->second.chunks.back().first == stream;
                    const std::size_t pendingBytes = found != mailbox->pending.end() ? found->second.bytes : 0;
                    const std::size_t pendingChunks = found != mailbox->pending.end() ? found->second.chunks.size() : 0;

                    return mailbox->closed || (length <= maximumPendingBytes - mailbox->bytes && (merged || mailbox->chunks < maximumPendingChunks) && (!pullOutput || (length <= maximumPullBytes - pendingBytes && (merged || pendingChunks < maximumPullChunks))));
                });

                if (mailbox->closed) {
                    return;
                }

                auto& chunks = mailbox->pending[identity].chunks;

                if (!chunks.empty() && chunks.back().first == stream) {
                    chunks.back().second.append(bytes, offset, length);
                } else {
                    chunks.emplace_back(stream, bytes.substr(offset, length));
                    ++mailbox->chunks;
                }

                mailbox->bytes += length;
                mailbox->pending[identity].bytes += length;
                offset += length;
                posted();
            }
        },
        [mailbox, identity, posted](process::ProcessEnd end) {
            const std::lock_guard lock(mailbox->mutex);

            if (mailbox->closed) {
                return;
            }

            mailbox->pending[identity].exit = end;
            posted();
        },
    };
    // clang-format on

    std::int64_t columns = 80;
    std::int64_t rows = 24;

    if (terminal != nullptr) {
        json::ObjectReader dimensions(*terminal, "process.start.terminal");
        dimensions.readInteger("columns", columns, 1, 10000).readInteger("rows", rows, 1, 10000);

        if (const auto valid = dimensions.finish(); !valid.hasValue()) {
            return HostReply::failure(valid.error());
        }

        if (inputGiven) {
            return HostReply::failure({"terminal_launch_invalid", "A terminal program keeps its input open for interactive writes", ""});
        }
    }

    auto started = terminal != nullptr ? process::TerminalProcess::start(m_services.terminals, launch, static_cast<int>(columns), static_cast<int>(rows), std::move(events)) : m_services.processes.start(launch, std::move(events));

    if (!started.hasValue()) {
        return HostReply::failure(started.error());
    }

    m_running.emplace(identity, Running{plugin, std::move(started.value()), {}, {}, binaryOutput || pullOutput, pullOutput, std::nullopt, false, false});

    return HostReply::success({{"process", identity}});
}

nlohmann::json ProcessHost::write(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t identity = 0;
    std::string content;
    json::ObjectReader reader(argument, "process.write");
    reader.readText("plugin", plugin).readInteger("process", identity, 1, largestRequest).read("content", content);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    auto running = owned(plugin, identity);

    if (!running.hasValue()) {
        return HostReply::failure(running.error());
    }

    const auto decoded = decodeInput(content);

    if (!decoded.hasValue()) {
        return HostReply::failure(decoded.error());
    }

    if (running.value()->retiring || !running.value()->process->write(decoded.value())) {
        return HostReply::failure({"process_input_refused", "The program has ended or has not read what it was already sent", std::to_string(identity)});
    }

    return HostReply::success();
}

nlohmann::json ProcessHost::endInput(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t identity = 0;
    json::ObjectReader reader(argument, "process.end-input");
    reader.readText("plugin", plugin).readInteger("process", identity, 1, largestRequest);

    if (const auto valid = reader.finish(); !valid.hasValue()) {
        return HostReply::failure(valid.error());
    }

    auto running = owned(plugin, identity);

    if (!running.hasValue()) {
        return HostReply::failure(running.error());
    }

    if (running.value()->retiring || !running.value()->process->endInput()) {
        return HostReply::failure({"process_input_refused", "The program input cannot be ended", std::to_string(identity)});
    }

    return HostReply::success();
}

nlohmann::json ProcessHost::resize(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t identity = 0;
    std::int64_t columns = 0;
    std::int64_t rows = 0;
    json::ObjectReader reader(argument, "process.resize");
    reader.readText("plugin", plugin).readInteger("process", identity, 1, largestRequest).readInteger("columns", columns, 1, 10000).readInteger("rows", rows, 1, 10000);

    if (const auto valid = reader.finish(); !valid.hasValue()) {
        return HostReply::failure(valid.error());
    }

    auto running = owned(plugin, identity);

    if (!running.hasValue()) {
        return HostReply::failure(running.error());
    }

    if (!running.value()->process->resize(static_cast<int>(columns), static_cast<int>(rows))) {
        return HostReply::failure({"process_not_resizable", "The program has no running terminal to resize", ""});
    }

    return HostReply::success();
}

nlohmann::json ProcessHost::read(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t identity = 0;
    std::int64_t request = 0;
    std::int64_t maximum = 0;
    json::ObjectReader reader(argument, "process.read");
    reader.readText("plugin", plugin).readInteger("process", identity, 1, largestRequest).readInteger("request", request, 1, largestRequest).readInteger("maximum", maximum, 1, static_cast<std::int64_t>(maximumReadBytes));

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    auto running = owned(plugin, identity);

    if (!running.hasValue()) {
        return HostReply::failure(running.error());
    }

    if (!running.value()->pullOutput || running.value()->retiring) {
        return HostReply::failure({"process_not_readable", "The program does not accept output reads", ""});
    }

    if (running.value()->reader.has_value()) {
        return HostReply::failure({"process_read_pending", "The program already has an accepted output reader", ""});
    }

    running.value()->reader = Waiting{request, static_cast<std::size_t>(maximum)};
    deliver();

    return HostReply::success();
}

Result<std::string> ProcessHost::decodeInput(std::string_view content) {
    if (content.size() > ((process::ProcessLaunch::maximumInputBytes + 2) / 3) * 4) {
        return Result<std::string>::failure({"process_input_too_large", "The program input exceeds its byte bound", ""});
    }

    const auto bytes = text::Base64Helper::decode(content);

    if (!bytes.has_value()) {
        return Result<std::string>::failure({"process_input_invalid", "The program input is not valid Base64", ""});
    }

    if (bytes->size() > process::ProcessLaunch::maximumInputBytes) {
        return Result<std::string>::failure({"process_input_too_large", "The program input exceeds its byte bound", ""});
    }

    return Result<std::string>::success(std::string(bytes->begin(), bytes->end()));
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

    running.value()->stopping = running.value()->pullOutput;
    running.value()->process->stop();
    deliver();

    return HostReply::success();
}

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

            if (running.reader.has_value()) {
                m_replies.reply(running.reader->request, Result<nlohmann::json>::failure({"plugin_not_running", "The plugin owning the program was withdrawn", ""}));
                running.reader.reset();
            }

            running.process->stop();
        }
    }

    deliver();

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

    if (name.size() > maximumPath || name.find('\0') != std::string::npos || !text::Utf8Helper::valid(name) || directories.size() > maximumFindDirectories) {
        return HostReply::failure({"process_find_invalid", "The executable name or search directories are invalid", ""});
    }

    for (const auto& directory : directories) {
        if (directory.size() > maximumPath || directory.find('\0') != std::string::npos || !text::Utf8Helper::valid(directory)) {
            return HostReply::failure({"process_find_invalid", "The executable name or search directories are invalid", ""});
        }

        searched.emplace_back(std::u8string(directory.begin(), directory.end()));
    }

    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, request, name, searched, launcher = &m_services.processes, mainThread = &m_services.mainThread]() {
        const auto found = launcher->find(name, searched);
        mainThread->post([this, alive, request, found]() {
            if (!alive.expired()) {
                m_replies.reply(request, Result<nlohmann::json>::success(found.has_value() ? nlohmann::json{{"found", true}, {"path", platform::PathTextHelper::generic(*found)}} : nlohmann::json{{"found", false}}));
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

        for (auto current = m_mailbox->pending.begin(); current != m_mailbox->pending.end();) {
            const auto running = m_running.find(current->first);
            const bool pull = running != m_running.end() && running->second.pullOutput && !running->second.retiring;

            if (pull && !running->second.reader.has_value() && !running->second.stopping) {
                ++current;
                continue;
            }

            auto& source = current->second;
            auto& taken = pending[current->first];
            std::size_t remaining = pull && !running->second.stopping ? running->second.reader->maximum : source.bytes;

            while (!source.chunks.empty() && remaining > 0) {
                auto& chunk = source.chunks.front();
                const std::size_t count = std::min(remaining, chunk.second.size() - source.offset);
                taken.bytes += count;
                remaining -= count;

                if (count == chunk.second.size() - source.offset) {
                    source.bytes -= chunk.second.size();
                    m_mailbox->bytes -= chunk.second.size();

                    if (source.offset == 0) {
                        taken.chunks.push_back(std::move(chunk));
                    } else {
                        taken.chunks.emplace_back(chunk.first, chunk.second.substr(source.offset, count));
                    }

                    source.chunks.pop_front();
                    source.offset = 0;
                    --m_mailbox->chunks;
                } else {
                    taken.chunks.emplace_back(chunk.first, chunk.second.substr(source.offset, count));
                    source.offset += count;
                }
            }

            if (source.chunks.empty()) {
                taken.exit = source.exit;
                current = m_mailbox->pending.erase(current);
            } else {
                ++current;
            }
        }

        m_mailbox->posted = false;
    }

    m_mailbox->ready.notify_all();

    for (auto& [identity, arrived] : pending) {
        const auto found = m_running.find(identity);

        if (found == m_running.end()) {
            continue;
        }

        Running& running = found->second;

        if (running.stopping && !arrived.exit.has_value()) {
            continue;
        }

        nlohmann::json chunks = nlohmann::json::array();

        for (auto& [stream, bytes] : arrived.chunks) {
            if (running.stopping) {
                break;
            }

            if (running.binaryOutput) {
                chunks.push_back({{"stream", streamName(stream)}, {"content", text::Base64Helper::encode(bytes)}});
                continue;
            }

            std::string text = (stream == process::ProcessStream::Output ? running.output : running.error).take(bytes);

            if (!text.empty()) {
                chunks.push_back({{"stream", streamName(stream)}, {"text", std::move(text)}});
            }
        }

        if (arrived.exit.has_value() && !running.binaryOutput) {
            for (const auto stream : {process::ProcessStream::Output, process::ProcessStream::Error}) {
                if (std::string rest = (stream == process::ProcessStream::Output ? running.output : running.error).finish(); !rest.empty()) {
                    chunks.push_back({{"stream", streamName(stream)}, {"text", std::move(rest)}});
                }
            }
        }

        if (running.pullOutput && !running.retiring && running.reader.has_value()) {
            nlohmann::json value{{"chunks", std::move(chunks)}, {"ended", arrived.exit.has_value()}};

            if (arrived.exit.has_value()) {
                value["code"] = arrived.exit->code;
                value["failed"] = arrived.exit->crashed || arrived.exit->code != 0;
            }

            const auto request = running.reader->request;
            running.reader.reset();
            m_replies.reply(request, Result<nlohmann::json>::success(std::move(value)));
        } else if (!chunks.empty() && !running.retiring) {
            m_runtime.emit("workpane.process.output", {{"plugin", running.owner}, {"process", identity}, {"chunks", std::move(chunks)}});
        }

        if (!arrived.exit.has_value()) {
            continue;
        }

        if (!running.retiring) {
            m_runtime.emit("workpane.process.exit", {{"plugin", running.owner}, {"process", identity}, {"code", arrived.exit->code}, {"failed", arrived.exit->crashed || arrived.exit->code != 0}});
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
