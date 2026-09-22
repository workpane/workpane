#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVariant>

namespace workpane::persistence {

class StoredValues final {
  public:
    // A text column declared NOT NULL rejects a null QString, which Qt binds as a SQL NULL rather than an empty value.
    static QString storedText(const QString& value) {
        return value.isNull() ? QString::fromLatin1("") : value;
    }

    static QString storedTimestamp(const QDateTime& value) {
        return value.toUTC().toString(Qt::ISODateWithMs);
    }

    static QDateTime parseStoredTimestamp(const QVariant& value) {
        const QString text = value.toString();
        return text.isEmpty() ? QDateTime{} : QDateTime::fromString(text, Qt::ISODateWithMs);
    }

    static bool validStoredTimestamp(const QDateTime& value) {
        return value.isValid() && value.timeSpec() == Qt::UTC;
    }

    static bool readStoredInteger(const QVariant& value, qint64& output) {
        const int type = value.metaType().id();

        if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong && type != QMetaType::ULongLong) {
            return false;
        }

        bool valid = false;
        output = value.toLongLong(&valid);
        return valid;
    }
};

} // namespace workpane::persistence
