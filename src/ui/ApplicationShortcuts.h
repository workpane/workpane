#pragma once

#include <QAction>
#include <QKeySequence>
#include <QList>
#include <QPair>
#include <QVector>
#include <QWidget>

#include <functional>

namespace workpane::ui {
enum class ContentFontStep { Increase, Decrease, Reset };
}

namespace workpane::ui {

class ApplicationShortcuts final {
  public:
    // Qt answers the Quit standard key with the Exit media key outside macOS and with nothing at all under some platform themes, so the combination is declared like every other one here and Qt maps the control modifier to the native key.
    static QKeySequence quit() {
        return QKeySequence(Qt::ControlModifier | Qt::Key_Q);
    }

    // A reader zooms with whichever of these keys their keyboard puts in front of them, so a direction answers every one of them rather than the single one this project happens to prefer.
    static QList<QKeySequence> increaseContentFont() {
        return {QKeySequence(Qt::ControlModifier | Qt::Key_Equal), QKeySequence(Qt::ControlModifier | Qt::Key_Plus), QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_Plus)};
    }

    static QList<QKeySequence> decreaseContentFont() {
        return {QKeySequence(Qt::ControlModifier | Qt::Key_Minus), QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_Minus)};
    }

    static QList<QKeySequence> resetContentFont() {
        return {QKeySequence(Qt::ControlModifier | Qt::Key_0), QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_0)};
    }

    // A reading size belongs to the surface being read, so the keys are rooted at that surface and never reach another one.
    static void installContentFontShortcuts(QWidget* view, const std::function<void(ContentFontStep)>& apply) {
        const QVector<QPair<QList<QKeySequence>, ContentFontStep>> steps{{increaseContentFont(), ContentFontStep::Increase}, {decreaseContentFont(), ContentFontStep::Decrease}, {resetContentFont(), ContentFontStep::Reset}};

        for (const auto& step : steps) {
            auto* action = new QAction(view);
            action->setShortcuts(step.first);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            view->addAction(action);
            // clang-format off
            QObject::connect(action, &QAction::triggered, view, [apply, value = step.second]() { apply(value); });
            // clang-format on
        }
    }
};

} // namespace workpane::ui
