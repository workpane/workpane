#pragma once

#include "ui/ApplicationShortcuts.h"

#include <QKeyEvent>
#include <QKeySequence>

#include <algorithm>

namespace workpane::terminalcore {

constexpr auto modifierMask = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

// Qt reports the macOS Command key as ControlModifier and the physical Control key as MetaModifier.
constexpr auto applicationModifier = Qt::ControlModifier;

#ifdef Q_OS_MACOS
constexpr auto terminalControlModifier = Qt::MetaModifier;
#else
constexpr auto terminalControlModifier = Qt::ControlModifier;
#endif

class TerminalShortcuts final {
  public:
    static Qt::KeyboardModifiers modifiers(const QKeyEvent& event) {
        return event.modifiers() & modifierMask;
    }

    static QKeySequence newTerminal() {
        return QKeySequence(QKeySequence::New);
    }

    static QKeySequence newTab() {
        return QKeySequence(applicationModifier | Qt::ShiftModifier | Qt::Key_T);
    }

    static QKeySequence layout() {
        return QKeySequence(applicationModifier | Qt::ShiftModifier | Qt::Key_L);
    }

    static bool matches(const QKeyEvent& event, const QKeySequence& sequence) {
        return QKeySequence(event.keyCombination()) == sequence;
    }

    static bool matches(const QKeyEvent& event, const QList<QKeySequence>& sequences) {
        // clang-format off
        return std::ranges::any_of(sequences, [&event](const QKeySequence& sequence) { return matches(event, sequence); });
        // clang-format on
    }

    // Control with C interrupts what the shell is running, so only a combination the shell does not own copies the selection.
    // Control with W deletes the previous word in a shell, so only macOS closes with the plain application combination.
    static QKeySequence closeTerminal() {
#ifdef Q_OS_MACOS
        return QKeySequence(QKeySequence::Close);
#else
        return QKeySequence(applicationModifier | Qt::ShiftModifier | Qt::Key_W);
#endif
    }

    static bool isCloseTerminal(const QKeyEvent& event) {
#ifdef Q_OS_MACOS
        return event.matches(QKeySequence::Close);
#else
        return matches(event, closeTerminal());
#endif
    }

    static bool isCopy(const QKeyEvent& event) {
        const Qt::KeyboardModifiers keyModifiers = modifiers(event);

        if (event.key() == Qt::Key_Copy && keyModifiers == Qt::NoModifier) {
            return true;
        }
        if (event.key() != Qt::Key_C) {
            return false;
        }

#ifdef Q_OS_LINUX
        return keyModifiers == (Qt::ControlModifier | Qt::ShiftModifier);
#else
        return keyModifiers == applicationModifier || keyModifiers == (applicationModifier | Qt::ShiftModifier);
#endif
    }

    static bool isPaste(const QKeyEvent& event) {
        const Qt::KeyboardModifiers keyModifiers = modifiers(event);

        if (event.key() == Qt::Key_Paste && keyModifiers == Qt::NoModifier) {
            return true;
        }
        if (event.key() == Qt::Key_Insert && keyModifiers == Qt::ShiftModifier) {
            return true;
        }
        if (event.key() != Qt::Key_V) {
            return false;
        }

#ifdef Q_OS_LINUX
        return keyModifiers == (Qt::ControlModifier | Qt::ShiftModifier);
#else
        return keyModifiers == applicationModifier || keyModifiers == (applicationModifier | Qt::ShiftModifier);
#endif
    }

    // Control with A moves to the beginning of the line in a shell, so only macOS selects with the plain application combination.
    static bool isSelectAll(const QKeyEvent& event) {
#ifdef Q_OS_MACOS
        return event.matches(QKeySequence::SelectAll);
#else
        return modifiers(event) == (Qt::ControlModifier | Qt::ShiftModifier) && event.key() == Qt::Key_A;
#endif
    }

    // Clearing the buffer follows the same rule, because Control with K deletes to the end of the line in a shell.
    static bool isClearBuffer(const QKeyEvent& event) {
        const Qt::KeyboardModifiers keyModifiers = modifiers(event);

        if (event.key() != Qt::Key_K) {
            return false;
        }

#ifdef Q_OS_MACOS
        return keyModifiers == applicationModifier;
#else
        return keyModifiers == (Qt::ControlModifier | Qt::ShiftModifier);
#endif
    }

    // Searching answers the native key on every platform, because no shell owns it.
    static bool isFind(const QKeyEvent& event) {
        return event.matches(QKeySequence::Find);
    }

    static bool isFindNext(const QKeyEvent& event) {
        return event.matches(QKeySequence::FindNext);
    }

    static bool isFindPrevious(const QKeyEvent& event) {
        return event.matches(QKeySequence::FindPrevious);
    }

    static bool isReservedForApplication(const QKeyEvent& event) {
        if (matches(event, ui::ApplicationShortcuts::quit()) || matches(event, newTerminal()) || matches(event, newTab()) || matches(event, layout())) {
            return true;
        }
        if (matches(event, ui::ApplicationShortcuts::increaseContentFont()) || matches(event, ui::ApplicationShortcuts::decreaseContentFont()) || matches(event, ui::ApplicationShortcuts::resetContentFont())) {
            return true;
        }

        return isCloseTerminal(event);
    }

    static bool isTerminalOwned(const QKeyEvent& event) {
        if (isPaste(event) || event.key() == Qt::Key_Tab || event.key() == Qt::Key_Backtab) {
            return true;
        }
        if (isReservedForApplication(event)) {
            return false;
        }

        const Qt::KeyboardModifiers keyModifiers = modifiers(event);
#ifdef Q_OS_MACOS

        if (keyModifiers.testFlag(applicationModifier)) {
            return false;
        }

#endif
        return keyModifiers.testFlag(terminalControlModifier) || keyModifiers.testFlag(Qt::AltModifier);
    }
};

} // namespace workpane::terminalcore
