#pragma once

#include "plugins/PluginInterface.h"

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

namespace workpane::test {

class TestCatalogs final {
  public:
    // A catalog is complete when every language the selector offers spells every key, so no reader ever falls back to another language.
    static void expectCompleteCatalog(const QString& owner, const plugins::TranslationCatalog& catalog) {
        const QStringList languages{QStringLiteral("en"), QStringLiteral("pt")};

        for (const auto& language : languages) {
            ASSERT_TRUE(catalog.contains(language)) << owner.toStdString() << " declares no " << language.toStdString();
        }

        const plugins::TranslationEntries english = catalog.value(QStringLiteral("en"));
        const QRegularExpression keyPattern(QStringLiteral("^[a-z0-9-]+\\.[a-z0-9-]+\\.[a-z0-9-]+$"));
        const QRegularExpression placeholderPattern(QStringLiteral("%\\d"));

        for (auto entry = english.constBegin(); entry != english.constEnd(); ++entry) {
            EXPECT_TRUE(keyPattern.match(entry.key()).hasMatch()) << owner.toStdString() << " declares " << entry.key().toStdString();
            EXPECT_FALSE(entry.value().trimmed().isEmpty()) << owner.toStdString() << " leaves " << entry.key().toStdString() << " empty";
        }

        for (const auto& language : languages) {
            const plugins::TranslationEntries entries = catalog.value(language);
            for (auto entry = english.constBegin(); entry != english.constEnd(); ++entry) {
                EXPECT_TRUE(entries.contains(entry.key())) << owner.toStdString() << " is missing " << entry.key().toStdString() << " in " << language.toStdString();
            }
            for (auto entry = entries.constBegin(); entry != entries.constEnd(); ++entry) {
                EXPECT_TRUE(english.contains(entry.key())) << owner.toStdString() << " spells " << entry.key().toStdString() << " in " << language.toStdString() << " and not in english";
                EXPECT_FALSE(entry.value().trimmed().isEmpty()) << owner.toStdString() << " leaves " << entry.key().toStdString() << " empty in " << language.toStdString();

                QSet<QString> declared;
                QSet<QString> written;
                // clang-format off
                for (auto match = placeholderPattern.globalMatch(english.value(entry.key())); match.hasNext();) { declared.insert(match.next().captured()); }
                for (auto match = placeholderPattern.globalMatch(entry.value()); match.hasNext();) { written.insert(match.next().captured()); }
                // clang-format on
                EXPECT_EQ(written, declared) << owner.toStdString() << " changes the placeholders of " << entry.key().toStdString() << " in " << language.toStdString();
            }
        }
    }
};

} // namespace workpane::test
