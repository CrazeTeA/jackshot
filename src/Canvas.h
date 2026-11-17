#pragma once
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QImage>
#include <QList>

// Холст: фон = снимок, поверх — аннотации (стрелка, рамка, кисть, стикер, текст).
class Canvas : public QGraphicsView {
    Q_OBJECT
public:
    enum Tool { Arrow, Rect, Pen, Sticker, Text };
    explicit Canvas(const QImage &img, QWidget *parent = nullptr);

    void setTool(Tool t) { tool_ = t; }
    void setColor(const QColor &c) { color_ = c; }
    void undo();
    QImage result();                 // финальное изображение с аннотациями

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QPen pen() const;
    void addSticker(const QPointF &pos);
    void addText(const QPointF &pos);
    static QPainterPath arrowPath(const QPointF &a, const QPointF &b);

    Tool tool_ = Arrow;
    QColor color_ = QColor(255, 60, 60);
    QGraphicsPathItem *current_ = nullptr;
    QPointF start_;
    QList<QGraphicsItem *> stack_;   // для отмены
    QImage base_;
    int stickerNo_ = 1;
};