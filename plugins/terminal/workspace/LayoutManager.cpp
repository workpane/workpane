#include "workspace/LayoutManager.h"

#include <algorithm>
#include <optional>

namespace workpane::plugins::terminalplugin::workspace {

QVector<domain::LayoutPreset> LayoutManager::presets() {
    return {{"1-single", "Single", 1, 1, 1}, {"2-columns", "Two Columns", 2, 2, 1}, {"2-rows", "Two Rows", 2, 1, 2}, {"3-left", "Large Left", 3, 2, 2}, {"3-bottom", "Large Bottom", 3, 2, 2}, {"4-grid", "Two by Two", 4, 2, 2}, {"5-balanced", "Five Balanced", 5, 3, 2}, {"6-columns", "Three by Two", 6, 3, 2}, {"6-rows", "Two by Three", 6, 2, 3}, {"7-balanced", "Seven Balanced", 7, 3, 3}, {"8-columns", "Four by Two", 8, 4, 2}, {"8-rows", "Two by Four", 8, 2, 4}, {"9-grid", "Three by Three", 9, 3, 3}, {"10-balanced", "Five by Two", 10, 5, 2}, {"11-balanced", "Eleven Balanced", 11, 4, 3}, {"12-columns", "Four by Three", 12, 4, 3}, {"12-rows", "Three by Four", 12, 3, 4}};
}

Result<domain::LayoutPreset> LayoutManager::preset(const QString& presetId) {
    const auto availablePresets = presets();
    const auto match = std::ranges::find(availablePresets, presetId, &domain::LayoutPreset::id);

    if (match == availablePresets.end()) {
        return Result<domain::LayoutPreset>::failure({"terminal_layout_preset_unknown", "The terminal layout preset is unknown", presetId});
    }

    return Result<domain::LayoutPreset>::success(*match);
}

bool LayoutManager::contains(const domain::SlotLayoutState& layout, const QString& sessionId) {
    // clang-format off
    const bool inSlot = std::ranges::any_of(layout.slotAssignments, [&](const auto& assigned) { return assigned.has_value() && assigned.value() == sessionId; });
    // clang-format on
    return inSlot || layout.shelf.contains(sessionId);
}

int LayoutManager::visibleSlotIndex(const domain::SlotLayoutState& layout, const QString& sessionId) {
    const auto assignment = std::ranges::find(layout.slotAssignments, sessionId);

    if (assignment == layout.slotAssignments.end()) {
        return -1;
    }

    return static_cast<int>(std::distance(layout.slotAssignments.begin(), assignment));
}

Result<void> LayoutManager::changePreset(domain::SlotLayoutState& layout, const QString& presetId) {
    const auto requested = preset(presetId);

    if (!requested.hasValue()) {
        return Result<void>::failure(requested.error());
    }

    const domain::LayoutPreset selectedPreset = requested.value();

    if (selectedPreset.slotCount < layout.slotCount) {
        for (int index = selectedPreset.slotCount; index < layout.slotAssignments.size(); ++index) {
            const auto& assignment = layout.slotAssignments.at(index);
            if (assignment.has_value()) {
                layout.shelf.append(*assignment);
            }
        }
    }

    layout.slotAssignments.resize(selectedPreset.slotCount);
    layout.slotCount = selectedPreset.slotCount;
    layout.presetId = selectedPreset.id;
    return Result<void>::success();
}

Result<void> LayoutManager::assignToSlot(domain::SlotLayoutState& layout, const QString& sessionId, int slotIndex) {
    if (slotIndex < 0 || slotIndex >= layout.slotCount || slotIndex >= layout.slotAssignments.size()) {
        return Result<void>::failure({"terminal_layout_slot_invalid", "The terminal slot is outside the active layout", QString::number(slotIndex)});
    }

    const auto sourceSlot = std::ranges::find(layout.slotAssignments, sessionId);
    const int sourceSlotIndex = sourceSlot == layout.slotAssignments.end() ? -1 : static_cast<int>(std::distance(layout.slotAssignments.begin(), sourceSlot));
    const std::optional<QString> displaced = layout.slotAssignments.at(slotIndex);

    remove(layout, sessionId);
    layout.slotAssignments[slotIndex] = sessionId;

    if (!displaced.has_value() || displaced.value() == sessionId) {
        return Result<void>::success();
    }

    if (sourceSlotIndex >= 0) {
        layout.slotAssignments[sourceSlotIndex] = displaced.value();
        return Result<void>::success();
    }

    layout.shelf.prepend(displaced.value());
    return Result<void>::success();
}

void LayoutManager::moveToShelf(domain::SlotLayoutState& layout, const QString& sessionId, int shelfIndex) {
    remove(layout, sessionId);

    if (shelfIndex < 0 || shelfIndex >= layout.shelf.size()) {
        layout.shelf.append(sessionId);
        return;
    }

    layout.shelf.insert(shelfIndex, sessionId);
}

void LayoutManager::remove(domain::SlotLayoutState& layout, const QString& sessionId) {
    for (auto& assigned : layout.slotAssignments) {
        if (assigned.has_value() && assigned.value() == sessionId) {
            assigned.reset();
        }
    }

    layout.shelf.removeAll(sessionId);
}

} // namespace workpane::plugins::terminalplugin::workspace
