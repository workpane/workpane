#pragma once

#include "ui/model/Component.h"
#include "ui/model/NodeId.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// Every component kind is created by name here, so a kind nobody registered is refused by name rather than drawn as nothing.
class ComponentRegistry final {
  public:
    using Factory = std::function<std::unique_ptr<Component>(NodeId)>;

    void add(std::string kind, Factory factory);
    [[nodiscard]] std::unique_ptr<Component> create(std::string_view kind, NodeId id) const;
    [[nodiscard]] std::vector<std::string> kinds() const;

  private:
    std::map<std::string, Factory, std::less<>> m_factories;
};

} // namespace workpane::ui
