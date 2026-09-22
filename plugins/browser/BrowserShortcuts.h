#pragma once

#include <QKeySequence>
#include <QList>

namespace workpane::plugins::browser {

// Qt resolves a standard key through the theme of the running platform and can answer it with a binding nobody presses or with nothing at all, so the combination this product promises is declared here and whatever else that platform names joins it.
class BrowserShortcuts final {
  public:
    static QList<QKeySequence> newTab() {
        return bindings(QKeySequence::AddTab, Qt::Key_T);
    }

    static QList<QKeySequence> closeTab() {
        return bindings(QKeySequence::Close, Qt::Key_W);
    }

    static QList<QKeySequence> reload() {
        return bindings(QKeySequence::Refresh, Qt::Key_R);
    }

  private:
    static QList<QKeySequence> bindings(QKeySequence::StandardKey standardKey, Qt::Key key) {
        QList<QKeySequence> sequences{QKeySequence(Qt::ControlModifier | key)};

        for (const auto& sequence : QKeySequence::keyBindings(standardKey)) {
            if (!sequences.contains(sequence)) {
                sequences.append(sequence);
            }
        }

        return sequences;
    }
};

} // namespace workpane::plugins::browser
