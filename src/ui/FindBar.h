#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

namespace workpane::ui {

class Theme;

// The owner supplies the words, so the shared bar carries no catalog of its own.
struct FindBarLabels final {
    QString placeholder;
    QString caseSensitive;
    QString wholeWord;
    QString previous;
    QString next;
    QString close;
    QString notFound;
};

class FindBar final : public QWidget {
    Q_OBJECT

  public:
    FindBar(const Theme& theme, FindBarLabels labels, QWidget* parent = nullptr);

    [[nodiscard]] QString query() const;
    [[nodiscard]] bool caseSensitive() const;
    [[nodiscard]] bool wholeWord() const;
    void activate(const QString& selectedText);
    void reportMatches(int current, int total, bool capped);

  signals:
    void searchRequested(const QString& query, bool forward);
    void queryChanged();
    void dismissed();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    FindBarLabels m_labels;
    QLineEdit* m_query{nullptr};
    QLabel* m_status{nullptr};
    QToolButton* m_caseSensitive{nullptr};
    QToolButton* m_wholeWord{nullptr};
};

} // namespace workpane::ui
