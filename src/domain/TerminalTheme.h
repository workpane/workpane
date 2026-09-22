#pragma once

#include <QColor>
#include <QString>

#include <array>

namespace workpane::domain {

struct TerminalTheme final {
    QString id;
    QString name;
    QColor foreground;
    QColor background;
    QColor cursor;
    std::array<QColor, 16> ansiPalette;
};

} // namespace workpane::domain
