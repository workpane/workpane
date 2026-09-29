#pragma once

#include "Result.h"
#include "platform/SystemInspector.h"
#include "support/SystemRecord.h"

#include <nlohmann/json.hpp>

#include <memory>

namespace workpane::tests {

// Answers the snapshot the record holds, or a fixed machine with every category filled, and fails when the record says so.
class FakeSystemInspector final : public platform::SystemInspector {
  public:
    explicit FakeSystemInspector(std::shared_ptr<SystemRecord> record);

    [[nodiscard]] Result<nlohmann::json> inspect() override;
    [[nodiscard]] static nlohmann::json sample();

  private:
    std::shared_ptr<SystemRecord> m_record;
};

} // namespace workpane::tests
