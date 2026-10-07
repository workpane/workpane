#include "support/LocalPort.h"
#include "support/Resources.h"
#include "support/ScriptHarness.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Loads the Lua modules of the AI plugin by themselves and checks the rules they carry, which the product tests reach only through a few cases.
class AiModuleTest : public ::testing::Test {
  protected:
    // Each body runs in a task of an environment that answers include with the other modules of the plugin, reads the catalog from the staged assets and names texts by their keys.
    [[nodiscard]] static json run(std::string_view body) {
        const std::string directory = (Resources::staged() / "plugins" / "ai").generic_string();
        const std::string prelude = R"(
            local directory = [==[)" +
                                    directory + R"(]==]
            local loaded = {}
            local environment = setmetatable({}, { __index = _G })
            local function translate(key, ...)
                local arguments = { ... }
                return #arguments == 0 and key or (key .. "[" .. table.concat(arguments, ",") .. "]")
            end
            local function defaultOf(rule)
                if rule.default ~= nil then
                    return rule.default
                end

                local value = {}

                for name, field in pairs(rule.fields or {}) do
                    value[name] = defaultOf(field)
                end

                return value
            end

            local function define(schema)
                local values = {}

                for key, rule in pairs(schema) do
                    values[key] = defaultOf(rule)
                end

                local function update(_, changes)
                    for key, value in pairs(changes) do
                        values[key] = value
                    end
                end

                return { get = function(_, key) return values[key] end, set = function(_, key, value) values[key] = value end, update = update, watch = function() return function() end end }
            end

            local function run(work, ...)
                require("workpane.task").run("test", "task", work, ...)
            end

            -- A fixed zone three hours behind UTC and a zone that moves one hour ahead from the end of March to the end of October of 2026.
            local zones = {
                ["Test/Fixed"] = function() return -10800 end,
                ["Test/Summer"] = function(seconds) return (seconds >= 1774746000 and seconds < 1792890000) and 3600 or 0 end,
            }

            local function offset(zone, seconds)
                if zones[zone] == nil then
                    error({ code = "time_zone_unknown", message = "The time zone is not known to the system", detail = zone }, 0)
                end

                return zones[zone](seconds)
            end

            environment.workpane = { ui = {}, task = run, plugin = { directory = directory }, preferences = { define = define }, time = { zone = function() return "Test/Fixed" end, offset = offset }, i18n = { text = translate, number = function(value) return value end, translate = translate } }
            local paths = require("workpane.paths")
            local system = require("platform").os()
            environment.workpane.files = { uri = paths.uri, path = function(uri) return paths.path(system, uri) end, absolute = function(path) return paths.absolute(system, path) end }

            function environment.include(name)
                if loaded[name] == nil then
                    loaded[name] = assert(loadfile(directory .. "/" .. name .. ".lua", "t", environment))()
                end

                return loaded[name]
            end

            local include = environment.include
            require("workpane.task").run("test", "test", function()
        )";
        ScriptHarness harness;
        const auto ran = harness.run(prelude + std::string(body) + "\nend)\n");
        EXPECT_TRUE(ran.hasValue()) << (ran.hasValue() ? "" : ran.error().message);
        // clang-format off
        EXPECT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty() || !harness.logged.empty(); }));
        // clang-format on
        EXPECT_TRUE(harness.logged.empty()) << (harness.logged.empty() ? "" : harness.logged.front().dump());

        return harness.results.size() == 1 ? harness.results.front() : json();
    }
};

// Empty lists, empty objects and nulls keep their shape through the codec, keys are written in order and broken text is refused with its position.
TEST_F(AiModuleTest, ReadsAndWritesJsonWithoutLosingItsShape) {
    const json results = run(R"(
        local codec = include("codec")
        local decoded = codec.decode('{"list":[],"object":{},"none":null,"number":1.5,"text":"café 😀","nested":[1,[2,{"a":true}]]}', true)
        local refused = select(2, pcall(codec.decode, '{"open":'))
        host.test_result({
            encoded = codec.encode(decoded),
            list = codec.isList(decoded.list),
            null = decoded.none == codec.null,
            text = decoded.text,
            empty = codec.encode({ list = codec.list(), object = {} }),
            broken = codec.encode({ text = "a\xffb" }),
            refused = refused.code,
            read = codec.read("not json") == nil,
            pretty = codec.pretty({ b = 1, a = codec.list({ "x" }) }),
        })
    )");

    EXPECT_EQ(results["encoded"], R"({"list":[],"nested":[1,[2,{"a":true}]],"none":null,"number":1.5,"object":{},"text":"café 😀"})");
    EXPECT_EQ(results["list"], true);
    EXPECT_EQ(results["null"], true);
    EXPECT_EQ(results["text"], "café 😀");
    EXPECT_EQ(results["empty"], R"({"list":[],"object":{}})");
    EXPECT_EQ(results["broken"], "{\"text\":\"a\xEF\xBF\xBD"
                                 "b\"}");
    EXPECT_EQ(results["refused"], "ai_json_invalid");
    EXPECT_EQ(results["read"], true);
    EXPECT_EQ(results["pretty"], "{\n  \"a\": [\n    \"x\"\n  ],\n  \"b\": 1\n}");
}

