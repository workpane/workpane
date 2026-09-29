#include "ui/model/SurfaceStore.h"

#include "ui/model/RenderContext.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace workpane::ui {

void SurfaceStore::collect(const Component& subtree, std::unordered_set<NodeId>& identities) {
    identities.insert(subtree.id());

    for (const auto& child : subtree.children()) {
        collect(*child, identities);
    }
}

SurfaceStore::SurfaceStore(const ComponentRegistry& registry) : m_registry(registry) {}

Result<void> SurfaceStore::mount(std::string surface, std::string owner, std::filesystem::path assets, const json::Json& tree, RenderContext& context) {
    if (surface.empty() || owner.empty()) {
        return Result<void>::failure({"ui_surface_invalid", "A surface needs an identity and an owner", surface});
    }

    const auto existing = m_surfaces.find(surface);

    if (existing != m_surfaces.end() && existing->second.owner != owner) {
        return Result<void>::failure({"ui_surface_foreign", "A surface belongs to another plugin", surface});
    }

    Staged staged;
    auto root = build(tree, staged, nullptr, 0);

    if (!root.hasValue()) {
        return Result<void>::failure(root.error());
    }

    if (existing != m_surfaces.end()) {
        context.setSurface(existing->second.id, existing->second.assets);
        existing->second.root->detach(context);
        m_surfaces.erase(existing);
    }

    Surface mounted{surface, std::move(owner), std::move(assets), std::move(root.value()), std::move(staged.nodes)};
    m_surfaces.emplace(std::move(surface), std::move(mounted));

    return Result<void>::success();
}

// A patch is read into the node itself and proven against all of its properties and its children, so a refused patch leaves the node exactly as it was.
Result<void> SurfaceStore::patch(std::string_view surface, std::string_view owner, NodeId node, const json::Json& properties) {
    auto found = owned(surface, owner);

    if (!found.hasValue()) {
        return Result<void>::failure(found.error());
    }

    const auto component = found.value()->nodes.find(node);

    if (component == found.value()->nodes.end()) {
        return Result<void>::failure({"ui_node_unknown", "A surface has no node with that identity", std::string(surface) + ":" + std::to_string(node)});
    }

    if (!properties.is_object()) {
        return Result<void>::failure({"ui_properties_invalid", "Node properties must be an object", std::string(surface) + ":" + std::to_string(node)});
    }

    return component->second->patch(properties);
}

