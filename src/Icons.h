#pragma once
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

// Простые векторные значки, нарисованные кодом (без внешних файлов).
namespace icons {

enum class Kind {
    Select, Rect, Ellipse, Line, Arrow, Pen, Highlight, Text, Step, Sticker, Magnify,
    Blur, Pixelate, Spotlight, Undo, Redo, Save, Copy, Upload, Gear, Close
};

inline QIcon make(Kind k, const QColor &c = QColor(205, 214, 244)) {
    const int S = 24, R = 2;
    QPixmap pm(S * R, S * R);
    pm.setDevicePixelRatio(R);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (k) {
    case Kind::Select: {
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawRect(QRectF(4, 5, 16, 14));
        break;
    }
    case Kind::Rect:
        p.drawRect(QRectF(4, 6, 16, 12));
        break;
    case Kind::Ellipse:
        p.drawEllipse(QRectF(4, 6, 16, 12));
        break;
    case Kind::Line:
        p.drawLine(QPointF(5, 19), QPointF(19, 5));
        break;
    case Kind::Arrow:
        p.drawLine(QPointF(5, 19), QPointF(19, 5));
        p.drawLine(QPointF(19, 5), QPointF(12, 6));
        p.drawLine(QPointF(19, 5), QPointF(18, 12));
        break;
    case Kind::Pen: {
        QPainterPath path(QPointF(4, 16));
        path.cubicTo(QPointF(8, 4), QPointF(12, 24), QPointF(20, 8));
        p.drawPath(path);
        break;
    }
    case Kind::Highlight: {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 200, 0, 210));
        p.drawRoundedRect(QRectF(3, 8, 18, 9), 2, 2);
        p.setPen(QPen(c, 1.4));
        p.drawLine(QPointF(6, 12.5), QPointF(18, 12.5));
        break;
    }
    case Kind::Text: {
        QFont f;
        f.setBold(true);
        f.setPixelSize(19);
        p.setFont(f);
        p.drawText(QRectF(0, 0, S, S), Qt::AlignCenter, "A");
        break;
    }
    case Kind::Step: {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        QFont f;
        f.setBold(true);
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(QRectF(3, 3, 18, 18), Qt::AlignCenter, "1");
        break;
    }
    case Kind::Sticker: {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(9, 10), 1.5, 1.5);
        p.drawEllipse(QPointF(15, 10), 1.5, 1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(7, 8, 10, 9), 200 * 16, 140 * 16);
        break;
    }
    case Kind::Magnify:
        p.drawEllipse(QRectF(4, 4, 12, 12));
        p.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
        break;
    case Kind::Blur: {
        for (int i = 0; i < 3; ++i) {
            p.setPen(QPen(QColor(c.red(), c.green(), c.blue(), 255 - i * 80), 1.6));
            p.drawEllipse(QPointF(12, 12), 3.0 + i * 3.2, 3.0 + i * 3.2);
        }
        break;
    }
    case Kind::Pixelate: {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                if ((x + y) % 2 == 0) p.drawRect(QRectF(4 + x * 5.3, 4 + y * 5.3, 5.3, 5.3));
        break;
    }
    case Kind::Spotlight: {
        QPainterPath outer;
        outer.addRect(QRectF(3, 4, 18, 16));
        QPainterPath inner;
        inner.addRoundedRect(QRectF(8, 8.5, 8, 7), 1.5, 1.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 110));
        p.drawPath(outer.subtracted(inner));
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        p.drawRoundedRect(QRectF(8, 8.5, 8, 7), 1.5, 1.5);
        break;
    }
    case Kind::Redo: {
        QPainterPath path(QPointF(6, 18));
        path.cubicTo(QPointF(5, 9), QPointF(13, 8), QPointF(17, 10));
        p.drawPath(path);
        p.drawLine(QPointF(17, 10), QPointF(13, 6));
        p.drawLine(QPointF(17, 10), QPointF(13, 14));
        break;
    }
    case Kind::Undo: {
        QPainterPath path(QPointF(18, 18));
        path.cubicTo(QPointF(19, 9), QPointF(11, 8), QPointF(7, 10));
        p.drawPath(path);
        p.drawLine(QPointF(7, 10), QPointF(11, 6));
        p.drawLine(QPointF(7, 10), QPointF(11, 14));
        break;
    }
    case Kind::Save:
        p.drawRect(QRectF(4, 4, 16, 16));
        p.drawRect(QRectF(8, 4, 8, 5));
        p.drawRect(QRectF(7, 13, 10, 7));
        break;
    case Kind::Copy:
        p.drawRect(QRectF(9, 9, 11, 11));
        p.drawRect(QRectF(4, 4, 11, 11));
        break;
    case Kind::Upload: {
        p.drawLine(QPointF(12, 16), QPointF(12, 5));
        p.drawLine(QPointF(12, 5), QPointF(8, 9));
        p.drawLine(QPointF(12, 5), QPointF(16, 9));
        QPainterPath tray(QPointF(5, 15));
        tray.lineTo(5, 19);
        tray.lineTo(19, 19);
        tray.lineTo(19, 15);
        p.drawPath(tray);
        break;
    }
    case Kind::Gear: {
        p.drawEllipse(QPointF(12, 12), 5.2, 5.2);
        p.drawEllipse(QPointF(12, 12), 1.8, 1.8);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 3.14159265358979 / 4;
            p.drawLine(QPointF(12 + 6.5 * std::cos(a), 12 + 6.5 * std::sin(a)),
                       QPointF(12 + 9.5 * std::cos(a), 12 + 9.5 * std::sin(a)));
        }
        break;
    }
    case Kind::Close:
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        break;
    }
    p.end();
    return QIcon(pm);
}

} // namespace icons