// Five fields are read with stars, ranges, lists and Sunday as seven, the next occurrence follows the wall clock of the zone of the schedule and a broken field names what it breaks.
TEST_F(AiModuleTest, FindsTheNextOccurrenceOfACronExpression) {
    const json results = run(R"(
        local cron = include("cron")
        local start = cron.epoch("2026-03-06T20:50:00.000Z")
        local function next(expression, after)
            local moment = cron.nextAfter(cron.parse(expression), after, "Test/Fixed")
            return os.date("!%Y-%m-%d %H:%M %w", moment - 10800)
        end
        local function summer(expression)
            return cron.stamp(cron.nextAfter(cron.parse(expression), start, "Test/Summer"))
        end
        local function refused(expression)
            local _, failure = pcall(cron.parse, expression)
            return failure.code
        end

        host.test_result({
            weekdays = next("* 9-17 * * 1-5", start),
            sunday = next("0 8 * * 7", start),
            either = next("30 6 1 * 1", start),
            quarter = next("15,45 * * * *", start),
            codes = { refused("* * * *"), refused("60 * * * *"), refused("5-1 * * * *"), refused("1,,2 * * * *") },
            skipped = summer("30 1 29 3 *"),
            repeated = summer("30 1 25 10 *"),
            other = cron.stamp(cron.nextAfter(cron.parse("0 9 * * *"), start, "Test/Summer")),
            unknown = select(2, pcall(cron.nextAfter, cron.parse("0 9 * * *"), start, "Test/Missing")).code,
            instant = cron.stamp(cron.localMoment("2026-03-07 08:05", "Test/Fixed")),
            moment = cron.localText(cron.localMoment("2026-03-07 08:05", "Test/Fixed"), "Test/Fixed"),
            stamp = cron.stamp(cron.epoch("2026-03-07T08:05:09.000Z")),
            invalid = cron.localMoment("2026-03-07", "Test/Fixed") == nil,
        })
    )");

    EXPECT_EQ(results["weekdays"], "2026-03-06 17:51 5");
    EXPECT_EQ(results["sunday"], "2026-03-08 08:00 0");
    EXPECT_EQ(results["either"], "2026-03-09 06:30 1");
    EXPECT_EQ(results["quarter"], "2026-03-06 18:15 5");
    EXPECT_EQ(results["codes"], json::array({"ai_tasks_cron_field_count_invalid", "ai_tasks_cron_value_invalid", "ai_tasks_cron_range_invalid", "ai_tasks_cron_field_invalid"}));
    EXPECT_EQ(results["skipped"], "2026-03-29T01:00:00.000Z");
    EXPECT_EQ(results["repeated"], "2026-10-25T00:30:00.000Z");
    EXPECT_EQ(results["other"], "2026-03-07T09:00:00.000Z");
    EXPECT_EQ(results["unknown"], "time_zone_unknown");
    EXPECT_EQ(results["instant"], "2026-03-07T11:05:00.000Z");
    EXPECT_EQ(results["moment"], "2026-03-07 08:05");
    EXPECT_EQ(results["stamp"], "2026-03-07T08:05:09.000Z");
    EXPECT_EQ(results["invalid"], true);
}

// Tool calls arrive in fragments in both protocols and are joined once the answer ends, and arguments that cannot be read are kept for the model to see.
TEST_F(AiModuleTest, JoinsToolCallsStreamedInFragments) {
    const json results = run(R"(
        local protocols = include("protocols")
        local openai = protocols.accumulator("openai-compatible")
        openai:consume({ choices = { { delta = { tool_calls = { { index = 1, id = "b", ["function"] = { name = "list_directory", arguments = '{"pa' } } } } } } })
        openai:consume({ choices = { { delta = { tool_calls = { { index = 0, id = "a", ["function"] = { name = "read_file", arguments = "not json" } } } } } } })
        openai:consume({ choices = { { delta = { tool_calls = { { index = 1, ["function"] = { arguments = 'th":"src"}' } } } } } } })
        local calls = openai:calls()

        local anthropic = protocols.accumulator("anthropic")
        anthropic:consume({ type = "content_block_start", index = 1, content_block = { type = "tool_use", id = "t1", name = "describe_task" } })
        anthropic:consume({ type = "content_block_delta", index = 1, delta = { type = "input_json_delta", partial_json = "" } })
        local answered = anthropic:calls()

        local incomplete = protocols.accumulator("openai-compatible")
        incomplete:consume({ choices = { { delta = { tool_calls = { { index = 0, ["function"] = { arguments = "{}" } } } } } } })
        local _, failure = incomplete:calls()

        host.test_result({
            first = { calls[1].id, calls[1].name, calls[1].unreadable },
            second = { calls[2].id, calls[2].name, calls[2].arguments.path },
            anthropic = { answered[1].id, answered[1].name, next(answered[1].arguments) == nil },
            incomplete = failure.code,
        })
    )");

    EXPECT_EQ(results["first"], json::array({"a", "read_file", "not json"}));
    EXPECT_EQ(results["second"], json::array({"b", "list_directory", "src"}));
    EXPECT_EQ(results["anthropic"], json::array({"t1", "describe_task", true}));
    EXPECT_EQ(results["incomplete"], "ai_tool_call_invalid");
}

// The window keeps room for the answer, a conversation too long for it loses its oldest large results first, and a result of wide characters shorter than what pruning keeps stays whole.
// A tool result that is not text becomes UTF-8 before it is bounded, so it never reaches a model whole.
TEST_F(AiModuleTest, FitsAConversationToTheWindowOfTheModel) {
    const json results = run(R"(
        local window = include("window")
        local long = string.rep("x", 40000)
        local messages = include("messages").normalize({
            { role = "system", content = "rules" },
            { role = "user", content = "start" },
            { role = "assistant", toolCalls = { { id = "1", name = "read_file", arguments = {} } } },
            { role = "tool", toolCallId = "1", content = { { type = "text", text = long }, { type = "image", mediaType = "image/png", data = "png" } } },
            { role = "user", content = "continue" },
        })

        local before = window.estimate(messages)
        local pruned = window.prune(messages, 2000)
        local wide = string.rep("\u{4E2D}", 3000)
        local multibyte = include("messages").normalize({
            { role = "user", content = "start" },
            { role = "assistant", toolCalls = { { id = "1", name = "read_file", arguments = {} } } },
            { role = "tool", toolCallId = "1", content = wide },
            { role = "user", content = "continue" },
        })
        window.prune(multibyte, 10)
        local tools = include("tools")
        local binary = tools.bounded("\xFF\xFEtext")
        local long = tools.bounded("\xFF" .. string.rep("a", 100000))

        host.test_result({
            wide = multibyte[3].content[1].text == wide,
            binary = binary,
            longValid = utf8.len(long) ~= nil,
            longShortened = #long < 100000,
            unbounded = window.limit(0, 100) == nil,
            limit = window.limit(10000, 2000),
            exhausted = window.limit(1000, 2000),
            shrank = window.estimate(messages) < before,
            pruned = pruned > 0,
            kept = messages[5].content[1].text,
            image = messages[4].content[2].type,
        })
    )");

    EXPECT_EQ(results["unbounded"], true);
    EXPECT_GT(results["limit"].get<int>(), 0);
    EXPECT_LT(results["limit"].get<int>(), 8000);
    EXPECT_EQ(results["exhausted"], 0);
    EXPECT_EQ(results["shrank"], true);
    EXPECT_EQ(results["pruned"], true);
    EXPECT_EQ(results["kept"], "continue");
    EXPECT_EQ(results["image"], "image");
    EXPECT_EQ(results["wide"], true);
    EXPECT_EQ(results["binary"], "\xEF\xBF\xBD\xEF\xBF\xBDtext");
    EXPECT_EQ(results["longValid"], true);
    EXPECT_EQ(results["longShortened"], true);
}