// A node inside the children being replaced that the new set names as kept is the same component with its state, wherever the new set places it.
// A page, an editor or a running shell that stays is therefore never built again, even when it moves to another container.
// The properties given with the children are patched onto the parent in the same step, so a strip of tabs changes its tabs and its pages at once.
Result<void> SurfaceStore::replaceChildren(std::string_view surface, std::string_view owner, NodeId node, const json::Json& children, const json::Json& properties, RenderContext& context) {
    auto found = owned(surface, owner);

    if (!found.hasValue()) {
        return Result<void>::failure(found.error());
    }

    Surface& target = *found.value();
    const auto component = target.nodes.find(node);
    const auto depth = depthOf(*target.root, node);

    if (component == target.nodes.end() || !depth.has_value()) {
        return Result<void>::failure({"ui_node_unknown", "A surface has no node with that identity", std::string(surface) + ":" + std::to_string(node)});
    }

    Component& parent = *component->second;

    if (!properties.is_object()) {
        return Result<void>::failure({"ui_properties_invalid", "Node properties must be an object", std::string(surface) + ":" + std::to_string(node)});
    }

    if (!json::ObjectReader::isList(children) || children.size() > parent.childLimit()) {
        return Result<void>::failure({"ui_children_refused", "A node refuses these children", std::string(parent.kind())});
    }

    std::unordered_map<NodeId, Origin> origins;
    locate(nullptr, parent.children(), origins);
    auto kept = keptNodes(children, origins);

    if (!kept.hasValue()) {
        return Result<void>::failure(kept.error());
    }

    // A kept node carries its whole subtree, so a node inside another kept node cannot move on its own.
    std::unordered_set<NodeId> carried;

    for (const NodeId identity : kept.value()) {
        std::unordered_set<NodeId> subtree;
        collect(*origin(parent.children(), origins.at(identity)), subtree);

        for (const NodeId inner : subtree) {
            if (inner != identity && kept.value().contains(inner)) {
                return Result<void>::failure({"ui_child_not_kept", "A kept node lies inside another kept node", std::to_string(inner)});
            }
        }

        carried.merge(subtree);
    }

    // Identities of the nodes that leave may be reused, while every other identity of the surface stays taken.
    Surface scratch{target.id, target.owner, target.assets, nullptr, {}};

    for (const auto& [identity, pointer] : target.nodes) {
        if (!origins.contains(identity) || carried.contains(identity)) {
            scratch.nodes.emplace(identity, pointer);
        }
    }

    // The new children sit below the parent among the nodes that stay, so the bounds of depth and size hold for the whole surface.
    Staged staged;
    staged.staying = scratch.nodes.size();
    std::vector<std::unique_ptr<Component>> built;

    for (const auto& spec : children) {
        if (spec.contains("kept")) {
            staged.placements.push_back({nullptr, built.size(), spec.at("id").get<NodeId>(), *depth + 1});
            built.push_back(nullptr);
            continue;
        }

        auto child = build(spec, staged, &scratch, *depth + 1);

        if (!child.hasValue()) {
            return Result<void>::failure(child.error());
        }

        built.push_back(std::move(child.value()));
    }

    for (const auto& place : staged.placements) {
        const std::size_t deepest = place.depth + height(*origin(parent.children(), origins.at(place.node))) - 1;

        if (deepest > maximumDepth) {
            return Result<void>::failure({"ui_tree_too_deep", "A tree nests deeper than the declared bound", std::to_string(deepest)});
        }
    }

    // Kept nodes move into their new places while the new set is proven, and a refused set puts every one of them back where it was.
    std::vector<std::unique_ptr<Component>> previous = std::move(parent.children());
    parent.children() = std::move(built);

    for (const auto& place : staged.placements) {
        destination(parent, place) = std::move(origin(previous, origins.at(place.node)));
    }

    auto proven = Result<void>::success();

    for (Component* waiting : staged.waiting) {
        if (proven.hasValue()) {
            proven = waiting->validateChildren();
        }
    }

    // The parent is patched last, because a refused patch puts its properties back while a later failure could not.
    if (proven.hasValue()) {
        proven = parent.patch(properties);
    }

    if (!proven.hasValue()) {
        for (const auto& place : staged.placements) {
            origin(previous, origins.at(place.node)) = std::move(destination(parent, place));
        }

        parent.children() = std::move(previous);
        return proven;
    }

    // Everything is proven, so the places the kept nodes left are closed and the nodes that left are released in one step.
    for (const auto& place : staged.placements) {
        const Origin& left = origins.at(place.node);

        if (left.holder != nullptr) {
            std::erase(left.holder->children(), nullptr);
        }
    }

    std::erase(previous, nullptr);
    context.setSurface(target.id, target.assets);

    for (auto& child : previous) {
        child->detach(context);
        forget(target, *child);
    }

    target.nodes.merge(staged.nodes);

    return Result<void>::success();
}

// Every node below a parent is recorded with the node holding it, where the children of the parent itself have no holder.
void SurfaceStore::locate(Component* holder, const std::vector<std::unique_ptr<Component>>& children, std::unordered_map<NodeId, Origin>& origins) {
    for (std::size_t index = 0; index < children.size(); ++index) {
        origins.emplace(children[index]->id(), Origin{holder, index});
        locate(children[index].get(), children[index]->children(), origins);
    }
}

// Answers how far below the root of its surface a node lies, where the root lies at depth zero.
std::optional<std::size_t> SurfaceStore::depthOf(const Component& subtree, NodeId node) {
    if (subtree.id() == node) {
        return 0;
    }

    for (const auto& child : subtree.children()) {
        if (const auto below = depthOf(*child, node); below.has_value()) {
            return *below + 1;
        }
    }

    return std::nullopt;
}

