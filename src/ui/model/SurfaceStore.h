#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/ComponentRegistry.h"
#include "ui/model/NodeId.h"
#include "ui/model/Surface.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace workpane::ui {

class RenderContext;

class SurfaceStore final {
  public:
    static constexpr std::size_t maximumDepth{64};
    static constexpr std::size_t maximumNodes{50000};

    explicit SurfaceStore(const ComponentRegistry& registry);

    [[nodiscard]] Result<void> mount(std::string surface, std::string owner, std::filesystem::path assets, const json::Json& tree, RenderContext& context);
    [[nodiscard]] Result<void> patch(std::string_view surface, std::string_view owner, NodeId node, const json::Json& properties);
    [[nodiscard]] Result<void> replaceChildren(std::string_view surface, std::string_view owner, NodeId node, const json::Json& children, const json::Json& properties, RenderContext& context);
    [[nodiscard]] Result<void> command(std::string_view surface, std::string_view owner, NodeId node, std::string_view name, const json::Json& arguments, RenderContext& context);
    [[nodiscard]] Result<void> unmount(std::string_view surface, std::string_view owner, RenderContext& context);
    void unmountOwner(std::string_view owner, RenderContext& context);
    void unmountAll(RenderContext& context);
    [[nodiscard]] Surface* find(std::string_view surface);
    [[nodiscard]] bool contains(std::string_view surface) const;
    void update(RenderContext& context);

  private:
    static constexpr std::int64_t largestNodeId{9007199254740991};

    struct Origin final {
        Component* holder;
        std::size_t index;
    };

    struct Placement final {
        Component* holder;
        std::size_t index;
        NodeId node;
        std::size_t depth;
    };

    struct Staged final {
        std::unique_ptr<Component> root;
        std::unordered_map<NodeId, Component*> nodes;
        std::vector<Placement> placements;
        std::vector<Component*> waiting;
        std::size_t staying{0};
    };

    static void collect(const Component& subtree, std::unordered_set<NodeId>& identities);
    [[nodiscard]] static std::optional<std::size_t> depthOf(const Component& subtree, NodeId node);
    [[nodiscard]] static std::size_t height(const Component& subtree);
    static void locate(Component* holder, const std::vector<std::unique_ptr<Component>>& children, std::unordered_map<NodeId, Origin>& origins);
    [[nodiscard]] static Result<std::unordered_set<NodeId>> keptNodes(const json::Json& children, const std::unordered_map<NodeId, Origin>& origins);
    [[nodiscard]] static std::unique_ptr<Component>& origin(std::vector<std::unique_ptr<Component>>& previous, const Origin& place);
    [[nodiscard]] static std::unique_ptr<Component>& destination(Component& parent, const Placement& place);

    [[nodiscard]] Result<std::unique_ptr<Component>> build(const json::Json& spec, Staged& staged, const Surface* existing, std::size_t depth);
    [[nodiscard]] Result<Surface*> owned(std::string_view surface, std::string_view owner);
    void forget(Surface& surface, const Component& subtree);

    const ComponentRegistry& m_registry;
    std::map<std::string, Surface, std::less<>> m_surfaces;
};

} // namespace workpane::ui