// A connection is checked against the catalog: its provider, model, key, address and every parameter the model declares.
TEST_F(AiModuleTest, ChecksConnectionsAgainstTheCatalog) {
    const json results = run(R"(
        local catalog = include("catalog")
        local connections = include("connections")
        catalog.load()
        local provider = catalog.provider("ollama")
        local valid = connections.declared(provider, "llama3")
        valid.address = "http://127.0.0.1:1/v1"

        local function refused(change)
            local candidate = {}

            for key, value in pairs(valid) do
                candidate[key] = value
            end

            change(candidate)
            local _, failure = pcall(connections.validate, candidate)
            return failure.code
        end

        local anthropic = connections.declared(catalog.provider("anthropic"), "")

        host.test_result({
            key = connections.key(connections.validate(valid)),
            label = connections.label({ providerId = "ollama", modelId = "llama3", displayName = "  " }),
            reference = anthropic.apiKey,
            codes = {
                refused(function(candidate) candidate.providerId = "unknown" end),
                refused(function(candidate) candidate.modelId = " " end),
                refused(function(candidate) candidate.address = "ftp://host" end),
                refused(function(candidate) candidate.parameters = { temperature = 1 } end),
                refused(function(candidate) candidate.parameters = setmetatable({ unknown = 1 }, { __index = valid.parameters }) end),
                refused(function(candidate) candidate.extraParameters = "{" end),
            },
            endpoint = connections.endpoint("ollama", "http://host/v1", "chat"),
            speech = connections.endpoint("ollama", "", "speech") == nil,
            answering = #catalog.answering("speech") > 0,
        })
    )");

    EXPECT_EQ(results["key"], "ollama/llama3");
    EXPECT_EQ(results["label"], "ollama/llama3");
    EXPECT_EQ(results["reference"], "{env.ANTHROPIC_API_KEY}");
    EXPECT_EQ(results["codes"], json::array({"ai_provider_unknown", "ai_model_invalid", "ai_address_invalid", "ai_parameter_missing", "ai_parameter_unknown", "ai_extra_parameter_invalid"}));
    EXPECT_EQ(results["endpoint"], "http://host/v1/chat/completions");
    EXPECT_EQ(results["speech"], true);
    EXPECT_EQ(results["answering"], true);
}

// Each protocol receives its own body: the instructions apart for Anthropic, usage asked for from the others, parameters at their fields and extras merged last.
TEST_F(AiModuleTest, ShapesTheRequestOfEachProtocol) {
    const json results = run(R"(
        local catalog = include("catalog")
        local chat = include("chat")
        local connections = include("connections")
        local codec = include("codec")
        catalog.load()
        local anthropic = catalog.provider("anthropic")
        local ollama = catalog.provider("ollama")
        local first = connections.declared(anthropic, "claude-sonnet-5")
        local second = connections.declared(ollama, "llama3")
        second.extraParameters = '{"options.num_ctx":8192,"temperature":null}'
        local messages = include("messages").normalize({ { role = "system", content = "rules" }, { role = "user", content = "hello" } })

        host.test_result({
            anthropic = codec.encode(chat.body(anthropic, { connection = first, messages = messages }, function(key) return key end)),
            ollama = codec.encode(chat.body(ollama, { connection = second, messages = messages }, function(key) return key end)),
            message = chat.providerMessage('{"error":{"type":"rate_limit","message":"Slow down"}}'),
            plain = chat.providerMessage("  Service   unavailable "),
            retry = { chat.retryDelay("3", 0), chat.retryable(429), chat.retryable(400), chat.retryable(nil) },
        })
    )");

    const json anthropic = json::parse(results["anthropic"].get<std::string>());
    EXPECT_EQ(anthropic["system"], "rules");
    EXPECT_EQ(anthropic["messages"].size(), 1U);
    EXPECT_FALSE(anthropic.contains("stream_options"));
    EXPECT_TRUE(anthropic["max_tokens"].is_number_integer());
    const json ollama = json::parse(results["ollama"].get<std::string>());
    EXPECT_EQ(ollama["messages"].size(), 2U);
    EXPECT_EQ(ollama["stream_options"]["include_usage"], true);
    EXPECT_EQ(ollama["options"]["num_ctx"], 8192);
    EXPECT_FALSE(ollama.contains("temperature"));
    EXPECT_EQ(results["message"], "Slow down (type \"rate_limit\")");
    EXPECT_EQ(results["plain"], "Service unavailable");
    EXPECT_EQ(results["retry"], json::array({3000, true, false, true}));
}

