#include "terminal/TerminalEnvironment.h"

#include <QString>

namespace workpane::terminalcore {

QProcessEnvironment TerminalEnvironment::sessionEnvironment() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("Workpane"));
    return environment;
}

} // namespace workpane::terminalcore
