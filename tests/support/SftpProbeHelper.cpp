#include "support/SftpProbeHelper.h"

#include "app/Product.h"
#include "platform/NativeProcesses.h"
#include "platform/PathTextHelper.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/Resources.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace workpane::tests {

void SftpProbeHelper::verify(const std::filesystem::path& executable, std::vector<std::string> arguments, const std::filesystem::path& workspace) {
    arguments.insert(arguments.end() - 1, "-s");
    arguments.emplace_back("sftp");
    HeadlessProduct product(workspace / "direct-sftp-data");
    product.setProcessLauncher(std::make_unique<platform::NativeProcesses>());
    ASSERT_TRUE(product.boot().hasValue());
    product.recordCalls("workpane_log");
    const std::string modules = platform::PathTextHelper::generic(Resources::staged() / "plugins" / "remote-ssh");
    const auto root = platform::PathTextHelper::generic(std::filesystem::canonical(workspace));
    const std::string script = "local MODULES = " + nlohmann::json(modules).dump() + "\nlocal PROGRAM = " + nlohmann::json(platform::PathTextHelper::generic(executable)).dump() + "\nlocal ROOT = " + nlohmann::json(root).dump() + "\nlocal ARGUMENTS = require('json').decode(" + nlohmann::json(nlohmann::json(arguments).dump()).dump() + ")\n" + R"lua(
        local process = require("workpane.process")
        local bridge = require("workpane.bridge")
        local environment = setmetatable({}, { __index = _G })
        local loaded = {}
        environment.workpane = { process = {
            start = function(options) return process.start("remote-ssh", options) end,
            write = function(identity, bytes) return process.write("remote-ssh", identity, bytes) end,
            endInput = function(identity) return process.endInput("remote-ssh", identity) end,
            stop = function(identity) return process.stop("remote-ssh", identity) end,
        } }

        function environment.include(name)
            if loaded[name] == nil then
                loaded[name] = assert(loadfile(MODULES .. "/" .. name .. ".lua", "t", environment))()
            end

            return loaded[name]
        end

        require("workpane.task").run("remote-ssh", "test", function()
            local packets = environment.include("sftp-packets")
            local types = packets.types
            local client = environment.include("sftp").new({ program = PROGRAM, arguments = ARGUMENTS, directory = ROOT })
            local ready, problem = client:start(10000)
            assert(ready, problem ~= nil and problem.code)
            local function request(kind, payload, expected)
                local value, failure = client:request(kind, payload, expected, 10000)
                assert(value ~= nil, failure ~= nil and failure.code)

                return value
            end

            local windows = require("platform").os() == "windows"
            local wireRoot = windows and ROOT:match("^[A-Za-z]:/") ~= nil and "/" .. ROOT or ROOT
            local canonical = request(types.realpath, packets.string(wireRoot), types.name)
            assert(#canonical.entries == 1)
            local resolved = canonical.entries[1].name

            if windows and resolved:match("^/[A-Za-z]:/") ~= nil then
                resolved = resolved:sub(2)
            end

            assert(resolved == ROOT)
            local filename = wireRoot .. "/binary '[]; 雪.txt"
            local flags = packets.integer(2 | 8 | 32)
            local handle = request(types.open, packets.string(filename) .. flags .. packets.integer(4) .. packets.integer(384), types.handle).handle
            local source = "\0\255\128\195\169\r\n" .. string.rep("binary", 11000)

            for offset = 0, #source - 1, packets.maximumData do
                local part = source:sub(offset + 1, offset + packets.maximumData)
                assert(request(types.write, packets.string(handle) .. packets.offset(offset) .. packets.string(part), types.status).status == 0)
            end

            assert(request(types.close, packets.string(handle), types.status).status == 0)
            local attributes = request(types.stat, packets.string(filename), types.attrs).attributes
            assert(attributes.size == #source and attributes.permissions & 511 == 384)
            handle = request(types.open, packets.string(filename) .. packets.integer(1) .. packets.integer(0), types.handle).handle
            local output = {}
            local count = 0

            while true do
                local value = request(types.read, packets.string(handle) .. packets.offset(count) .. packets.integer(packets.maximumData), types.data)

                if value.type == types.status then
                    assert(value.status == 1)
                    break
                end

                output[#output + 1] = value.data
                count = count + #value.data
            end

            assert(table.concat(output) == source)
            assert(request(types.close, packets.string(handle), types.status).status == 0)
            assert(client:close())
            bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "native-sftp-test", message = "The SFTP transfer completed", details = { bytes = count, closed = client.closed, ended = client.ended } })
        end)
    )lua";
    ASSERT_TRUE(product.product().runtime().loadString(script, "native-sftp-transport").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("category", "") == "native-sftp-test"; }));
    // clang-format on
    const auto result = product.lastCall("workpane_log")["details"];
    EXPECT_EQ(result, nlohmann::json({{"bytes", 66007}, {"closed", true}, {"ended", true}}));
    std::ifstream transferred(workspace / std::filesystem::path(std::u8string(u8"binary '[]; 雪.txt")), std::ios::binary);
    const std::string content{std::istreambuf_iterator<char>(transferred), std::istreambuf_iterator<char>()};
    ASSERT_EQ(content.size(), 66007U);
    EXPECT_EQ(content.substr(0, 7), std::string("\0\xff\x80\xc3\xa9\r\n", 7));
    EXPECT_EQ(content.substr(7, 12), "binarybinary");
}

} // namespace workpane::tests