// A conversation is refused by the first rule it breaks, named by its code and place, and fitted to a model by notes for what the model cannot read.
TEST_F(AiModuleTest, ChecksAConversationAndFitsItToAModel) {
    const json results = run(R"(
        local messages = include("messages")
        local function translate(key, ...) return table.concat({ key, ... }, ":") end
        local function refusal(list)
            local _, failure = pcall(messages.normalize, list)
            return failure.code .. "@" .. failure.detail
        end

        local refusals = {
            refusal({}),
            refusal({ { role = "system", content = "rules" } }),
            refusal({ { role = "robot", content = "hi" } }),
            refusal({ { role = "user", content = "  " } }),
            refusal({ { role = "user", content = { { type = "video", data = "x" } } } }),
            refusal({ { role = "assistant", content = { { type = "image", mediaType = "image/png", data = "x" } } } }),
            refusal({ { role = "user", content = { { type = "image", data = "x" } } } }),
            refusal({ { role = "user", content = { { type = "image", mediaType = "image/png", data = "x", url = "https://host/a.png" } } } }),
            refusal({ { role = "user", content = { { type = "document", mediaType = "text/plain", name = "a.txt", data = "\xff" } } } }),
            refusal({ { role = "user", content = { { type = "text", text = "hi", color = "red" } } } }),
            refusal({ { role = "user", content = "hi" }, { role = "tool", toolCallId = "1", content = "late" } }),
            refusal({ { role = "user", content = "hi" }, { role = "assistant", toolCalls = { { id = "1", name = "a", arguments = {} } } }, { role = "user", content = "again" } }),
            refusal({ { role = "user", content = "hi" }, { role = "assistant", toolCalls = { { id = "1", name = "a", arguments = {} }, { id = "1", name = "b", arguments = {} } } } }),
            refusal({ { role = "user", content = "hi" }, { role = "assistant", toolCalls = { { id = "1", name = "a", arguments = {} } } } }),
        }

        local conversation = messages.normalize({
            { role = "system", content = "rules" },
            { role = "user", content = { { type = "text", text = "look" }, { type = "image", mediaType = "image/png", data = "png" }, { type = "document", mediaType = "application/pdf", name = "a.pdf", data = "%PDF /Type /Page /Type /Pages /Type /Page" }, { type = "document", mediaType = "text/markdown", name = "notes.md", data = "# Notes" }, { type = "audio", format = "wav", data = "wav" } } },
            { role = "assistant", content = { { type = "reasoning", text = "think", signature = "s" }, { type = "text", text = "reading" } }, toolCalls = { { id = "1", name = "read_file", arguments = { path = "a" } } } },
            { role = "tool", toolCallId = "1", failed = true, content = "missing" },
        })

        local plain, plainAdjustments = messages.fit(conversation, {}, translate)
        local rich, richAdjustments = messages.fit(conversation, { vision = true, pdf = true, audio = true, ["system-prompt"] = true, ["function-calling"] = true }, translate)
        local texts = {}

        for index, part in ipairs(plain[2].content) do
            texts[index] = part.text
        end

        host.test_result({
            refusals = refusals,
            plain = texts,
            plainRoles = { plain[1].role, plain[3].role, plain[4].role },
            called = plain[3].content[3].text,
            answered = plain[4].content[1].text,
            plainAdjustments = plainAdjustments,
            rich = { rich[2].content[2].type, rich[2].content[3].type, rich[2].content[4].text, rich[2].content[5].type },
            richAdjustments = #richAdjustments,
            pages = messages.tokens({ content = { { type = "document", mediaType = "application/pdf", name = "a.pdf", data = "/Type /Page /Type /Page" } } }),
            image = messages.tokens({ content = { { type = "image", mediaType = "image/png", data = "x" } } }),
            described = messages.describe(conversation[2], translate),
        })
    )");

    EXPECT_EQ(results["refusals"], json::array({"ai_conversation_empty@messages", "ai_conversation_empty@messages", "ai_message_role_invalid@messages[1].role", "ai_message_content_invalid@messages[1].content", "ai_message_part_invalid@messages[1].content[1].type", "ai_message_part_invalid@messages[1].content[1].type", "ai_message_part_invalid@messages[1].content[1].mediaType", "ai_message_part_invalid@messages[1].content[1]", "ai_message_part_invalid@messages[1].content[1].data", "ai_message_part_invalid@messages[1].content[1].color", "ai_message_tool_result_invalid@messages[2].toolCallId", "ai_message_tool_result_invalid@messages[3]", "ai_message_tool_call_invalid@messages[2].toolCalls[2]", "ai_message_tool_result_invalid@messages"}));
    EXPECT_EQ(results["plain"], json::array({"look", "ai.message.image-omitted", "ai.message.document-omitted:a.pdf", "ai.message.document-text:notes.md\n\n# Notes", "ai.message.audio-omitted"}));
    EXPECT_EQ(results["plainRoles"], json::array({"user", "assistant", "user"}));
    EXPECT_EQ(results["called"], "ai.message.tool-call:read_file:{\"path\":\"a\"}");
    EXPECT_EQ(results["answered"], "ai.message.tool-failed:read_file");
    EXPECT_EQ(results["plainAdjustments"], json::array({"system-as-user", "image-omitted", "document-omitted", "audio-omitted", "tools-as-text"}));
    EXPECT_EQ(results["rich"], json::array({"image", "document", "ai.message.document-text:notes.md\n\n# Notes", "audio"}));
    EXPECT_EQ(results["richAdjustments"], 0);
    EXPECT_EQ(results["pages"], 4 + 2 * 3000);
    EXPECT_EQ(results["image"], 4 + 1600);
    EXPECT_EQ(results["described"], "look\nai.message.image-described\nai.message.document-described:a.pdf\nai.message.document-described:notes.md\nai.message.audio-described");
}

