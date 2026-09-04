#pragma once

#include <QString>
#include <QStringList>

namespace workpane::plugins {

struct CapabilityDescriptor final {
    QString name;
    int version{1};
};

// The core owns the vocabulary of the seams so a provider and a consumer cannot spell one differently, and owns no implementation of any of them.
inline constexpr auto openFolderCapability = "workspace.folder.open";
inline constexpr auto serveFolderCapability = "workspace.folder.serve";
inline constexpr auto openPageCapability = "workspace.page.open";
inline constexpr auto terminalSnapshotCapability = "terminal.workspace.snapshot";

class CapabilityNames final {
  public:
    // A capability is named for what it does rather than for who does it, in the same three lowercase components a translation key uses.
    static bool validCapabilityName(const QString& name) {
        const QStringList components = name.split(QLatin1Char('.'));

        if (components.size() != 3) {
            return false;
        }

        for (const QString& component : components) {
            if (component.isEmpty() || component.startsWith(QLatin1Char('-')) || component.endsWith(QLatin1Char('-'))) {
                return false;
            }

            for (const QChar character : component) {
                const bool allowed = (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('0') && character <= QLatin1Char('9')) || character == QLatin1Char('-');

                if (!allowed) {
                    return false;
                }
            }
        }

        return true;
    }
};

} // namespace workpane::plugins
