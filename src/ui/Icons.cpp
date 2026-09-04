#include "ui/Icons.h"

#include "ui/Theme.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <utility>

namespace workpane::ui {

class LayoutIconEngine final : public QIconEngine {
  public:
    LayoutIconEngine(QString presetId, int columns, int rows, int slotCount, const QColor& text, const QColor& textMuted, const QColor& accent, const QColor& hover, const QColor& pressed) : m_presetId(std::move(presetId)), m_columns(columns), m_rows(rows), m_slotCount(slotCount), m_text(text), m_textMuted(textMuted), m_accent(accent), m_hover(hover), m_pressed(pressed) {}

    [[nodiscard]] QIconEngine* clone() const override {
        return new LayoutIconEngine(m_presetId, m_columns, m_rows, m_slotCount, m_text, m_textMuted, m_accent, m_hover, m_pressed);
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        const bool selected = state == QIcon::On;
        const bool active = mode == QIcon::Active || mode == QIcon::Selected;
        const QColor outline = selected || active ? m_text : m_textMuted;
        const QColor fill = selected ? m_accent : active ? m_hover : m_pressed;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(outline, 1.0));
        painter->setBrush(fill);
        const QRectF bounds = QRectF(rect).adjusted(2.0, 3.0, -2.0, -3.0);
        const qreal gap = 1.5;

        if (m_presetId == QStringLiteral("3-left")) {
            const qreal columnWidth = (bounds.width() - gap) / 2.0;
            const qreal rowHeight = (bounds.height() - gap) / 2.0;
            painter->drawRect(QRectF(bounds.left(), bounds.top(), columnWidth, bounds.height()));
            painter->drawRect(QRectF(bounds.left() + columnWidth + gap, bounds.top(), columnWidth, rowHeight));
            painter->drawRect(QRectF(bounds.left() + columnWidth + gap, bounds.top() + rowHeight + gap, columnWidth, rowHeight));
            painter->restore();
            return;
        }

        if (m_presetId == QStringLiteral("3-bottom")) {
            const qreal columnWidth = (bounds.width() - gap) / 2.0;
            const qreal rowHeight = (bounds.height() - gap) / 2.0;
            painter->drawRect(QRectF(bounds.left(), bounds.top(), columnWidth, rowHeight));
            painter->drawRect(QRectF(bounds.left() + columnWidth + gap, bounds.top(), columnWidth, rowHeight));
            painter->drawRect(QRectF(bounds.left(), bounds.top() + rowHeight + gap, bounds.width(), rowHeight));
            painter->restore();
            return;
        }

        const qreal cellWidth = (bounds.width() - gap * (m_columns - 1)) / m_columns;
        const qreal cellHeight = (bounds.height() - gap * (m_rows - 1)) / m_rows;

        for (int index = 0; index < m_slotCount; ++index) {
            const int row = index / m_columns;
            const int column = index % m_columns;
            painter->drawRect(QRectF(bounds.left() + column * (cellWidth + gap), bounds.top() + row * (cellHeight + gap), cellWidth, cellHeight));
        }

        painter->restore();
    }

    [[nodiscard]] QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, pixmap.rect(), mode, state);
        return pixmap;
    }

  private:
    QString m_presetId;
    int m_columns;
    int m_rows;
    int m_slotCount;
    QColor m_text;
    QColor m_textMuted;
    QColor m_accent;
    QColor m_hover;
    QColor m_pressed;
};

class IconsHelper final {
  public:
    static void drawEye(QPainter& painter);
    static void appendSparkle(QPainterPath& path, const QPointF& centre, qreal radius);
    static void drawIcon(QPainter& painter, IconName name, const QRectF& bounds);
};

// The four sides curve toward the centre, which is what makes a star read as a sparkle rather than as a diamond.
void IconsHelper::appendSparkle(QPainterPath& path, const QPointF& centre, qreal radius) {
    const qreal waist = radius * 0.1414;
    path.moveTo(centre.x(), centre.y() - radius);
    path.quadTo(centre.x() + waist, centre.y() - waist, centre.x() + radius, centre.y());
    path.quadTo(centre.x() + waist, centre.y() + waist, centre.x(), centre.y() + radius);
    path.quadTo(centre.x() - waist, centre.y() + waist, centre.x() - radius, centre.y());
    path.quadTo(centre.x() - waist, centre.y() - waist, centre.x(), centre.y() - radius);
    path.closeSubpath();
}