// Each protocol writes the canonical conversation its own way: parts, calls, results with their images, the reasoning a model signed and the reason an answer ended.
TEST_F(AiModuleTest, WritesTheConversationInTheShapeOfEachProtocol) {
    const json results = run(R"(
        local codec = include("codec")
        local messages = include("messages")
        local protocols = include("protocols")
        local function translate(key) return key end
        local conversation = messages.normalize({
            { role = "system", content = "rules" },
            { role = "user", content = { { type = "text", text = "look" }, { type = "image", mediaType = "image/png", data = "png" }, { type = "image", url = "https://host/a.png" }, { type = "document", mediaType = "application/pdf", name = "a.pdf", data = "pdf" } } },
            { role = "assistant", content = { { type = "reasoning", text = "think", signature = "s" }, { type = "reasoning", text = "", redacted = "opaque" }, { type = "reasoning", text = "unsigned" }, { type = "text", text = "reading" } }, toolCalls = { { id = "1", name = "view_image", arguments = { path = "a.png" } }, { id = "2", name = "read_file", arguments = {} } } },
            { role = "tool", toolCallId = "1", content = { { type = "text", text = "shown" }, { type = "image", mediaType = "image/jpeg", data = "jpg" } } },
            { role = "tool", toolCallId = "2", failed = true, content = "missing" },
            { role = "user", content = "and the audio" },
        })
        local listening = messages.normalize({ { role = "user", content = { { type = "audio", format = "mp3", data = "mp3" } } } })
        local opened = messages.normalize({ { role = "assistant", content = "earlier" }, { role = "user", content = "go on" } })

        host.test_result({
            openai = codec.encode(protocols.encode("openai-compatible", conversation, translate)),
            anthropic = codec.encode(protocols.encode("anthropic", conversation, translate)),
            audio = codec.encode(protocols.encode("openai-compatible", listening, translate)),
            refused = select(2, pcall(protocols.encode, "anthropic", listening, translate)).code,
            opened = codec.encode(protocols.encode("anthropic", opened, translate)),
            reasons = { protocols.finishReason("anthropic", "end_turn"), protocols.finishReason("anthropic", "max_tokens"), protocols.finishReason("anthropic", "tool_use"), protocols.finishReason("anthropic", "refusal"), protocols.finishReason("openai-compatible", "length"), protocols.finishReason("openai-compatible", "something"), protocols.finishReason("openai-compatible", "") },
        })
    )");

    const json openai = json::parse(results["openai"].get<std::string>())["messages"];
    ASSERT_EQ(openai.size(), 7U);
    EXPECT_EQ(openai[0], json({{"role", "system"}, {"content", "rules"}}));
    EXPECT_EQ(openai[1]["content"][1]["image_url"]["url"], "data:image/png;base64,cG5n");
    EXPECT_EQ(openai[1]["content"][2]["image_url"]["url"], "https://host/a.png");
    EXPECT_EQ(openai[1]["content"][3], json({{"type", "file"}, {"file", {{"filename", "a.pdf"}, {"file_data", "data:application/pdf;base64,cGRm"}}}}));
    EXPECT_EQ(openai[2]["content"], "reading");
    EXPECT_EQ(openai[2]["tool_calls"][0]["function"]["arguments"], "{\"path\":\"a.png\"}");
    EXPECT_EQ(openai[3], json({{"role", "tool"}, {"tool_call_id", "1"}, {"content", "shown"}}));
    EXPECT_EQ(openai[4], json({{"role", "tool"}, {"tool_call_id", "2"}, {"content", "missing"}}));
    EXPECT_EQ(openai[5]["content"][0]["text"], "ai.message.tool-images");
    EXPECT_EQ(openai[5]["content"][1]["image_url"]["url"], "data:image/jpeg;base64,anBn");
    EXPECT_EQ(openai[6], json({{"role", "user"}, {"content", "and the audio"}}));

    const json anthropic = json::parse(results["anthropic"].get<std::string>());
    const json turns = anthropic["messages"];
    EXPECT_EQ(anthropic["system"], "rules");
    ASSERT_EQ(turns.size(), 3U);
    EXPECT_EQ(turns[0]["content"][1]["source"], json({{"type", "base64"}, {"media_type", "image/png"}, {"data", "cG5n"}}));
    EXPECT_EQ(turns[0]["content"][2]["source"], json({{"type", "url"}, {"url", "https://host/a.png"}}));
    EXPECT_EQ(turns[0]["content"][3]["type"], "document");
    EXPECT_EQ(turns[1]["content"][0], json({{"type", "thinking"}, {"thinking", "think"}, {"signature", "s"}}));
    EXPECT_EQ(turns[1]["content"][1], json({{"type", "redacted_thinking"}, {"data", "opaque"}}));
    EXPECT_EQ(turns[1]["content"].size(), 5U);
    EXPECT_EQ(turns[1]["content"][3], json({{"type", "tool_use"}, {"id", "1"}, {"name", "view_image"}, {"input", {{"path", "a.png"}}}}));
    EXPECT_EQ(turns[1]["content"][4]["input"], json::object());
    EXPECT_EQ(turns[2]["content"][0]["content"][1]["type"], "image");
    EXPECT_EQ(turns[2]["content"][1]["is_error"], true);
    EXPECT_EQ(turns[2]["content"][2], json({{"type", "text"}, {"text", "and the audio"}}));

    EXPECT_EQ(json::parse(results["audio"].get<std::string>())["messages"][0]["content"][0], json({{"type", "input_audio"}, {"input_audio", {{"data", "bXAz"}, {"format", "mp3"}}}}));
    EXPECT_EQ(results["refused"], "ai_message_part_unsupported");
    EXPECT_EQ(json::parse(results["opened"].get<std::string>())["messages"][0]["content"][0]["text"], "ai.message.conversation-start");
    EXPECT_EQ(results["reasons"], json::array({"stop", "length", "tool_calls", "content_filter", "length", "stop", ""}));
}

// Colors, cursor movements and titles leave the output of a command, and a sequence split between two reads waits for its end.
TEST_F(AiModuleTest, KeepsThePlainTextOfCommandOutput) {
    const json results = run(R"(
        local commands = include("commands")
        local first, pending = commands.plain("", "\27[1;32mgreen\27[0m and \27]0;title\7plain\r\nnext\27[")
        local second, rest = commands.plain(pending, "2Kdone\rover")

        host.test_result({ first = first, pending = pending, second = second, rest = rest, message = commands.message({ code = "ai_command_timeout", message = "late" }, function(key) return key end), other = commands.message({ code = "other", message = "why" }, function(key) return key end) })
    )");

    EXPECT_EQ(results["first"], "green and plain\nnext");
    EXPECT_EQ(results["pending"], "\x1b[");
    EXPECT_EQ(results["second"], "done\nover");
    EXPECT_EQ(results["rest"], "");
    EXPECT_EQ(results["message"], "ai.error.command-timeout");
    EXPECT_EQ(results["other"], "why");
}

// A turn calling tools names the one it runs or how many run together, and says only that it calls its tools while none is listed.
TEST_F(AiModuleTest, NamesTheToolsATurnCalls) {
    const json results = run(R"(
        local running = {}
        loaded["engine"] = { phase = function() return "calling-tool" end, runningTools = function() return running end }
        local status = include("views/status")
        local none = status.phase("task")
        running = { "read_file" }
        local one = status.phase("task")
        running = { "read_file", "run_command" }
        host.test_result({ none = none, one = one, two = status.phase("task") })
    )");

    EXPECT_EQ(results["none"], "ai.phase.calling-tools");
    EXPECT_EQ(results["one"], "ai.phase.calling-tool[read_file]");
    EXPECT_EQ(results["two"], "ai.phase.calling-tool[ai.phase.tool-count[2]]");
}

// A command stopped while its folder was being checked never starts its program.
TEST_F(AiModuleTest, NeverStartsACommandStoppedBeforeItRan) {
    const json results = run(R"(
        local commands = include("commands")
        local stopped = commands.start({ program = "/bin/sh", arguments = { "-c", "true" }, workdir = directory, cancelled = function() return true end })
        local _, failure = pcall(stopped.await, stopped)
        host.test_result({ code = failure.code })
    )");

    EXPECT_EQ(results["code"], "ai_command_cancelled");
}