// Answers how many levels a subtree spans, one for a node without children.
std::size_t SurfaceStore::height(const Component& subtree) {
    std::size_t tallest = 0;

    for (const auto& child : subtree.children()) {
        tallest = std::max(tallest, height(*child));
    }

    return tallest + 1;
}

// A kept node is named by its identity alone, once, anywhere in the new set, and it must lie inside the children being replaced.
Result<std::unordered_set<NodeId>> SurfaceStore::keptNodes(const json::Json& children, const std::unordered_map<NodeId, Origin>& origins) {
    std::unordered_set<NodeId> kept;
    std::vector<const json::Json*> lists{&children};

    while (!lists.empty()) {
        const json::Json& list = *lists.back();
        lists.pop_back();

        for (const auto& spec : list) {
            if (!spec.is_object()) {
                continue;
            }

            if (!spec.contains("kept")) {
                if (const auto nested = spec.find("children"); nested != spec.end() && nested->is_array()) {
                    lists.push_back(&*nested);
                }

                continue;
            }

            std::int64_t identity = 0;
            bool keep = false;
            json::ObjectReader reader(spec, "ui.children.kept");
            reader.readInteger("id", identity, 1, largestNodeId).read("kept", keep);

            if (const auto finished = reader.finish(); !finished.hasValue()) {
                return Result<std::unordered_set<NodeId>>::failure(finished.error());
            }

            const auto child = static_cast<NodeId>(identity);

            if (!keep || !origins.contains(child) || !kept.insert(child).second) {
                return Result<std::unordered_set<NodeId>>::failure({"ui_child_not_kept", "A kept node names nothing inside the children being replaced, or names one twice", std::to_string(identity)});
            }
        }
    }

    return Result<std::unordered_set<NodeId>>::success(std::move(kept));
}

std::unique_ptr<Component>& SurfaceStore::origin(std::vector<std::unique_ptr<Component>>& previous, const Origin& place) {
    return place.holder == nullptr ? previous[place.index] : place.holder->children()[place.index];
}

std::unique_ptr<Component>& SurfaceStore::destination(Component& parent, const Placement& place) {
    return place.holder == nullptr ? parent.children()[place.index] : place.holder->children()[place.index];
}

Result<void> SurfaceStore::command(std::string_view surface, std::string_view owner, NodeId node, std::string_view name, const json::Json& arguments, RenderContext& context) {
    auto found = owned(surface, owner);

    if (!found.hasValue()) {
        return Result<void>::failure(found.error());
    }

    const auto component = found.value()->nodes.find(node);

    if (component == found.value()->nodes.end()) {
        return Result<void>::failure({"ui_node_unknown", "A surface has no node with that identity", std::string(surface) + ":" + std::to_string(node)});
    }

    // An event a command raises belongs to the surface of its node, whichever surface the frame visited last.
    context.setSurface(found.value()->id, found.value()->assets);

    return component->second->command(context, name, arguments);
}

Result<void> SurfaceStore::unmount(std::string_view surface, std::string_view owner, RenderContext& context) {
    auto found = owned(surface, owner);

    if (!found.hasValue()) {
        return Result<void>::failure(found.error());
    }

    context.setSurface(found.value()->id, found.value()->assets);
    found.value()->root->detach(context);
    m_surfaces.erase(m_surfaces.find(surface));

    return Result<void>::success();
}

void SurfaceStore::unmountOwner(std::string_view owner, RenderContext& context) {
    for (auto surface = m_surfaces.begin(); surface != m_surfaces.end();) {
        if (surface->second.owner != owner) {
            ++surface;
            continue;
        }

        context.setSurface(surface->second.id, surface->second.assets);
        surface->second.root->detach(context);
        surface = m_surfaces.erase(surface);
    }
}

void SurfaceStore::unmountAll(RenderContext& context) {
    for (auto& [identity, surface] : m_surfaces) {
        context.setSurface(surface.id, surface.assets);
        surface.root->detach(context);
    }

    m_surfaces.clear();
}

Surface* SurfaceStore::find(std::string_view surface) {
    const auto found = m_surfaces.find(surface);
    return found == m_surfaces.end() ? nullptr : &found->second;
}

bool SurfaceStore::contains(std::string_view surface) const {
    return m_surfaces.contains(surface);
}

