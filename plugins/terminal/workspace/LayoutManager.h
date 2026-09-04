#pragma once

#include "domain/Result.h"
#include "domain/SlotLayoutState.h"

#include <QString>
#include <QVector>

namespace workpane::plugins::terminalplugin::workspace {

class LayoutManager final {
  public:
    [[nodiscard]] static QVector<domain::LayoutPreset> presets();
    [[nodiscard]] static Result<domain::LayoutPreset> preset(const QString& presetId);
    [[nodiscard]] static bool contains(const domain::SlotLayoutState& layout, const QString& sessionId);
    [[nodiscard]] static int visibleSlotIndex(const domain::SlotLayoutState& layout, const QString& sessionId);

    [[nodiscard]] static Result<void> changePreset(domain::SlotLayoutState& layout, const QString& presetId);
    [[nodiscard]] static Result<void> assignToSlot(domain::SlotLayoutState& layout, const QString& sessionId, int slotIndex);
    static void moveToShelf(domain::SlotLayoutState& layout, const QString& sessionId, int shelfIndex = -1);
    static void remove(domain::SlotLayoutState& layout, const QString& sessionId);
};

} // namespace workpane::plugins::terminalplugin::workspace