class IconEngine final : public QIconEngine {
  public:
    IconEngine(IconName name, const QColor& color, const QColor& activeColor) : m_name(name), m_color(color), m_activeColor(activeColor) {}

    [[nodiscard]] QIconEngine* clone() const override {
        return new IconEngine(m_name, m_color, m_activeColor);
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        QColor foreground = mode == QIcon::Active || mode == QIcon::Selected ? m_activeColor : m_color;

        if (mode == QIcon::Disabled) {
            foreground.setAlphaF(0.42F);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(foreground);
        IconsHelper::drawIcon(*painter, m_name, rect);
        painter->restore();
    }

    [[nodiscard]] QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, pixmap.rect(), mode, state);
        return pixmap;
    }

  private:
    IconName m_name;
    QColor m_color;
    QColor m_activeColor;
};

void IconsHelper::drawEye(QPainter& painter) {
    QPainterPath eye;
    eye.moveTo(1.2, 8.0);
    eye.quadTo(8.0, 1.8, 14.8, 8.0);
    eye.quadTo(8.0, 14.2, 1.2, 8.0);
    painter.drawPath(eye);
    painter.drawEllipse(QPointF(8.0, 8.0), 2.4, 2.4);
}

void IconsHelper::drawIcon(QPainter& painter, IconName name, const QRectF& bounds) {
    const qreal scale = std::min(bounds.width(), bounds.height()) / 16.0;
    painter.translate(bounds.center());
    painter.scale(scale, scale);
    painter.translate(-8.0, -8.0);

    QPen pen(painter.pen());
    pen.setWidthF(1.45);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (name) {
    case IconName::Add:
        painter.drawLine(QPointF(8.0, 3.0), QPointF(8.0, 13.0));
        painter.drawLine(QPointF(3.0, 8.0), QPointF(13.0, 8.0));
        break;
    case IconName::Terminal:
        painter.drawRoundedRect(QRectF(1.5, 2.5, 13.0, 11.0), 1.5, 1.5);
        painter.drawLine(QPointF(4.0, 6.0), QPointF(6.2, 8.0));
        painter.drawLine(QPointF(6.2, 8.0), QPointF(4.0, 10.0));
        painter.drawLine(QPointF(8.0, 10.0), QPointF(11.5, 10.0));
        break;
    case IconName::Layout:
        painter.drawRoundedRect(QRectF(2.0, 2.0, 12.0, 12.0), 1.2, 1.2);
        painter.drawLine(QPointF(8.0, 2.0), QPointF(8.0, 14.0));
        painter.drawLine(QPointF(8.0, 8.0), QPointF(14.0, 8.0));
        break;
    case IconName::Search:
        painter.drawEllipse(QRectF(2.0, 2.0, 8.5, 8.5));
        painter.drawLine(QPointF(9.2, 9.2), QPointF(14.0, 14.0));
        break;
    case IconName::Settings:
        painter.drawEllipse(QRectF(5.3, 5.3, 5.4, 5.4));
        painter.drawEllipse(QRectF(2.2, 2.2, 11.6, 11.6));
        painter.drawLine(QPointF(8.0, 0.8), QPointF(8.0, 3.0));
        painter.drawLine(QPointF(8.0, 13.0), QPointF(8.0, 15.2));
        painter.drawLine(QPointF(0.8, 8.0), QPointF(3.0, 8.0));
        painter.drawLine(QPointF(13.0, 8.0), QPointF(15.2, 8.0));
        break;
    case IconName::Close:
        painter.drawLine(QPointF(4.0, 4.0), QPointF(12.0, 12.0));
        painter.drawLine(QPointF(12.0, 4.0), QPointF(4.0, 12.0));
        break;
    case IconName::More:
        painter.setBrush(pen.color());
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(3.0, 8.0), 1.0, 1.0);
        painter.drawEllipse(QPointF(8.0, 8.0), 1.0, 1.0);
        painter.drawEllipse(QPointF(13.0, 8.0), 1.0, 1.0);
        break;
    case IconName::Focus:
        painter.drawLine(QPointF(2.0, 6.0), QPointF(2.0, 2.0));
        painter.drawLine(QPointF(2.0, 2.0), QPointF(6.0, 2.0));
        painter.drawLine(QPointF(10.0, 2.0), QPointF(14.0, 2.0));
        painter.drawLine(QPointF(14.0, 2.0), QPointF(14.0, 6.0));
        painter.drawLine(QPointF(14.0, 10.0), QPointF(14.0, 14.0));
        painter.drawLine(QPointF(14.0, 14.0), QPointF(10.0, 14.0));
        painter.drawLine(QPointF(6.0, 14.0), QPointF(2.0, 14.0));
        painter.drawLine(QPointF(2.0, 14.0), QPointF(2.0, 10.0));
        break;
    case IconName::Restore:
        painter.drawRoundedRect(QRectF(2.0, 4.0, 9.5, 9.5), 1.0, 1.0);
        painter.drawRoundedRect(QRectF(4.5, 1.5, 9.5, 9.5), 1.0, 1.0);
        break;
    case IconName::WebServer:
        painter.drawRoundedRect(QRectF(2.0, 2.5, 12.0, 4.5), 1.0, 1.0);
        painter.drawRoundedRect(QRectF(2.0, 9.0, 12.0, 4.5), 1.0, 1.0);
        painter.setBrush(pen.color());
        painter.drawEllipse(QRectF(4.0, 4.0, 1.6, 1.6));
        painter.drawEllipse(QRectF(4.0, 10.5, 1.6, 1.6));
        painter.setBrush(Qt::NoBrush);
        break;
    case IconName::Browser:
        painter.drawEllipse(QRectF(1.5, 1.5, 13.0, 13.0));
        painter.drawEllipse(QRectF(4.8, 1.5, 6.4, 13.0));
        painter.drawLine(QPointF(2.0, 6.0), QPointF(14.0, 6.0));
        painter.drawLine(QPointF(2.0, 10.0), QPointF(14.0, 10.0));
        break;
    case IconName::Start:
        painter.setBrush(pen.color());
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF({QPointF(4.5, 2.8), QPointF(13.0, 8.0), QPointF(4.5, 13.2)}));
        break;
    case IconName::Stop:
        painter.setBrush(pen.color());
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(3.5, 3.5, 9.0, 9.0), 1.0, 1.0);
        break;
    case IconName::Back:
        painter.drawLine(QPointF(13.0, 8.0), QPointF(3.0, 8.0));
        painter.drawLine(QPointF(3.0, 8.0), QPointF(7.0, 4.0));
        painter.drawLine(QPointF(3.0, 8.0), QPointF(7.0, 12.0));
        break;
    case IconName::Forward:
        painter.drawLine(QPointF(3.0, 8.0), QPointF(13.0, 8.0));
        painter.drawLine(QPointF(13.0, 8.0), QPointF(9.0, 4.0));
        painter.drawLine(QPointF(13.0, 8.0), QPointF(9.0, 12.0));
        break;
    case IconName::Home:
        painter.drawPolyline(QPolygonF{QPointF(2.0, 7.5), QPointF(8.0, 2.0), QPointF(14.0, 7.5)});
        painter.drawRoundedRect(QRectF(3.5, 7.0, 9.0, 7.0), 1.0, 1.0);
        painter.drawRect(QRectF(7.0, 10.0, 2.0, 4.0));
        break;
    case IconName::Shelf:
        painter.drawRoundedRect(QRectF(2.0, 3.0, 12.0, 10.0), 1.0, 1.0);
        painter.drawLine(QPointF(2.0, 9.0), QPointF(6.0, 9.0));
        painter.drawLine(QPointF(6.0, 9.0), QPointF(7.5, 11.0));
        painter.drawLine(QPointF(7.5, 11.0), QPointF(8.5, 11.0));
        painter.drawLine(QPointF(8.5, 11.0), QPointF(10.0, 9.0));
        painter.drawLine(QPointF(10.0, 9.0), QPointF(14.0, 9.0));
        break;
    case IconName::Bookmark:
        painter.drawPolyline(QPolygonF{QPointF(4.0, 2.0), QPointF(12.0, 2.0), QPointF(12.0, 14.0), QPointF(8.0, 11.0), QPointF(4.0, 14.0), QPointF(4.0, 2.0)});
        break;
    case IconName::Folder:
        painter.drawPolyline(QPolygonF{QPointF(2.0, 4.0), QPointF(6.0, 4.0), QPointF(7.5, 6.0), QPointF(14.0, 6.0), QPointF(14.0, 13.0), QPointF(2.0, 13.0), QPointF(2.0, 4.0)});
        break;
    case IconName::Edit:
        painter.drawLine(QPointF(3.0, 13.0), QPointF(5.8, 12.4));
        painter.drawLine(QPointF(5.8, 12.4), QPointF(13.0, 5.2));
        painter.drawLine(QPointF(10.8, 3.0), QPointF(13.0, 5.2));
        painter.drawLine(QPointF(3.0, 13.0), QPointF(3.6, 10.2));
        painter.drawLine(QPointF(3.6, 10.2), QPointF(10.8, 3.0));
        break;
    case IconName::Refresh:
        painter.drawArc(QRectF(3.0, 3.0, 10.0, 10.0), 75 * 16, 300 * 16);
        painter.setBrush(pen.color());
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF({QPointF(12.0, 3.6), QPointF(14.5, 6.3), QPointF(11.2, 7.2)}));
        break;
    case IconName::Schedule:
        painter.drawEllipse(QRectF(2.5, 2.5, 11.0, 11.0));
        painter.drawLine(QPointF(8.0, 5.0), QPointF(8.0, 8.0));
        painter.drawLine(QPointF(8.0, 8.0), QPointF(10.5, 9.5));
        break;
    case IconName::Clear:
        painter.drawLine(QPointF(3.0, 4.5), QPointF(13.0, 4.5));
        painter.drawLine(QPointF(6.0, 2.5), QPointF(10.0, 2.5));
        painter.drawRoundedRect(QRectF(4.0, 4.5, 8.0, 9.0), 1.0, 1.0);
        painter.drawLine(QPointF(6.5, 7.0), QPointF(6.5, 11.0));
        painter.drawLine(QPointF(9.5, 7.0), QPointF(9.5, 11.0));
        break;
    case IconName::Import:
        painter.drawRoundedRect(QRectF(2.0, 9.5, 12.0, 4.0), 1.0, 1.0);
        painter.drawLine(QPointF(8.0, 2.0), QPointF(8.0, 10.0));
        painter.drawLine(QPointF(5.0, 7.0), QPointF(8.0, 10.0));
        painter.drawLine(QPointF(11.0, 7.0), QPointF(8.0, 10.0));
        break;
    case IconName::ExternalLink:
        painter.drawPolyline(QPolygonF({QPointF(12.0, 8.7), QPointF(12.0, 14.0), QPointF(2.0, 14.0), QPointF(2.0, 4.0), QPointF(7.3, 4.0)}));
        painter.drawPolyline(QPolygonF({QPointF(10.0, 2.0), QPointF(14.0, 2.0), QPointF(14.0, 6.0)}));
        painter.drawLine(QPointF(6.7, 9.3), QPointF(14.0, 2.0));
        break;
    case IconName::Export:
        painter.drawRoundedRect(QRectF(2.0, 9.5, 12.0, 4.0), 1.0, 1.0);
        painter.drawLine(QPointF(8.0, 10.0), QPointF(8.0, 2.0));
        painter.drawLine(QPointF(5.0, 5.0), QPointF(8.0, 2.0));
        painter.drawLine(QPointF(11.0, 5.0), QPointF(8.0, 2.0));
        break;
    case IconName::Logs:
        painter.drawRoundedRect(QRectF(2.0, 2.0, 12.0, 12.0), 1.5, 1.5);
        painter.drawLine(QPointF(4.5, 5.0), QPointF(11.5, 5.0));
        painter.drawLine(QPointF(4.5, 8.0), QPointF(11.5, 8.0));
        painter.drawLine(QPointF(4.5, 11.0), QPointF(9.5, 11.0));
        break;
    case IconName::Workspace:
        painter.drawRoundedRect(QRectF(2.0, 3.0, 12.0, 10.0), 1.5, 1.5);
        painter.drawLine(QPointF(6.0, 3.0), QPointF(6.0, 13.0));
        painter.drawLine(QPointF(10.0, 3.0), QPointF(10.0, 13.0));
        break;
    case IconName::Tasks:
        painter.drawPolyline(QPolygonF({QPointF(2.5, 5.0), QPointF(4.0, 6.5), QPointF(6.5, 3.5)}));
        painter.drawLine(QPointF(8.5, 5.0), QPointF(13.5, 5.0));
        painter.drawPolyline(QPolygonF({QPointF(2.5, 11.0), QPointF(4.0, 12.5), QPointF(6.5, 9.5)}));
        painter.drawLine(QPointF(8.5, 11.0), QPointF(13.5, 11.0));
        break;
    case IconName::Donate: {
        QPainterPath heart(QPointF(8.0, 13.8));
        heart.cubicTo(QPointF(6.7, 12.5), QPointF(2.0, 9.2), QPointF(2.0, 5.8));
        heart.cubicTo(QPointF(2.0, 3.6), QPointF(3.5, 2.2), QPointF(5.4, 2.2));
        heart.cubicTo(QPointF(6.6, 2.2), QPointF(7.5, 2.9), QPointF(8.0, 3.8));
        heart.cubicTo(QPointF(8.5, 2.9), QPointF(9.4, 2.2), QPointF(10.6, 2.2));
        heart.cubicTo(QPointF(12.5, 2.2), QPointF(14.0, 3.6), QPointF(14.0, 5.8));
        heart.cubicTo(QPointF(14.0, 9.2), QPointF(9.3, 12.5), QPointF(8.0, 13.8));
        painter.drawPath(heart);
        break;
    }
    case IconName::System:
        painter.drawRoundedRect(QRectF(1.5, 2.0, 13.0, 9.0), 1.5, 1.5);
        painter.drawPolyline(QPolygonF{QPointF(3.5, 8.0), QPointF(6.0, 5.5), QPointF(8.0, 7.0), QPointF(11.8, 3.8)});
        painter.drawLine(QPointF(6.0, 14.0), QPointF(10.0, 14.0));
        painter.drawLine(QPointF(8.0, 11.0), QPointF(8.0, 14.0));
        break;
    case IconName::Processor:
        painter.drawRoundedRect(QRectF(3.5, 3.5, 9.0, 9.0), 1.5, 1.5);
        painter.drawRect(QRectF(6.0, 6.0, 4.0, 4.0));
        for (const qreal position : {5.0, 8.0, 11.0}) {
            painter.drawLine(QPointF(position, 1.5), QPointF(position, 3.5));
            painter.drawLine(QPointF(position, 12.5), QPointF(position, 14.5));
            painter.drawLine(QPointF(1.5, position), QPointF(3.5, position));
            painter.drawLine(QPointF(12.5, position), QPointF(14.5, position));
        }
        break;
    case IconName::Memory:
        painter.drawRoundedRect(QRectF(1.5, 4.0, 13.0, 8.0), 1.2, 1.2);
        painter.drawRect(QRectF(3.2, 6.0, 2.2, 3.2));
        painter.drawRect(QRectF(6.2, 6.0, 2.2, 3.2));
        painter.drawRect(QRectF(9.2, 6.0, 2.2, 3.2));
        painter.drawLine(QPointF(3.5, 12.0), QPointF(3.5, 14.0));
        painter.drawLine(QPointF(6.5, 12.0), QPointF(6.5, 14.0));
        painter.drawLine(QPointF(9.5, 12.0), QPointF(9.5, 14.0));
        painter.drawLine(QPointF(12.5, 12.0), QPointF(12.5, 14.0));
        break;
    case IconName::Graphics:
        painter.drawRoundedRect(QRectF(1.5, 3.0, 13.0, 10.0), 1.2, 1.2);
        painter.drawEllipse(QRectF(4.0, 5.0, 6.0, 6.0));
        painter.drawLine(QPointF(7.0, 5.0), QPointF(7.0, 11.0));
        painter.drawLine(QPointF(4.0, 8.0), QPointF(10.0, 8.0));
        painter.drawLine(QPointF(12.0, 6.0), QPointF(14.5, 6.0));
        painter.drawLine(QPointF(12.0, 10.0), QPointF(14.5, 10.0));
        break;
    case IconName::Mainboard:
        painter.drawRoundedRect(QRectF(2.0, 2.0, 12.0, 12.0), 1.2, 1.2);
        painter.drawRect(QRectF(4.0, 4.0, 5.0, 5.0));
        painter.drawEllipse(QRectF(10.5, 4.0, 1.8, 1.8));
        painter.drawLine(QPointF(6.5, 9.0), QPointF(6.5, 12.0));
        painter.drawLine(QPointF(6.5, 12.0), QPointF(11.5, 12.0));
        painter.drawLine(QPointF(9.0, 6.5), QPointF(12.0, 6.5));
        break;
    case IconName::Storage:
        painter.drawEllipse(QRectF(2.0, 2.0, 12.0, 4.0));
        painter.drawLine(QPointF(2.0, 4.0), QPointF(2.0, 12.0));
        painter.drawLine(QPointF(14.0, 4.0), QPointF(14.0, 12.0));
        painter.drawArc(QRectF(2.0, 6.0, 12.0, 4.0), 180 * 16, 180 * 16);
        painter.drawArc(QRectF(2.0, 10.0, 12.0, 4.0), 180 * 16, 180 * 16);
        break;
    case IconName::Battery:
        painter.drawRoundedRect(QRectF(2.0, 4.0, 11.0, 8.0), 1.2, 1.2);
        painter.drawLine(QPointF(13.0, 6.5), QPointF(15.0, 6.5));
        painter.drawLine(QPointF(15.0, 6.5), QPointF(15.0, 9.5));
        painter.drawLine(QPointF(15.0, 9.5), QPointF(13.0, 9.5));
        painter.drawRect(QRectF(4.0, 6.0, 5.0, 4.0));
        break;
    case IconName::Network:
        painter.drawEllipse(QRectF(6.0, 1.5, 4.0, 4.0));
        painter.drawEllipse(QRectF(1.5, 10.5, 4.0, 4.0));
        painter.drawEllipse(QRectF(10.5, 10.5, 4.0, 4.0));
        painter.drawLine(QPointF(7.0, 5.0), QPointF(4.5, 10.8));
        painter.drawLine(QPointF(9.0, 5.0), QPointF(11.5, 10.8));
        painter.drawLine(QPointF(5.5, 12.5), QPointF(10.5, 12.5));
        break;
    case IconName::Visible:
        drawEye(painter);
        break;
    case IconName::Minus:
        painter.drawLine(QPointF(3.5, 8.0), QPointF(12.5, 8.0));
        break;
    case IconName::Hidden:
        drawEye(painter);
        painter.drawLine(QPointF(2.6, 13.4), QPointF(13.4, 2.6));
        break;
    case IconName::Tool: {
        // The wrench of the SVG Repo tool shape, whose head is one circle carrying the jaw notch and the handle.
        const QRectF head(6.0, 1.33, 8.67, 8.67);
        QPainterPath wrench;
        wrench.arcMoveTo(head, 0.0);
        wrench.arcTo(head, 0.0, -108.0);
        wrench.lineTo(4.41, 14.41);
        wrench.lineTo(1.59, 11.59);
        wrench.lineTo(6.20, 6.97);
        wrench.arcTo(head, -162.5, -133.3);
        wrench.lineTo(9.33, 4.67);
        wrench.lineTo(11.33, 6.67);
        wrench.lineTo(14.23, 3.77);
        wrench.arcTo(head, 25.9, -25.9);
        wrench.closeSubpath();
        painter.drawPath(wrench);
        break;
    }
    case IconName::Chat:
        painter.drawRoundedRect(QRectF(2.0, 3.0, 12.0, 8.5), 2.0, 2.0);
        painter.drawLine(QPointF(5.5, 11.5), QPointF(5.0, 14.0));
        painter.drawLine(QPointF(5.0, 14.0), QPointF(8.5, 11.5));
        break;
    case IconName::Spark: {
        // The three sparkles of the Octicons sparkles-fill-16 shape, which GitHub publishes under the MIT licence.
        QPainterPath sparkles;
        IconsHelper::appendSparkle(sparkles, QPointF(10.0, 8.0), 5.72);
        IconsHelper::appendSparkle(sparkles, QPointF(5.5, 13.5), 2.38);
        IconsHelper::appendSparkle(sparkles, QPointF(3.0, 3.0), 2.86);
        painter.setBrush(pen.color());
        painter.setPen(Qt::NoPen);
        painter.drawPath(sparkles);
        break;
    }
    case IconName::Person:
        painter.drawEllipse(QRectF(5.4, 2.4, 5.2, 5.2));
        painter.drawArc(QRectF(2.6, 9.4, 10.8, 9.6), 0, 180 * 16);
        break;
    case IconName::Information:
        painter.drawEllipse(QRectF(1.5, 1.5, 13.0, 13.0));
        painter.drawPoint(QPointF(8.0, 5.0));
        painter.drawLine(QPointF(8.0, 7.5), QPointF(8.0, 11.5));
        break;
    case IconName::Success:
        painter.drawEllipse(QRectF(1.5, 1.5, 13.0, 13.0));
        painter.drawLine(QPointF(4.5, 8.0), QPointF(7.0, 10.5));
        painter.drawLine(QPointF(7.0, 10.5), QPointF(11.8, 5.5));
        break;
    case IconName::Warning:
        painter.drawPolygon(QPolygonF{QPointF(8.0, 1.5), QPointF(14.5, 13.5), QPointF(1.5, 13.5)});
        painter.drawLine(QPointF(8.0, 5.0), QPointF(8.0, 9.0));
        painter.drawPoint(QPointF(8.0, 11.5));
        break;
    case IconName::Error:
        painter.drawEllipse(QRectF(1.5, 1.5, 13.0, 13.0));
        painter.drawLine(QPointF(5.0, 5.0), QPointF(11.0, 11.0));
        painter.drawLine(QPointF(11.0, 5.0), QPointF(5.0, 11.0));
        break;
    case IconName::Bell:
        painter.drawPolyline(QPolygonF{QPointF(3.5, 11.0), QPointF(3.5, 7.0), QPointF(4.6, 4.2), QPointF(8.0, 3.0), QPointF(11.4, 4.2), QPointF(12.5, 7.0), QPointF(12.5, 11.0)});
        painter.drawLine(QPointF(2.5, 11.0), QPointF(13.5, 11.0));
        painter.drawLine(QPointF(8.0, 1.5), QPointF(8.0, 3.0));
        painter.drawArc(QRectF(6.3, 11.5, 3.4, 3.0), 0, -180 * 16);
        break;
    }
}

