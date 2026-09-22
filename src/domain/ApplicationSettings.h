#pragma once

#include <QByteArray>
#include <QString>

namespace workpane::domain {

struct ApplicationSettings final {
    QByteArray windowGeometry;
    QString language;
    QString themeId{QStringLiteral("green")};
};

} // namespace workpane::domain