// A prompt answers every tag it declares, names the tags nobody declares and leaves the rest of the text alone.
TEST_F(AiModuleTest, AnswersTheTagsOfAPrompt) {
    const json results = run(R"(
        local prompt = include("prompt")
        host.test_result({
            rendered = prompt.render("Hi {{AGENT_NAME}} in {{TASK_WORKDIR}} {{UNKNOWN}} {single}", { AGENT_NAME = "Ada" }),
            unknown = prompt.unknownTags("{{MODEL}} {{NOPE}} {{NOPE}} {{ALSO}}"),
            key = prompt.descriptionKey("TASK_ISSUE_URL"),
            count = #prompt.tags(),
        })
    )");

    EXPECT_EQ(results["rendered"], "Hi Ada in  {{UNKNOWN}} {single}");
    EXPECT_EQ(results["unknown"], json::array({"NOPE", "ALSO"}));
    EXPECT_EQ(results["key"], "ai.tag.task-issue-url");
    EXPECT_EQ(results["count"], 25);
}

// Every template composes from its sections, opens with the identity of the agent and carries only the tags a prompt may name.
TEST_F(AiModuleTest, ComposesEveryTemplateFromItsSections) {
    const json results = run(R"(
        local prompt = include("prompt")
        local templates = include("templates")
        templates.load()
        local problems = {}
        for _, id in ipairs(templates.list()) do
            local body = templates.body(id)
            if body:sub(1, 30) ~= "You are {{AGENT_NAME}}. {{AGEN" or #prompt.unknownTags(body) > 0 or not body:find("{{SYSTEM_PROMPT_DATA}}", 1, true) then
                problems[#problems + 1] = id
            end
        end
        host.test_result({ count = #templates.list(), problems = problems, review = templates.body("code-review"):find("How you carry out the review", 1, true) ~= nil })
    )");

    EXPECT_EQ(results["count"], 27);
    EXPECT_TRUE(results["problems"].empty()) << results["problems"].dump();
    EXPECT_EQ(results["review"], true);
}

// The front matter of a skill reads the YAML its ecosystems write, after a byte order mark too, and a long run of spaces costs its length once.
TEST_F(AiModuleTest, ReadsTheFrontMatterTheEcosystemsWrite) {
    const json results = run(R"(
        local frontmatter = include("frontmatter")
        local marked, markedBody = frontmatter.read("\239\187\191---\r\nname: deploy\r\ndescription: \"Ships it\" # the site\r\n---\r\nBody")
        local empty, emptyBody = frontmatter.read("---\n---\nOnly body")
        local blocks = frontmatter.read("---\nfolded: >\n  one\n  two\nliteral: |-\n  first\n  second\ntags: [a, 'b c']\nlisted:\n  - x\n  - y\nmetadata:\n  owner: team\n---\n")
        local started = os.clock()
        local padded = frontmatter.read("---\nname: " .. string.rep(" ", 200000) .. "wide\n---\n")
        host.test_result({ name = marked.name, description = marked.description, body = markedBody, empty = next(empty) == nil, emptyBody = emptyBody, folded = blocks.folded, literal = blocks.literal, tags = frontmatter.text(blocks, "tags"), listed = blocks.listed, owner = blocks.metadata.owner, padded = padded.name, fast = os.clock() - started < 1 })
    )");

    EXPECT_EQ(results["name"], "deploy");
    EXPECT_EQ(results["description"], "Ships it");
    EXPECT_EQ(results["body"], "Body");
    EXPECT_EQ(results["empty"], true);
    EXPECT_EQ(results["emptyBody"], "Only body");
    EXPECT_EQ(results["folded"], "one two");
    EXPECT_EQ(results["literal"], "first\nsecond");
    EXPECT_EQ(results["tags"], "a b c");
    EXPECT_EQ(results["listed"], json::array({"x", "y"}));
    EXPECT_EQ(results["owner"], "team");
    EXPECT_EQ(results["padded"], "wide");
    EXPECT_EQ(results["fast"], true);
}

// A long line of an MCP server or of a provider stream is split in time proportional to its length, however many pieces it arrives in.
TEST_F(AiModuleTest, SplitsLongStreamedLinesInLinearTime) {
    httplib::Server server;
    const std::string text(600000, 'x');
    // clang-format off
    server.Post("/v1/messages", [&text](const httplib::Request&, httplib::Response& response) {
        const std::vector<json> events = {
            {{"type", "message_start"}, {"message", {{"usage", {{"input_tokens", 1}}}}}},
            {{"type", "content_block_start"}, {"index", 0}, {"content_block", {{"type", "text"}, {"text", ""}}}},
            {{"type", "content_block_delta"}, {"index", 0}, {"delta", {{"type", "text_delta"}, {"text", text}}}},
            {{"type", "message_delta"}, {"delta", {{"stop_reason", "end_turn"}}}, {"usage", {{"output_tokens", 1}}}},
            {{"type", "message_stop"}},
        };

        std::string stream;

        for (const auto& event : events) {
            stream += "event: " + event["type"].get<std::string>() + "\ndata: " + event.dump() + "\n\n";
        }

        response.set_content(stream, "text/event-stream");
    });
    // clang-format on

    const int port = LocalPort::bind(server);
    // clang-format off
    std::thread thread([&server]() { std::ignore = server.listen_after_bind(); });
    // clang-format on
    server.wait_until_ready();

    const auto started = std::chrono::steady_clock::now();
    const json results = run(R"(
        local catalog = include("catalog")
        local chat = include("chat")
        local connections = include("connections")
        local mcp = include("mcp")
        local preferences = include("preferences")
        catalog.load()
        preferences.define()

        local client = mcp.new({ id = "server" }, 1000, { toolsChanged = function() end })
        local received = {}
        client.dispatch = function(_, message) received[#received + 1] = message end
        local payload = '{"jsonrpc":"2.0","id":1,"result":{"text":"' .. string.rep("y", 1000000) .. '"}}'
        for offset = 1, #payload, 16384 do
            client:receive({ { stream = "output", text = payload:sub(offset, offset + 16383) } })
        end
        client:receive({ { stream = "output", text = "\n" } })

        local connection = connections.declared(catalog.provider("anthropic"), "claude-sonnet-5")
        connection.apiKey = "secret-key"
        local streamer = chat.new({ requestSent = function() end, content = function() end, throttled = function() end })
        local answer = streamer:send({ connection = connection, address = "http://127.0.0.1:)" +
                             std::to_string(port) + R"(", messages = include("messages").normalize({ { role = "user", content = "hi" } }), tools = {} }, function(key) return key end)

        host.test_result({ messages = #received, size = #received[1].result.text, content = #answer.content })
    )");
    const auto elapsed = std::chrono::steady_clock::now() - started;

    server.stop();
    thread.join();
    EXPECT_EQ(results["messages"], 1);
    EXPECT_EQ(results["size"], 1000000);
    EXPECT_EQ(results["content"], 600000);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

// An Anthropic answer streams its text, usage, reason and tool calls, a request the service throttles is sent again after the wait it names, and the key travels in its own header.
TEST_F(AiModuleTest, StreamsAnAnthropicAnswerAfterARetry) {
    httplib::Server server;
    std::vector<std::string> keys;
    int attempts = 0;
    // clang-format off
    server.Post("/v1/messages", [&](const httplib::Request& request, httplib::Response& response) {
        keys.push_back(request.get_header_value("x-api-key"));

        if (++attempts == 1) {
            response.status = 429;
            response.set_header("Retry-After", "0");
            response.set_content(R"({"type":"error","error":{"type":"rate_limit_error","message":"Slow down"}})", "application/json");
            return;
        }

        const std::vector<json> events = {
            {{"type", "message_start"}, {"message", {{"usage", {{"input_tokens", 11}}}}}},
            {{"type", "content_block_start"}, {"index", 0}, {"content_block", {{"type", "thinking"}, {"thinking", ""}}}},
            {{"type", "content_block_delta"}, {"index", 0}, {"delta", {{"type", "thinking_delta"}, {"thinking", "Look at "}}}},
            {{"type", "content_block_delta"}, {"index", 0}, {"delta", {{"type", "thinking_delta"}, {"thinking", "the folder."}}}},
            {{"type", "content_block_delta"}, {"index", 0}, {"delta", {{"type", "signature_delta"}, {"signature", "signed"}}}},
            {{"type", "content_block_start"}, {"index", 1}, {"content_block", {{"type", "redacted_thinking"}, {"data", "opaque"}}}},
            {{"type", "content_block_start"}, {"index", 2}, {"content_block", {{"type", "thinking"}, {"thinking", ""}}}},
            {{"type", "content_block_delta"}, {"index", 2}, {"delta", {{"type", "thinking_delta"}, {"thinking", "Then answer."}}}},
            {{"type", "content_block_delta"}, {"index", 2}, {"delta", {{"type", "signature_delta"}, {"signature", "again"}}}},
            {{"type", "content_block_start"}, {"index", 3}, {"content_block", {{"type", "text"}, {"text", ""}}}},
            {{"type", "content_block_delta"}, {"index", 3}, {"delta", {{"type", "text_delta"}, {"text", "Hel"}}}},
            {{"type", "content_block_delta"}, {"index", 3}, {"delta", {{"type", "text_delta"}, {"text", "lo"}}}},
            {{"type", "content_block_start"}, {"index", 4}, {"content_block", {{"type", "tool_use"}, {"id", "tool-1"}, {"name", "list_directory"}}}},
            {{"type", "content_block_delta"}, {"index", 4}, {"delta", {{"type", "input_json_delta"}, {"partial_json", "{\"path\":"}}}},
            {{"type", "content_block_delta"}, {"index", 4}, {"delta", {{"type", "input_json_delta"}, {"partial_json", "\"src\"}"}}}},
            {{"type", "message_delta"}, {"delta", {{"stop_reason", "tool_use"}}}, {"usage", {{"output_tokens", 7}}}},
            {{"type", "message_stop"}},
        };

        std::string stream;

        for (const auto& event : events) {
            stream += "event: " + event["type"].get<std::string>() + "\ndata: " + event.dump() + "\n\n";
        }

        response.set_content(stream, "text/event-stream");
    });
    // clang-format on

    const int port = LocalPort::bind(server);
    // clang-format off
    std::thread thread([&server]() { std::ignore = server.listen_after_bind(); });
    // clang-format on
    server.wait_until_ready();

    const json results = run(R"(
        local catalog = include("catalog")
        local chat = include("chat")
        local connections = include("connections")
        local preferences = include("preferences")
        catalog.load()
        preferences.define()
        local connection = connections.declared(catalog.provider("anthropic"), "claude-sonnet-5")
        connection.apiKey = "secret-key"
        local streamed = {}
        local throttles = {}
        local client = chat.new({ requestSent = function() end, content = function(delta) streamed[#streamed + 1] = delta end, throttled = function(reason, _, cause) throttles[#throttles + 1] = reason .. ":" .. tostring(cause) end })
        local answer = client:send({ connection = connection, address = "http://127.0.0.1:)" +
                             std::to_string(port) + R"(", messages = include("messages").normalize({ { role = "system", content = "rules" }, { role = "user", content = "hi" } }), tools = {} }, function(key) return key end)

        host.test_result({ content = answer.content, streamed = table.concat(streamed, "|"), input = answer.usage.input, output = answer.usage.output, finish = answer.finishReason, reasoning = answer.reasoning, call = { answer.calls[1].id, answer.calls[1].name, answer.calls[1].arguments.path }, throttles = throttles })
    )");

    server.stop();
    thread.join();
    EXPECT_EQ(results["content"], "Hello");
    EXPECT_EQ(results["streamed"], "Hel|lo");
    EXPECT_EQ(results["input"], 11);
    EXPECT_EQ(results["output"], 7);
    EXPECT_EQ(results["finish"], "tool_calls");
    EXPECT_EQ(results["reasoning"], json::array({{{"text", "Look at the folder."}, {"signature", "signed"}}, {{"text", ""}, {"redacted", "opaque"}}, {{"text", "Then answer."}, {"signature", "again"}}}));
    EXPECT_EQ(results["call"], json::array({"tool-1", "list_directory", "src"}));
    EXPECT_EQ(results["throttles"], json::array({"retry:Slow down (type \"rate_limit_error\")"}));
    EXPECT_EQ(keys, (std::vector<std::string>{"secret-key", "secret-key"}));
}

// A tool of a server reaches the client that server has when the call runs, so a server restarted during a run still answers, and its description is its own text rather than a key.
TEST_F(AiModuleTest, CallsTheServerThatAnswersWhenTheCallRuns) {
    const json results = run(R"(
        local catalog = include("catalog")
        local tools = include("tools")
        catalog.load()
        local function client(label)
            return { ready = true, descriptor = { id = "notes" }, tools = { { name = "lookup", description = "Look up a note", inputSchema = { type = "object", properties = {} }, readOnly = false } }, callTool = function() return { content = { { type = "text", text = label } } } end }
        end
        local earlier = client("earlier")
        local schemas = tools.schemas({ earlier })
        earlier.ready = false
        local current = { client("restarted") }
        local answered = tools.invoke(schemas, { id = "1", name = "mcp_notes_lookup", arguments = {} }, { servers = function() return current end, translate = function(key) return key end })
        host.test_result({ text = answered.text, failed = answered.failed, description = tools.find(schemas, "mcp_notes_lookup").description })
    )");

    EXPECT_EQ(results["text"], "restarted");
    EXPECT_EQ(results["failed"], false);
    EXPECT_EQ(results["description"], "Look up a note");
}

// A request an MCP server leaves unanswered fails once its deadline passes, and the client withdraws it at the server by its identifier.
TEST_F(AiModuleTest, WithdrawsAnMcpRequestThatOutlivesItsDeadline) {
    httplib::Server server;
    std::mutex mutex;
    std::vector<json> received;
    // clang-format off
    server.Post("/mcp", [&](const httplib::Request& request, httplib::Response& response) {
        const json message = json::parse(request.body);

        {
            const std::lock_guard lock(mutex);
            received.push_back(message);
        }

        const std::string method = message.value("method", "");

        if (method == "tools/call") {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }

        if (method == "initialize" || method == "tools/list") {
            const json result = method == "initialize" ? json{{"protocolVersion", "2025-06-18"}, {"capabilities", json::object()}} : json{{"tools", json::array()}};
            response.set_content(json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", result}}.dump(), "application/json");
            return;
        }

        response.status = 202;
    });
    // clang-format on

    const int port = LocalPort::bind(server);
    // clang-format off
    std::thread thread([&server]() { std::ignore = server.listen_after_bind(); });
    // clang-format on
    server.wait_until_ready();

    const json results = run(R"(
        local async = require("async")
        environment.workpane.app = { version = "test" }
        local mcp = include("mcp")
        local failures = {}
        local client = mcp.new({ id = "slow", transport = "http", command = "", arguments = {}, workdir = "", url = "http://127.0.0.1:)" +
                             std::to_string(port) + R"(/mcp", apiKey = "", roots = {}, samplingEnabled = false, samplingMaximumTokens = 0 }, 1000, { failed = function(failure) failures[#failures + 1] = failure.code end, toolsChanged = function() end, progress = function() end })
        client:start()

        while not client.ready do
            async.sleep(10):await()
        end

        local answered, failure = pcall(client.callTool, client, "wait", {}, 200)
        async.sleep(300):await()
        host.test_result({ answered = answered, code = failure.code, settled = next(client.pending) == nil, failures = failures })
        client:stop()
    )");

    server.stop();
    thread.join();
    EXPECT_EQ(results["answered"], false);
    EXPECT_EQ(results["code"], "ai_mcp_timeout");
    EXPECT_EQ(results["settled"], true);

    const std::lock_guard lock(mutex);
    // clang-format off
    const auto call = std::ranges::find_if(received, [](const json& message) { return message.value("method", "") == "tools/call"; });
    const auto cancelled = std::ranges::find_if(received, [](const json& message) { return message.value("method", "") == "notifications/cancelled"; });
    // clang-format on
    ASSERT_NE(call, received.end());
    ASSERT_NE(cancelled, received.end());
    EXPECT_EQ((*cancelled)["params"]["requestId"], (*call)["id"]);
}

// Requests to one provider wait for a free place when its limit of requests at the same time is reached, and take it the moment it is given back.
TEST_F(AiModuleTest, PacesTheRequestsOfAProvider) {
    const json results = run(R"(
        local async = require("async")
        local catalog = include("catalog")
        local pacing = include("pacing")
        local preferences = include("preferences")
        catalog.load()
        preferences.define()
        preferences.saveRateLimit({ providerId = "ollama", minimumIntervalMs = 0, maximumRequestsPerMinute = 0, maximumConcurrentRequests = 1 })
        local order = {}
        local first = pacing.ticket("ollama")
        first:wait(function() end)
        local second = pacing.ticket("ollama")
        local reported

        require("workpane.task").run("test", "second", function()
            second:wait(function(milliseconds) reported = milliseconds end)
            order[#order + 1] = "second admitted"
        end)

        async.sleep(100):await()
        order[#order + 1] = "first released"
        first:release()

        while #order < 2 do
            async.sleep(10):await()
        end

        second:release()
        host.test_result({ order = order, reported = reported })
    )");

    EXPECT_EQ(results["order"], json::array({"first released", "second admitted"}));
    EXPECT_EQ(results["reported"], 0);
}

// A request cancelled before its turn came gives its place back, so the next request to the provider is admitted at once.
TEST_F(AiModuleTest, GivesBackThePlaceOfACancelledRequest) {
    const json results = run(R"(
        local async = require("async")
        local catalog = include("catalog")
        local chat = include("chat")
        local connections = include("connections")
        local pacing = include("pacing")
        local preferences = include("preferences")
        catalog.load()
        preferences.define()
        preferences.saveRateLimit({ providerId = "ollama", minimumIntervalMs = 0, maximumRequestsPerMinute = 0, maximumConcurrentRequests = 1 })
        local client = chat.new({ requestSent = function() end, content = function() end, throttled = function() end })
        client:cancel()
        local connection = connections.declared(catalog.provider("ollama"), "llama3")
        local refused = select(2, pcall(client.send, client, { connection = connection, address = "http://127.0.0.1:9", messages = include("messages").normalize({ { role = "user", content = "hi" } }), tools = {} }, function(key) return key end))
        local next = pacing.ticket("ollama")
        local admitted = async.timeout(async.promise(function() return next:wait(function() end) end), 500):await()
        next:release()
        host.test_result({ refused = refused.code, admitted = admitted == true })
    )");

    EXPECT_EQ(results["refused"], "ai_cancelled");
    EXPECT_EQ(results["admitted"], true);
}

} // namespace workpane::tests
