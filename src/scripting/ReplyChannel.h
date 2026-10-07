#pragma once

#include "Result.h"

#include <nlohmann/json.hpp>

#include <cstdint>

namespace workpane::scripting {

class ScriptRuntime;

// Answers an asynchronous request of Lua by its identity, which the SDK turns back into the promise that asked for it.
class ReplyChannel final {
  public:
    explicit ReplyChannel(ScriptRuntime& runtime);

    void reply(std::int64_t request, const Result<nlohmann::json>& result);

  private:
    ScriptRuntime& m_runtime;
};

} // namespace workpane::scripting
