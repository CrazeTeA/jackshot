#include "Canvas.h"
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

Canvas::Canvas(const QImage &img, QWidget *parent) : QGraphicsView(parent), base_(img) {
    auto *scene = new QGraphicsScene(this);
    auto *bg = scene->addPixmap(QPixmap::fromImage(img));
    bg->setZValue(-1);
    scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(img.size())));
    setScene(scene);
    setRenderHint(QPainter::Antialiasing);
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::CrossCursor);
    setAlignment(Qt::AlignCenter);
}

QPen Canvas::pen() const {
    QPen p(color_, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return p;
}

QPainterPath Canvas::arrowPath(const QPointF &a, const QPointF &b) {
    QPainterPath path(a);
    path.lineTo(b);
    const double ang = std::atan2(b.y() - a.y(), b.x() - a.x());
    const double len = 18, spread = M_PI / 7;       // длина и раствор «головы»
    for (int s : {-1, 1}) {
        QPointF h(b.x() - len * std::cos(ang + s * spread),
                  b.y() - len * std::sin(ang + s * spread));
        path.moveTo(b);
        path.lineTo(h);
    }
    return path;
}

void Canvas::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return QGraphicsView::mousePressEvent(e);
    start_ = mapToScene(e->pos());
    switch (tool_) {
    case Sticker: addSticker(start_); return;
    case Text:    addText(start_);    return;
    default: break;
    }
    current_ = scene()->addPath(QPainterPath(start_), pen());
    if (tool_ == Pen) { QPainterPath p(start_); current_->setPath(p); }
}

void Canvas::mouseMoveEvent(QMouseEvent *e) {
    if (!current_) return QGraphicsView::mouseMoveEvent(e);
    const QPointF pos = mapToScene(e->pos());
    switch (tool_) {
    case Arrow: current_->setPath(arrowPath(start_, pos)); break;
    case Rect: {
        QPainterPath p; p.addRect(QRectF(start_, pos).normalized());
        current_->setPath(p); break;
    }
    case Pen: {
        QPainterPath p = current_->path(); p.lineTo(pos);
        current_->setPath(p); break;
    }
    default: break;
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (current_) { stack_.append(current_); current_ = nullptr; }
    else QGraphicsView::mouseReleaseEvent(e);
}

// Нумерованный стикер: цветной круг с числом.
void Canvas::addSticker(const QPointF &pos) {
    const double r = 16;
    auto *circle = scene()->addEllipse(-r, -r, 2 * r, 2 * r, QPen(Qt::white, 2), QBrush(color_));
    circle->setPos(pos);
    auto *txt = new QGraphicsSimpleTextItem(QString::number(stickerNo_++), circle);
    QFont f; f.setBold(true); f.setPointSize(13);
    txt->setFont(f);
    txt->setBrush(Qt::white);
    txt->setPos(-txt->boundingRect().width() / 2, -txt->boundingRect().height() / 2);
    stack_.append(circle);
}

void Canvas::addText(const QPointF &pos) {
    bool ok = false;
    QString s = QInputDialog::getText(this, "Текст", "Надпись:", QLineEdit::Normal, "", &ok);
    if (!ok || s.isEmpty()) return;
    auto *t = scene()->addSimpleText(s);
    QFont f; f.setBold(true); f.setPointSize(18);
    t->setFont(f);
    t->setBrush(color_);
    t->setPen(QPen(Qt::black, 0.8));
    t->setPos(pos);
    stack_.append(t);
}

void Canvas::undo() {
    if (stack_.isEmpty()) return;
    QGraphicsItem *it = stack_.takeLast();
    scene()->removeItem(it);
    delete it;
}

QImage Canvas::result() {
    QImage out(base_.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    scene()->render(&p, QRectF(), scene()->sceneRect());
    return out;
}