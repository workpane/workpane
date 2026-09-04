#pragma once

#include <QAbstractButton>
#include <QTabBar>
#include <QTabWidget>

namespace workpane::ui {

class Theme;

class TabCloseButton final : public QAbstractButton {
    Q_OBJECT

  public:
    TabCloseButton(const Theme& theme, QWidget* parent);

    void applyTheme(const Theme& theme);
    [[nodiscard]] QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    const Theme* m_theme{nullptr};
    bool m_hovered{false};
};

class TabBar final : public QTabBar {
    Q_OBJECT

  public:
    TabBar(const Theme& theme, QWidget* parent);

    void applyTheme(const Theme& theme);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void tabInserted(int index) override;
    [[nodiscard]] QSize tabSizeHint(int index) const override;

  private:
    void installCloseButton(int index);

    const Theme* m_theme{nullptr};
};

class TabWidget final : public QTabWidget {
    Q_OBJECT

  public:
    TabWidget(const Theme& theme, QWidget* parent);

    void applyTheme(const Theme& theme);

  private:
    TabBar* m_bar{nullptr};
};

} // namespace workpane::ui
