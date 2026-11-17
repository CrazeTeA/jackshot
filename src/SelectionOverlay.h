#pragma once
#include <QColor>
#include <QImage>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>
#include <QWidget>

class QBoxLayout;
class QFrame;
class QLineEdit;
class QPainter;
class QScreen;
class QToolButton;

// Полноэкранный слой с замороженным снимком одного монитора:
// выбор области + аннотации + перетаскиваемая панель инструментов.
class SelectionOverlay : public QWidget {
    Q_OBJECT
public:
    // Image — только объект (вставка из буфера), кнопки-инструмента у него нет.
    // Eraser — инструмент и одновременно штрих ластика в списке объектов.
    enum Tool {
        Select, Rect, Ellipse, Line, Arrow, Pen, Highlight, Text, Balloon, Step, Sticker,
        Magnify, Blur, Pixelate, Eraser, Image, Spotlight
    };
    enum Action { Save, Copy, Upload, SaveCopy };

    SelectionOverlay(QScreen *screen, const QImage &shot, bool direct,
                     QWidget *parent = nullptr);
    void start();
    void presetRegion(const QRect &r); // готовая рамка (повтор последней области), до start()

signals:
    void activated();                                            // первый клик на этом мониторе
    void finished(const QImage &img, SelectionOverlay::Action action);
    void cancelled();

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    enum class DragMode { None, Move, Resize, Rotate, EndA, EndB };

    struct Item {
        Tool tool = Arrow;
        QPointF a, b; // для картинки и фигур-прямоугольников: два угла
        QVector<QPointF> pts; // кисть и ластик: точки штриха
        QColor color;
        QString text; // текст, выноска, эмодзи или номер шага
        QImage image; // вставленная картинка
        double size = 0; // текст/выноска: высота шрифта; стикер/шаг: сторона; ластик: толщина
        double angle = 0; // поворот в градусах (только для rotatable)
    };

    struct Handles {
        QRectF move, resize, del, rot, endA, endB, frame;
        QPolygonF quad; // рамка с учётом поворота
        bool line = false;
        bool hasRot = false;
    };

    struct Snap { // снимок состояния для отмены
        QList<Item> items;
        int step = 1;
    };

    // панель (OverlayBars.cpp)
    void buildToolbar();
    void buildEmojiBar();
    void layoutBars();
    void placeEmojiBar();
    void saveBarPos();
    void setBarOrientation(bool horizontal);
    void updateBarOrientation(const QPoint &pointer);
    void updateColorIcon();
    void openSettings();

    // логика слоя (SelectionOverlay.cpp)
    void addExtraButtons();
    void setTool(Tool t);
    void cycleColor();
    void pushHistory();
    void undo();
    void redo();
    void deleteHovered();
    void pasteImage();
    void openText(const QPointF &pos, const QString &initial = QString(),
                  const QPointF &offset = QPointF());
    void commitText();
    void finish(Action a);

    static bool rotatable(Tool t);
    QTransform itemTransform(const Item &it) const;
    void paintItems(QPainter &p) const;
    void drawItem(QPainter &p, const Item &it) const;
    void drawShape(QPainter &p, const Item &it) const;
    void drawEffect(QPainter &p, const QRectF &r, bool pixelate) const;
    void drawLens(QPainter &p, const QRectF &r, const QColor &ring) const;
    void drawHandles(QPainter &p, const Item &it) const;
    QRectF itemBounds(const Item &it) const;
    Handles handlesFor(const Item &it) const;
    bool inHover(const Item &it, const QPointF &pos) const;
    int hitTest(const QPointF &pos) const;
    void updateHover(const QPointF &pos);
    void applyDrag(const QPointF &pos);

    // кэш слоя аннотаций и частичная перерисовка (быстрый ластик)
    void ensureLayer();
    void eraseSeg(const QPointF &a, const QPointF &b, double size);
    void updateRect(const QPointF &a, const QPointF &b, double pad);

    QRect selection() const;
        enum SelEdge { EdgeL = 1, EdgeR = 2, EdgeT = 4, EdgeB = 8, EdgeMove = 16 };
    int selHit(const QPoint &pos) const;
    static Qt::CursorShape selCursor(int mode);
    void applySelDrag(const QPoint &pos);
    void nudgeSelection(int dx, int dy, bool resize);
    QRect sourceRect(const QRect &logical) const;
    QImage render() const;

    QScreen *screen_;
    QImage shot_;
    bool direct_;

    // настройки рисования (читаются один раз при создании слоя)
    QList<QColor> palette_;
    int lineW_ = 4;
    int arrowLen_ = 18;
    int hlAlpha_ = 100;
    double zoom_ = 2.0;

    Tool tool_ = Select;
    QColor color_;
    int colorIdx_ = 0;
    int stepNo_ = 1;
    QString emoji_;

    QList<Item> items_;
    QList<Snap> history_;
    QList<Snap> redo_; // отменённые состояния (для Ctrl+Shift+Z)
    Item cur_;
    bool drawing_ = false;

    bool erasing_ = false;
    double eraserSize_ = 24;
    QPointF mousePos_ = QPointF(-1000, -1000);
    QImage layer_;           // готовый слой аннотаций (нужен, пока есть штрихи ластика)
    bool layerDirty_ = true; // слой устарел и будет пересобран при ближайшей отрисовке

    // наведение и перетаскивание ручек
    int hovered_ = -1;
    int selIdx_ = -1;
    DragMode dragMode_ = DragMode::None;
    Item dragOrig_;
    QPointF dragStart_;

    QPoint a_, b_;
    QRect sel_;
    bool dragging_ = false;
    bool activated_ = false;
    bool toolRestored_ = false; // последний инструмент уже подставлен после выделения
        int selDrag_ = 0;      // что тянем у рамки выделения (комбинация SelEdge), 0 — ничего
    QRect selOrig_;        // рамка в момент начала перетаскивания
    QPoint selStart_;      // где нажали

    // ввод текста
    QPointF textPos_;
    int editIdx_ = -1;         // индекс редактируемого объекта, -1 — новый
    bool balloonEdit_ = false; // новый объект — выноска, а не текст

    bool barPlaced_ = false;
    bool horizontal_ = false;
    QWidget *toolbar_ = nullptr;
    QBoxLayout *barLay_ = nullptr;
    QWidget *grip_ = nullptr;
    QList<QFrame *> seps_;
    QWidget *emojiBar_ = nullptr;
    QLineEdit *textEdit_ = nullptr;
    QToolButton *colorBtn_ = nullptr;
    QMap<int, QToolButton *> toolBtns_;
};