QVector<IconName> IconCatalog::allIconNames() {
    return {IconName::Add, IconName::Terminal, IconName::Layout, IconName::Search, IconName::Settings, IconName::Close, IconName::More, IconName::Focus, IconName::Restore, IconName::WebServer, IconName::Browser, IconName::Start, IconName::Stop, IconName::Back, IconName::Forward, IconName::Home, IconName::Shelf, IconName::Bookmark, IconName::Folder, IconName::Edit, IconName::Refresh, IconName::Clear, IconName::Import, IconName::Export, IconName::ExternalLink, IconName::Logs, IconName::Tasks, IconName::Workspace, IconName::Donate, IconName::System, IconName::Processor, IconName::Memory, IconName::Graphics, IconName::Mainboard, IconName::Storage, IconName::Battery, IconName::Network, IconName::Information, IconName::Success, IconName::Warning, IconName::Error, IconName::Visible, IconName::Hidden, IconName::Minus, IconName::Schedule, IconName::Bell, IconName::Spark, IconName::Person, IconName::Tool, IconName::Chat};
}

QIcon IconCatalog::icon(IconName name, const Theme& theme) {
    return QIcon(new IconEngine(name, theme.color(ThemeColor::TextMuted), theme.color(ThemeColor::Text)));
}

QIcon IconCatalog::icon(IconName name, const QColor& color) {
    return QIcon(new IconEngine(name, color, color));
}

QIcon IconCatalog::primaryIcon(IconName name, const Theme& theme) {
    return IconCatalog::icon(name, theme.color(ThemeColor::OnAccent));
}

// An icon on a filled destructive surface reads in the on-danger color, exactly as an accent button reads in the on-accent one.
QIcon IconCatalog::destructiveIcon(IconName name, const Theme& theme) {
    return IconCatalog::icon(name, theme.color(ThemeColor::OnDanger));
}

QIcon IconCatalog::layoutIcon(const QString& presetId, int columns, int rows, int slotCount, const Theme& theme) {
    return QIcon(new LayoutIconEngine(presetId, columns, rows, slotCount, theme.color(ThemeColor::Text), theme.color(ThemeColor::TextMuted), theme.color(ThemeColor::Accent), theme.color(ThemeColor::Hover), theme.color(ThemeColor::Pressed)));
}

} // namespace workpane::ui