// Every mounted component gets its turn each frame after drawing, so one that is not on screen keeps up with the work it owns.
void SurfaceStore::update(RenderContext& context) {
    for (auto& [identity, mounted] : m_surfaces) {
        context.setSurface(mounted.id, mounted.assets);

        for (auto& [node, component] : mounted.nodes) {
            component->update(context);
        }
    }
}

Result<std::unique_ptr<Component>> SurfaceStore::build(const json::Json& spec, Staged& staged, const Surface* existing, std::size_t depth) {
    if (depth > maximumDepth) {
        return Result<std::unique_ptr<Component>>::failure({"ui_tree_too_deep", "A tree nests deeper than the declared bound", std::to_string(depth)});
    }

    if (staged.staying + staged.nodes.size() >= maximumNodes) {
        return Result<std::unique_ptr<Component>>::failure({"ui_tree_too_large", "A tree carries more nodes than the declared bound", std::to_string(staged.staying + staged.nodes.size())});
    }

    std::int64_t identity = 0;
    std::string kind;
    const json::Json* properties = &json::ObjectReader::emptyObject();
    const json::Json* children = &json::ObjectReader::emptyList();
    json::ObjectReader reader(spec, "node");

    // Node identities travel through Lua numbers, so the largest one is the largest integer a double holds exactly.
    reader.readInteger("id", identity, 1, largestNodeId).readText("kind", kind).readObject("props", properties, json::Presence::Optional).readArray("children", children, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return Result<std::unique_ptr<Component>>::failure(finished.error());
    }

    const auto node = static_cast<NodeId>(identity);
    auto component = m_registry.create(kind, node);

    if (component == nullptr) {
        return Result<std::unique_ptr<Component>>::failure({"ui_kind_unknown", "A node names a component kind nobody registered", kind});
    }

    if (staged.nodes.contains(node) || (existing != nullptr && existing->nodes.contains(node))) {
        return Result<std::unique_ptr<Component>>::failure({"ui_node_duplicate", "Two nodes of one surface share an identity", std::to_string(node)});
    }

    if (const auto applied = component->apply(*properties); !applied.hasValue()) {
        return Result<std::unique_ptr<Component>>::failure(applied.error());
    }

    if (children->size() > component->childLimit()) {
        return Result<std::unique_ptr<Component>>::failure({"ui_children_refused", "A node carries more children than its kind accepts", kind});
    }

    staged.nodes.emplace(node, component.get());

    // A node that waits for a kept child moving into it is proven once that child is in place.
    bool waiting = false;

    for (const auto& childSpec : *children) {
        if (existing != nullptr && childSpec.contains("kept")) {
            staged.placements.push_back({component.get(), component->children().size(), childSpec.at("id").get<NodeId>(), depth + 1});
            component->children().push_back(nullptr);
            waiting = true;
            continue;
        }

        auto child = build(childSpec, staged, existing, depth + 1);

        if (!child.hasValue()) {
            return child;
        }

        component->children().push_back(std::move(child.value()));
    }

    if (waiting) {
        staged.waiting.push_back(component.get());
        return Result<std::unique_ptr<Component>>::success(std::move(component));
    }

    if (const auto proven = component->validateChildren(); !proven.hasValue()) {
        return Result<std::unique_ptr<Component>>::failure(proven.error());
    }

    return Result<std::unique_ptr<Component>>::success(std::move(component));
}

Result<Surface*> SurfaceStore::owned(std::string_view surface, std::string_view owner) {
    const auto found = m_surfaces.find(surface);

    if (found == m_surfaces.end()) {
        return Result<Surface*>::failure({"ui_surface_unknown", "No surface is mounted under that identity", std::string(surface)});
    }

    if (found->second.owner != owner) {
        return Result<Surface*>::failure({"ui_surface_foreign", "A surface belongs to another plugin", std::string(surface)});
    }

    return Result<Surface*>::success(&found->second);
}

void SurfaceStore::forget(Surface& surface, const Component& subtree) {
    surface.nodes.erase(subtree.id());

    for (const auto& child : subtree.children()) {
        forget(surface, *child);
    }
}

} // namespace workpane::ui
