#pragma once

#include <QtTypes>

namespace workpane::terminalcore {

constexpr int maximumTerminalDimension = 32767;
constexpr qint64 maximumTerminalCells = 1024 * 1024;
constexpr int maximumTerminalCellSize = 65535;

class TerminalDimensions final {
  public:
    static bool validTerminalGrid(int columns, int rows) {
        return columns > 0 && rows > 0 && columns <= maximumTerminalDimension && rows <= maximumTerminalDimension && static_cast<qint64>(columns) * rows <= maximumTerminalCells;
    }

    static bool validTerminalCellSize(int width, int height) {
        return width > 0 && height > 0 && width <= maximumTerminalCellSize && height <= maximumTerminalCellSize;
    }
};

} // namespace workpane::terminalcore
