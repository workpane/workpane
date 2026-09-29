#pragma once

#include "process/Process.h"
#include "support/ProcessRecord.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace workpane::tests {

// A recorded program that keeps what it is sent, hands it to the responder of the test and ends when it is stopped.
class FakeProcess final : public process::Process {
  public:
    FakeProcess(std::shared_ptr<ProcessRecord> record, std::size_t index);

    [[nodiscard]] bool write(std::string_view bytes) override;
    void stop() override;

  private:
    static constexpr int stoppedCode{143};

    std::shared_ptr<ProcessRecord> m_record;
    std::size_t m_index;
};

} // namespace workpane::tests
