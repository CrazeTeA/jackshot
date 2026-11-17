// Логика слоя: рисование, ручки объектов, события мыши и клавиатуры.
#include "SelectionOverlay.h"
#include "Capture.h"
#include "Settings.h"

#include <LayerShellQt/Window>
#include <QBoxLayout>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QToolButton>
#include <QWheelEvent>
#include <QWindow>
#include <cmath>

namespace {
const double kPi = 3.14159265358979323846;
const double kTextSize = 22;
const double kBalloonSize = 18;
const double kStickerSize = 56;
const double kStepSize = 36;
const int kMinSel = 8; // минимальная сторона рамки выделения

// Высота хвоста выноски зависит от размера шрифта.
double balloonTail(double size) { return qMax(8.0, size * 0.65); }

double segDist(const QPointF &p, const QPointF &a, const QPointF &b) {
    const QPointF ab = b - a;
    const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = 0;
    if (l2 > 0)
        t = qBound(0.0, ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / l2, 1.0);
    const QPointF c = a + ab * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

QFont textFont(double size) {
    QFont f;
    f.setBold(true);
    f.setPixelSize(qMax(6, qRound(size)));
    return f;
}

// Значки дополнительных кнопок панели (рисуются кодом, как в Icons.h).
QPixmap iconCanvas() {
    QPixmap pm(48, 48);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    return pm;
}

QIcon balloonIcon(const QColor &c) {
    QPixmap pm = iconCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath bubble;
    bubble.addRoundedRect(QRectF(3.5, 4.5, 17, 11), 3, 3);
    QPainterPath tail;
    tail.moveTo(8, 15.5);
    tail.lineTo(6, 20.5);
    tail.lineTo(13, 15.5);
    p.drawPath(bubble.united(tail));
    p.end();
    return QIcon(pm);
}

QIcon eraserIcon(const QColor &c) {
    QPixmap pm = iconCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.save();
    p.translate(12, 11);
    p.rotate(-45);
    p.drawRoundedRect(QRectF(-8, -4.5, 16, 9), 2, 2);
    p.drawLine(QPointF(-1.5, -4.5), QPointF(-1.5, 4.5));
    p.restore();
    p.drawLine(QPointF(5, 20), QPointF(20, 20));
    p.end();
    return QIcon(pm);
}

QIcon pasteIcon(const QColor &c) {
    QPixmap pm = iconCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(5.5, 5.5, 13, 15), 2, 2);
    p.drawRect(QRectF(9, 3.5, 6, 4));
    p.drawLine(QPointF(8.5, 12), QPointF(15.5, 12));
    p.drawLine(QPointF(8.5, 16), QPointF(13, 16));
    p.end();
    return QIcon(pm);
}
} // namespace

SelectionOverlay::SelectionOverlay(QScreen *screen, const QImage &shot, bool direct,
                                   QWidget *parent)
    : QWidget(parent), screen_(screen), shot_(shot), direct_(direct) {
    palette_ = settings::palette();
    color_ = palette_.first();
    lineW_ = settings::lineWidth();
    arrowLen_ = settings::arrowHead();
    hlAlpha_ = settings::highlightAlpha();
    zoom_ = settings::magnifyZoom();

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_NativeWindow);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);

    textEdit_ = new QLineEdit(this);
    textEdit_->setStyleSheet(
        "QLineEdit{background:rgba(0,0,0,170);color:white;border:1px solid #89b4fa;"
        "font-size:20px;font-weight:bold;padding:2px 6px;}");
    textEdit_->hide();
    textEdit_->installEventFilter(this); // Enter и Esc в поле обрабатываем сами

    buildToolbar();
    addExtraButtons();
    buildEmojiBar();
    updateColorIcon();
    setTool(Select);
    if (direct_) {
        toolbar_->hide();
        emojiBar_->hide();
    }
}

// Enter завершает ввод, Esc отменяет; до остального слоя эти клавиши не доходят
// (иначе Enter ещё и запускал бы «Копировать»).
bool SelectionOverlay::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == textEdit_ && ev->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(ev)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            commitText();
            return true;
        }
        if (key == Qt::Key_Escape) {
            textEdit_->hide();
            editIdx_ = -1;
            balloonEdit_ = false;
            setFocus();
            update();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// Выноска, ластик и вставка картинки: кнопки встают рядом с родственными инструментами.
void SelectionOverlay::addExtraButtons() {
    QColor ic(settings::iconColor());
    if (!ic.isValid()) ic = QColor(205, 214, 244);

    auto make = [&](const QIcon &icon, const QString &tip, bool checkable) {
        auto *b = new QToolButton(toolbar_);
        b->setIcon(icon);
        b->setIconSize(QSize(22, 22));
        b->setFixedSize(38, 38);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    auto insertAfter = [&](Tool anchor, QToolButton *b) {
        QToolButton *after = toolBtns_.value(int(anchor), nullptr);
        barLay_->insertWidget(after ? barLay_->indexOf(after) + 1 : barLay_->count(), b);
    };

    QToolButton *balloon = make(balloonIcon(ic), "Выноска (O)", true);
    toolBtns_.insert(int(Balloon), balloon);
    connect(balloon, &QToolButton::clicked, this, [this] { setTool(Balloon); });
    insertAfter(Text, balloon);

    QToolButton *eraser = make(eraserIcon(ic), "Ластик (D), размер — колёсиком", true);
    toolBtns_.insert(int(Eraser), eraser);
    connect(eraser, &QToolButton::clicked, this, [this] { setTool(Eraser); });
    insertAfter(Pixelate, eraser);

    QToolButton *paste = make(pasteIcon(ic), "Вставить картинку из буфера (Ctrl+V)", false);
    connect(paste, &QToolButton::clicked, this, &SelectionOverlay::pasteImage);
    barLay_->insertWidget(barLay_->indexOf(eraser) + 1, paste);
}

void SelectionOverlay::start() {
    create();
    windowHandle()->setScreen(screen_);

    auto *ls = LayerShellQt::Window::get(windowHandle());
    ls->setScope("jackshot");
    ls->setLayer(LayerShellQt::Window::LayerOverlay);
    ls->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
        LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    ls->setExclusiveZone(-1);
    ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);

    show();
    setFocus();
}

// Подставляет готовую рамку выделения (она ограничивается размером монитора).
void SelectionOverlay::presetRegion(const QRect &r) {
    const QRect c = r.intersected(QRect(QPoint(0, 0), screen_->size()));
    if (c.width() >= kMinSel && c.height() >= kMinSel) sel_ = c;
}

void SelectionOverlay::setTool(Tool t) {
    commitText();
    tool_ = t;
    if (!direct_ && t != Select && t != Image) settings::setLastTool(int(t)); // для «запомнить инструмент»
    hovered_ = selIdx_ = -1;
    dragMode_ = DragMode::None;
    erasing_ = false;
    for (auto it = toolBtns_.begin(); it != toolBtns_.end(); ++it)
        it.value()->setChecked(it.key() == int(t));
    emojiBar_->setVisible(t == Sticker && !direct_);
    if (barPlaced_) placeEmojiBar();
    setCursor(t == Eraser ? Qt::BlankCursor : Qt::CrossCursor); // у ластика круг вместо курсора
    update();
}

void SelectionOverlay::cycleColor() {
    colorIdx_ = (colorIdx_ + 1) % palette_.size();
    color_ = palette_.at(colorIdx_);
    updateColorIcon();
}

// Снимок состояния перед любым изменением объектов (заодно слой аннотаций устаревает).
void SelectionOverlay::pushHistory() {
    history_.append({items_, stepNo_});
    if (history_.size() > 100) history_.removeFirst();
    redo_.clear(); // новое действие обнуляет «вернуть»
    layerDirty_ = true;
}

void SelectionOverlay::undo() {
    if (textEdit_->isVisible()) { // сначала закрываем ввод текста
        textEdit_->hide();
        editIdx_ = -1;
        balloonEdit_ = false;
        setFocus();
        update();
        return;
    }
    if (history_.isEmpty()) return;
    redo_.append({items_, stepNo_});
    const Snap s = history_.takeLast();
    items_ = s.items;
    stepNo_ = s.step;
    hovered_ = selIdx_ = -1;
    layerDirty_ = true;
    update();
}

void SelectionOverlay::redo() {
    if (redo_.isEmpty()) return;
    history_.append({items_, stepNo_});
    const Snap s = redo_.takeLast();
    items_ = s.items;
    stepNo_ = s.step;
    hovered_ = selIdx_ = -1;
    layerDirty_ = true;
    update();
}

void SelectionOverlay::deleteHovered() {
    const int i = hovered_ >= 0 ? hovered_ : selIdx_;
    if (i >= 0 && i < items_.size()) {
        pushHistory();
        items_.removeAt(i);
    }
    hovered_ = selIdx_ = -1;
    update();
}

// Картинка из буфера обмена (wl-paste) становится объектом, который можно двигать и растягивать.
void SelectionOverlay::pasteImage() {
    commitText();
    QString err;
    const QByteArray data = capture::run("wl-paste", {"--type", "image/png"}, &err, 5000);
    QImage img;
    if (!data.isEmpty()) img.loadFromData(data);
    if (img.isNull()) {
        QProcess::startDetached("notify-send", {"-a", "jackshot", "jackshot",
                                                "В буфере обмена нет картинки"});
        return;
    }

    double w = img.width(), h = img.height();
    const double k = qMin(1.0, qMin(width() * 0.6 / w, height() * 0.6 / h));
    w *= k;
    h *= k;
    const QRect area = sel_.isNull() ? rect() : sel_;

    pushHistory();
    Item it;
    it.tool = Image;
    it.image = img;
    it.color = color_;
    it.a = QPointF(area.center().x() - w / 2, area.center().y() - h / 2);
    it.b = it.a + QPointF(w, h);
    items_ << it;
    hovered_ = selIdx_ = -1;
    update();
}

// pos — место объекта, offset — смещение поля ввода (у выноски текст внутри блока).
void SelectionOverlay::openText(const QPointF &pos, const QString &initial,
                                const QPointF &offset) {
    textPos_ = pos;
    textEdit_->setText(initial);
    textEdit_->move((pos + offset).toPoint());
    textEdit_->resize(260, 34);
    textEdit_->show();
    textEdit_->raise();
    textEdit_->setFocus();
    textEdit_->selectAll();
}

void SelectionOverlay::commitText() {
    if (!textEdit_ || !textEdit_->isVisible()) return;
    const QString s = textEdit_->text().trimmed();
    textEdit_->hide();

    if (editIdx_ >= 0) { // правка существующего объекта
        // шаг в истории добавляем только если текст действительно изменился
        if (editIdx_ < items_.size() && !s.isEmpty() && s != items_.at(editIdx_).text) {
            pushHistory();
            items_[editIdx_].text = s;
        }
        editIdx_ = -1;
    } else if (!s.isEmpty()) {
        pushHistory();
        Item it;
        it.a = textPos_;
        it.color = color_;
        it.text = s;
        it.tool = balloonEdit_ ? Balloon : Text;
        it.size = balloonEdit_ ? kBalloonSize : kTextSize;
        items_ << it;
    }
    balloonEdit_ = false;
    setFocus();
    update();
}

void SelectionOverlay::finish(Action a) {
    commitText();
    hovered_ = selIdx_ = -1;
    if (!sel_.isNull()) settings::setLastRegion(screen_->name(), sel_); // для `--last`
    emit finished(render(), a);
}

// Что под курсором у рамки выделения: край, угол или внутренность (перенос).
int SelectionOverlay::selHit(const QPoint &pos) const {
    if (direct_ || sel_.isNull()) return 0;
    const int g = 9; // допуск попадания в край, px
    const int x0 = sel_.left(), x1 = sel_.right() + 1;
    const int y0 = sel_.top(), y1 = sel_.bottom() + 1;
    const bool inX = pos.x() >= x0 - g && pos.x() <= x1 + g;
    const bool inY = pos.y() >= y0 - g && pos.y() <= y1 + g;
    int m = 0;
    if (inY) {
        if (qAbs(pos.x() - x0) <= g) m |= EdgeL;
        else if (qAbs(pos.x() - x1) <= g) m |= EdgeR;
    }
    if (inX) {
        if (qAbs(pos.y() - y0) <= g) m |= EdgeT;
        else if (qAbs(pos.y() - y1) <= g) m |= EdgeB;
    }
    if (m) return m;
    return sel_.contains(pos) ? EdgeMove : 0;
}

Qt::CursorShape SelectionOverlay::selCursor(int m) {
    if (m == 0) return Qt::CrossCursor;
    if (m & EdgeMove) return Qt::SizeAllCursor;
    const bool h = m & (EdgeL | EdgeR), v = m & (EdgeT | EdgeB);
    if (h && v) {
        const bool diag = ((m & EdgeL) && (m & EdgeT)) || ((m & EdgeR) && (m & EdgeB));
        return diag ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    }
    return h ? Qt::SizeHorCursor : Qt::SizeVerCursor;
}

// Перетаскивание края, угла или всей рамки выделения.
void SelectionOverlay::applySelDrag(const QPoint &pos) {
    const QPoint d = pos - selStart_;
    QRect r = selOrig_;
    if (selDrag_ & EdgeMove) {
        r.translate(d);
        r.moveLeft(qBound(0, r.left(), width() - r.width()));
        r.moveTop(qBound(0, r.top(), height() - r.height()));
    } else {
        int l = r.left(), t = r.top(), rr = r.right() + 1, b = r.bottom() + 1;
        if (selDrag_ & EdgeL) l = qBound(0, l + d.x(), rr - kMinSel);
        if (selDrag_ & EdgeR) rr = qBound(l + kMinSel, rr + d.x(), width());
        if (selDrag_ & EdgeT) t = qBound(0, t + d.y(), b - kMinSel);
        if (selDrag_ & EdgeB) b = qBound(t + kMinSel, b + d.y(), height());
        r = QRect(l, t, rr - l, b - t);
    }
    sel_ = r;
}

// Стрелки: сдвиг рамки; с Alt — изменение размера (правый и нижний край).
void SelectionOverlay::nudgeSelection(int dx, int dy, bool resize) {
    if (direct_ || sel_.isNull()) return;
    QRect r = sel_;
    if (resize) {
        r.setWidth(qBound(kMinSel, r.width() + dx, width() - r.left()));
        r.setHeight(qBound(kMinSel, r.height() + dy, height() - r.top()));
    } else {
        r.translate(dx, dy);
        r.moveLeft(qBound(0, r.left(), width() - r.width()));
        r.moveTop(qBound(0, r.top(), height() - r.height()));
    }
    sel_ = r;
    update();
}

QRect SelectionOverlay::selection() const {
    return dragging_ ? QRect(a_, b_).normalized() : sel_;
}

QRect SelectionOverlay::sourceRect(const QRect &s) const {
    const double rx = double(shot_.width()) / qMax(1, width());
    const double ry = double(shot_.height()) / qMax(1, height());
    QRect r(qRound(s.x() * rx), qRound(s.y() * ry),
            qRound(s.width() * rx), qRound(s.height() * ry));
    return r.intersected(shot_.rect());
}

// Поворачиваются только фигуры, которым это имеет смысл.
bool SelectionOverlay::rotatable(Tool t) {
    return t == Rect || t == Ellipse || t == Text || t == Balloon || t == Sticker ||
           t == Pen || t == Image;
}

// Поворот вокруг центра объекта.
QTransform SelectionOverlay::itemTransform(const Item &it) const {
    QTransform t;
    if (it.angle != 0 && rotatable(it.tool)) {
        const QPointF c = itemBounds(it).center();
        t.translate(c.x(), c.y());
        t.rotate(it.angle);
        t.translate(-c.x(), -c.y());
    }
    return t;
}

QRectF SelectionOverlay::itemBounds(const Item &it) const {
    switch (it.tool) {
    case Rect:
    case Ellipse:
    case Line:
    case Arrow:
    case Highlight:
    case Magnify:
    case Blur:
    case Pixelate:
    case Spotlight:
    case Image:
        return QRectF(it.a, it.b).normalized();
    case Pen:
        return QPolygonF(it.pts).boundingRect();
    case Sticker:
    case Step: {
        const double s = it.size > 0 ? it.size : (it.tool == Step ? kStepSize : kStickerSize);
        return QRectF(it.a.x() - s / 2, it.a.y() - s / 2, s, s);
    }
    case Text: {
        const QFontMetrics fm(textFont(it.size > 0 ? it.size : kTextSize));
        return QRectF(it.a, QSizeF(fm.horizontalAdvance(it.text), fm.height()));
    }
    case Balloon: { // блок вместе с хвостом
        const double sz = it.size > 0 ? it.size : kBalloonSize;
        const QFontMetrics fm(textFont(sz));
        return QRectF(it.a, QSizeF(fm.horizontalAdvance(it.text) + 20,
                                   fm.height() + 16 + balloonTail(sz)));
    }
    default:
        return QRectF();
    }
}

SelectionOverlay::Handles SelectionOverlay::handlesFor(const Item &it) const {
    Handles h;
    const double hs = 10;
    auto box = [&](const QPointF &c) { return QRectF(c.x() - hs, c.y() - hs, 2 * hs, 2 * hs); };

    if (it.tool == Arrow || it.tool == Line) {
        h.line = true;
        h.endA = box(it.a);
        h.endB = box(it.b);
        const QPointF mid = (it.a + it.b) / 2;
        const double dx = it.b.x() - it.a.x(), dy = it.b.y() - it.a.y();
        const double len = std::hypot(dx, dy);
        QPointF n(0, -1);
        if (len > 0) {
            n = QPointF(dy / len, -dx / len);
            if (n.y() > 0) n = QPointF(-n.x(), -n.y());
        }
        h.move = box(mid + n * 26);
        h.del = box(mid - n * 26);
    } else {
        const QRectF fr = itemBounds(it).adjusted(-8, -8, 8, 8);
        const QTransform t = itemTransform(it);
        h.frame = fr;
        h.quad = t.map(QPolygonF(fr));
        h.move = box(t.map(fr.topLeft()));
        h.resize = box(t.map(fr.bottomRight()));
        h.del = box(t.map(fr.topRight()));
        if (rotatable(it.tool)) {
            h.hasRot = true;
            h.rot = box(t.map(QPointF(fr.center().x(), fr.top() - 30)));
        }
    }
    return h;
}

// Пока курсор в этой зоне, рамка и ручки объекта не пропадают.
bool SelectionOverlay::inHover(const Item &it, const QPointF &pos) const {
    const Handles h = handlesFor(it);
    if (h.line) {
        // ручки стоят в 26 px от линии (+10 на размер), поэтому зона — 40 px
        return h.endA.contains(pos) || h.endB.contains(pos) || h.move.contains(pos) ||
               h.del.contains(pos) || segDist(pos, it.a, it.b) < 40;
    }
    if (h.move.contains(pos) || h.resize.contains(pos) || h.del.contains(pos) ||
        (h.hasRot && h.rot.contains(pos)))
        return true;

    bool ok = true;
    const QTransform inv = itemTransform(it).inverted(&ok);
    const QPointF lp = ok ? inv.map(pos) : pos;
    const QRectF outer = h.frame.adjusted(-14, -14, 14, 14);
    const QRectF inner = h.frame.adjusted(14, 14, -14, -14);
    if (outer.contains(lp) && !(inner.isValid() && inner.contains(lp))) return true;
    // «мостик» от рамки к ручке поворота
    return h.hasRot &&
           QRectF(h.frame.center().x() - 14, h.frame.top() - 44, 28, 44).contains(lp);
}

// Объект под курсором (сверху вниз); у линий — по линии, у рамок — по границе.
// Штрихи ластика объектами для наведения не считаются.
int SelectionOverlay::hitTest(const QPointF &pos) const {
    for (int i = items_.size() - 1; i >= 0; --i) {
        const Item &it = items_.at(i);
        bool ok = true;
        const QPointF lp = (it.angle != 0 && rotatable(it.tool))
                               ? itemTransform(it).inverted(&ok).map(pos)
                               : pos;
        switch (it.tool) {
        case Arrow:
        case Line:
            if (segDist(pos, it.a, it.b) < 10) return i;
            break;
        case Rect:
        case Ellipse:
        case Magnify: {
            const QRectF r = QRectF(it.a, it.b).normalized();
            if (r.adjusted(-8, -8, 8, 8).contains(lp) && !r.adjusted(8, 8, -8, -8).contains(lp))
                return i;
            break;
        }
        case Pen:
            for (int k = 1; k < it.pts.size(); ++k)
                if (segDist(lp, it.pts.at(k - 1), it.pts.at(k)) < 8) return i;
            break;
        case Highlight:
        case Blur:
        case Pixelate:
        case Spotlight:
        case Image:
        case Sticker:
        case Step:
        case Text:
        case Balloon:
            if (itemBounds(it).adjusted(-4, -4, 4, 4).contains(lp)) return i;
            break;
        default:
            break;
        }
    }
    return -1;
}

void SelectionOverlay::updateHover(const QPointF &pos) {
    int h = -1;
    if (hovered_ >= 0 && hovered_ < items_.size() && inHover(items_.at(hovered_), pos))
        h = hovered_;
    if (h < 0) h = hitTest(pos);

    Qt::CursorShape cs = (h < 0 && tool_ == Select) ? selCursor(selHit(pos.toPoint()))
                                                     : Qt::CrossCursor;
    if (h >= 0) {
        const Handles hd = handlesFor(items_.at(h));
        if (hd.del.contains(pos)) cs = Qt::PointingHandCursor;
        else if (hd.line) {
            if (hd.endA.contains(pos) || hd.endB.contains(pos)) cs = Qt::PointingHandCursor;
            else if (hd.move.contains(pos)) cs = Qt::SizeAllCursor;
        } else {
            if (hd.hasRot && hd.rot.contains(pos)) cs = Qt::OpenHandCursor;
            else if (hd.resize.contains(pos)) cs = Qt::SizeFDiagCursor;
            else if (hd.move.contains(pos)) cs = Qt::SizeAllCursor;
        }
    }
    setCursor(cs);
    if (h != hovered_) {
        hovered_ = h;
        update();
    }
}

void SelectionOverlay::applyDrag(const QPointF &pos) {
    if (selIdx_ < 0 || selIdx_ >= items_.size()) return;
    layerDirty_ = true; // объект меняется, кэшированный слой больше не актуален
    Item &it = items_[selIdx_];
    const Item &o = dragOrig_;
    const QPointF d = pos - dragStart_;

    // сдвиг курсора в системе координат повёрнутого объекта
    QTransform rinv;
    rinv.rotate(-o.angle);
    const QPointF dl = rotatable(o.tool) ? rinv.map(d) : d;

    it = o;
    switch (dragMode_) {
    case DragMode::Move:
        it.a += d;
        it.b += d;
        for (QPointF &p : it.pts) p += d;
        break;
    case DragMode::EndA:
        it.a = o.a + d;
        break;
    case DragMode::EndB:
        it.b = o.b + d;
        break;
    case DragMode::Rotate: {
        const QPointF c = itemBounds(o).center();
        double ang = std::atan2(pos.y() - c.y(), pos.x() - c.x()) * 180.0 / kPi + 90.0;
        const double snap = std::round(ang / 15.0) * 15.0;
        if (std::fabs(ang - snap) < 4.0) ang = snap; // прилипание к шагу 15°
        while (ang > 180) ang -= 360;
        while (ang <= -180) ang += 360;
        it.angle = ang;
        break;
    }
    case DragMode::Resize: {
        switch (o.tool) {
        case Rect:
        case Ellipse:
        case Highlight:
        case Magnify:
        case Blur:
        case Pixelate:
        case Spotlight:
        case Image: {
            const QRectF r = QRectF(o.a, o.b).normalized();
            it.a = r.topLeft();
            it.b = QPointF(qMax(r.left() + 8, r.right() + dl.x()),
                           qMax(r.top() + 8, r.bottom() + dl.y()));
            break;
        }
        case Pen: {
            const QRectF r = QPolygonF(o.pts).boundingRect();
            const double sx = qMax(0.1, (r.width() + dl.x()) / qMax(1.0, r.width()));
            const double sy = qMax(0.1, (r.height() + dl.y()) / qMax(1.0, r.height()));
            for (int i = 0; i < it.pts.size(); ++i)
                it.pts[i] = r.topLeft() + QPointF((o.pts.at(i).x() - r.left()) * sx,
                                                  (o.pts.at(i).y() - r.top()) * sy);
            break;
        }
        case Sticker:
        case Step: {
            const double base = o.size > 0 ? o.size : (o.tool == Step ? kStepSize : kStickerSize);
            it.size = qBound(20.0, base + 2 * qMax(dl.x(), dl.y()), 400.0);
            break;
        }
        case Text:
        case Balloon: {
            const double base = o.size > 0 ? o.size : (o.tool == Balloon ? kBalloonSize : kTextSize);
            const QFontMetrics fm(textFont(base));
            const double w0 = qMax(1, fm.horizontalAdvance(o.text));
            const double nw = qMax(10.0, w0 + dl.x());
            it.size = qBound(10.0, base * nw / w0, 300.0);
            break;
        }
        default:
            break;
        }

        // якорь (центр стикера, у остальных левый верхний угол) остаётся на месте и при повороте
        auto anchor = [&](const Item &x) {
            const QRectF b = itemBounds(x);
            return itemTransform(x).map((x.tool == Sticker || x.tool == Step) ? b.center()
                                                                             : b.topLeft());
        };
        const QPointF fix = anchor(o) - anchor(it);
        it.a += fix;
        it.b += fix;
        for (QPointF &p : it.pts) p += fix;
        break;
    }
    case DragMode::None:
        break;
    }
}

// Размытие и мозаика берут пиксели исходного снимка под прямоугольником.
void SelectionOverlay::drawEffect(QPainter &p, const QRectF &r, bool pixelate) const {
    const double rx = double(shot_.width()) / qMax(1, width());
    const double ry = double(shot_.height()) / qMax(1, height());
    const QRect src = QRectF(r.x() * rx, r.y() * ry, r.width() * rx, r.height() * ry)
                          .toAlignedRect().intersected(shot_.rect());
    if (src.width() < 2 || src.height() < 2) return;

    const QImage part = shot_.copy(src);
    QImage out;
    if (pixelate) {
        const int block = qMax(6, qRound(12 * rx));
        const QSize small(qMax(1, part.width() / block), qMax(1, part.height() / block));
        out = part.scaled(small, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                  .scaled(part.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    } else {
        const int f = qMax(6, qRound(10 * rx));
        const QSize small(qMax(1, part.width() / f), qMax(1, part.height() / f));
        out = part.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .scaled(part.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    p.drawImage(QRectF(src.x() / rx, src.y() / ry, src.width() / rx, src.height() / ry), out);
}

// Лупа: в эллипс r вписывается участок снимка вокруг его центра, увеличенный в zoom_ раз.
void SelectionOverlay::drawLens(QPainter &p, const QRectF &r, const QColor &ring) const {
    if (r.width() < 4 || r.height() < 4) return;
    const double rx = double(shot_.width()) / qMax(1, width());
    const double ry = double(shot_.height()) / qMax(1, height());
    const QPointF c = r.center();
    const double hw = r.width() / (2 * zoom_), hh = r.height() / (2 * zoom_);
    const QRect src = QRectF((c.x() - hw) * rx, (c.y() - hh) * ry, 2 * hw * rx, 2 * hh * ry)
                          .toAlignedRect().intersected(shot_.rect());
    if (src.width() < 2 || src.height() < 2) return;

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(r);
    p.setClipPath(clip);
    p.drawImage(r, shot_.copy(src));
    p.restore();

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ring, qMax(2, lineW_ * 3 / 4)));
    p.drawEllipse(r);
}

// Все объекты по порядку: штрих ластика стирает то, что нарисовано до него.
void SelectionOverlay::paintItems(QPainter &p) const {
    for (int i = 0; i < items_.size(); ++i) {
        if (i == editIdx_ && textEdit_->isVisible()) continue; // его сейчас правят
        drawItem(p, items_.at(i));
    }
    if (drawing_) drawItem(p, cur_);
}

// Слой аннотаций (без объекта в процессе рисования) пересобирается только когда устарел.
void SelectionOverlay::ensureLayer() {
    const qreal dpr = devicePixelRatioF();
    const QSize px = size() * dpr;
    if (!layerDirty_ && layer_.size() == px) return;

    layer_ = QImage(px, QImage::Format_ARGB32_Premultiplied);
    layer_.setDevicePixelRatio(dpr);
    layer_.fill(Qt::transparent);
    QPainter lp(&layer_);
    lp.setRenderHint(QPainter::Antialiasing);
    for (const Item &it : items_) drawItem(lp, it);
    lp.end();
    layerDirty_ = false;
}

// Новый отрезок штриха ластика сразу стирается в готовом слое: остальное не перерисовывается.
void SelectionOverlay::eraseSeg(const QPointF &a, const QPointF &b, double size) {
    QPainter lp(&layer_);
    lp.setRenderHint(QPainter::Antialiasing);
    lp.setCompositionMode(QPainter::CompositionMode_Clear);
    lp.setPen(QPen(Qt::black, size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (a == b) lp.drawPoint(a);
    else lp.drawLine(a, b);
}

// Перерисовывается только прямоугольник между двумя точками плюс запас.
void SelectionOverlay::updateRect(const QPointF &a, const QPointF &b, double pad) {
    update(QRectF(a, b).normalized().adjusted(-pad, -pad, pad, pad).toAlignedRect());
}

void SelectionOverlay::drawItem(QPainter &p, const Item &it) const {
    if (it.angle != 0 && rotatable(it.tool)) {
        p.save();
        p.setTransform(itemTransform(it), true);
        drawShape(p, it);
        p.restore();
    } else {
        drawShape(p, it);
    }
}

void SelectionOverlay::drawShape(QPainter &p, const Item &it) const {
    QPen pen(it.color, lineW_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (it.tool) {
    case Rect:
        p.drawRect(QRectF(it.a, it.b).normalized());
        break;
    case Ellipse:
        p.drawEllipse(QRectF(it.a, it.b).normalized());
        break;
    case Line:
        p.drawLine(it.a, it.b);
        break;
    case Arrow: {
        QPainterPath path(it.a);
        path.lineTo(it.b);
        const double ang = std::atan2(it.b.y() - it.a.y(), it.b.x() - it.a.x());
        const double spread = kPi / 7;
        for (int s : {-1, 1}) {
            QPointF h(it.b.x() - arrowLen_ * std::cos(ang + s * spread),
                      it.b.y() - arrowLen_ * std::sin(ang + s * spread));
            path.moveTo(it.b);
            path.lineTo(h);
        }
        p.drawPath(path);
        break;
    }
    case Pen: {
        if (it.pts.isEmpty()) break;
        QPainterPath path(it.pts.first());
        for (int i = 1; i < it.pts.size(); ++i) path.lineTo(it.pts.at(i));
        p.drawPath(path);
        break;
    }
    case Eraser: { // стирает всё, что уже лежит в слое аннотаций
        if (it.pts.isEmpty()) break;
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setPen(QPen(Qt::black, it.size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (it.pts.size() == 1) {
            p.drawPoint(it.pts.first());
        } else {
            QPainterPath path(it.pts.first());
            for (int i = 1; i < it.pts.size(); ++i) path.lineTo(it.pts.at(i));
            p.drawPath(path);
        }
        p.restore();
        break;
    }
    case Highlight: {
        QColor c = it.color;
        c.setAlpha(hlAlpha_);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(QRectF(it.a, it.b).normalized());
        break;
    }
    case Magnify:
        drawLens(p, QRectF(it.a, it.b).normalized(), it.color);
        break;
    case Blur:
        drawEffect(p, QRectF(it.a, it.b).normalized(), false);
        break;
    case Pixelate:
        drawEffect(p, QRectF(it.a, it.b).normalized(), true);
        break;
    case Spotlight: { // затемняем всё, кроме выбранной области
        const QRectF r = QRectF(it.a, it.b).normalized();
        QPainterPath dim;
        dim.addRect(QRectF(rect()));
        QPainterPath hole;
        hole.addRoundedRect(r, 4, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, settings::spotlightAlpha()));
        p.drawPath(dim.subtracted(hole));
        break;
    }
    case Image:
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(it.a, it.b).normalized(), it.image);
        p.restore();
        break;
    case Step: {
        const QRectF b = itemBounds(it);
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(it.color);
        p.drawEllipse(b);
        QFont f;
        f.setBold(true);
        f.setPixelSize(qMax(8, qRound(b.height() * 0.5)));
        p.setFont(f);
        p.setPen(it.color.lightness() > 180 ? Qt::black : Qt::white);
        p.drawText(b, Qt::AlignCenter, it.text);
        break;
    }
    case Sticker: {
        const QRectF b = itemBounds(it);
        QFont f;
        f.setPixelSize(qMax(8, qRound(b.height() * 0.72)));
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(b, Qt::AlignCenter, it.text);
        break;
    }
    case Text: {
        const double sz = it.size > 0 ? it.size : kTextSize;
        const QFont f = textFont(sz);
        QPainterPath path;
        path.addText(it.a.x(), it.a.y() + QFontMetrics(f).ascent(), f, it.text);
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(it.color);
        p.drawPath(path);
        break;
    }
    case Balloon: { // цельная фигура: блок + хвостик в левом нижнем углу
        const double sz = it.size > 0 ? it.size : kBalloonSize;
        const double tailH = balloonTail(sz);
        QRectF body = itemBounds(it);
        body.setHeight(body.height() - tailH);

        QPainterPath shape;
        shape.addRoundedRect(body, 10, 10);
        QPainterPath tail;
        const double x0 = body.left() + 14;
        tail.moveTo(x0, body.bottom() - 1);
        tail.lineTo(x0 + tailH * 1.2, body.bottom() - 1);
        tail.lineTo(x0 - 2, body.bottom() + tailH);
        tail.closeSubpath();

        p.setPen(QPen(QColor(30, 30, 46), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(it.color);
        p.drawPath(shape.united(tail));

        const QFont f = textFont(sz);
        p.setFont(f);
        p.setPen(it.color.lightness() > 180 ? Qt::black : Qt::white);
        p.drawText(QPointF(body.left() + 10, body.top() + 8 + QFontMetrics(f).ascent()), it.text);
        break;
    }
    default:
        break;
    }
}

// Рамка и ручки наведённого объекта (в итоговое изображение не попадают).
void SelectionOverlay::drawHandles(QPainter &p, const Item &it) const {
    const QColor accent(137, 180, 250);
    const QColor danger(243, 139, 168);
    const QColor dark(30, 30, 46);
    const Handles h = handlesFor(it);
    const QPen glyph(dark, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    p.setBrush(Qt::NoBrush);
    if (!h.line) {
        p.setPen(QPen(accent, 1.5, Qt::DashLine));
        p.drawPolygon(h.quad);
        if (h.hasRot) // стебелёк от рамки к ручке поворота
            p.drawLine((h.quad.at(0) + h.quad.at(1)) / 2, h.rot.center());
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(h.endA.center(), 6, 6);
        p.drawEllipse(h.endB.center(), 6, 6);
    }

    // перенос: крест со стрелками
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawEllipse(h.move);
    p.setPen(glyph);
    const QPointF c = h.move.center();
    p.drawLine(c + QPointF(-5, 0), c + QPointF(5, 0));
    p.drawLine(c + QPointF(0, -5), c + QPointF(0, 5));
    p.drawLine(c + QPointF(-5, 0), c + QPointF(-3, -2));
    p.drawLine(c + QPointF(-5, 0), c + QPointF(-3, 2));
    p.drawLine(c + QPointF(5, 0), c + QPointF(3, -2));
    p.drawLine(c + QPointF(5, 0), c + QPointF(3, 2));
    p.drawLine(c + QPointF(0, -5), c + QPointF(-2, -3));
    p.drawLine(c + QPointF(0, -5), c + QPointF(2, -3));
    p.drawLine(c + QPointF(0, 5), c + QPointF(-2, 3));
    p.drawLine(c + QPointF(0, 5), c + QPointF(2, 3));

    // удаление: красный кружок с крестиком
    p.setPen(Qt::NoPen);
    p.setBrush(danger);
    p.drawEllipse(h.del);
    p.setPen(glyph);
    const QPointF x = h.del.center();
    p.drawLine(x + QPointF(-4, -4), x + QPointF(4, 4));
    p.drawLine(x + QPointF(-4, 4), x + QPointF(4, -4));

    if (h.hasRot) { // поворот: круговая стрелка
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(h.rot);
        p.setPen(glyph);
        p.setBrush(Qt::NoBrush);
        const QPointF r = h.rot.center();
        p.drawArc(QRectF(r.x() - 5, r.y() - 5, 10, 10), 40 * 16, 280 * 16);
        const QPointF tip = r + QPointF(5 * std::cos(40 * kPi / 180), -5 * std::sin(40 * kPi / 180));
        p.drawLine(tip, tip + QPointF(-1, -4));
        p.drawLine(tip, tip + QPointF(-4.5, 0.5));
    }

    if (!h.line) { // размер: диагональная двойная стрелка
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(h.resize);
        p.setPen(glyph);
        const QPointF r = h.resize.center();
        p.drawLine(r + QPointF(-4, -4), r + QPointF(4, 4));
        p.drawLine(r + QPointF(4, 4), r + QPointF(4, 0.5));
        p.drawLine(r + QPointF(4, 4), r + QPointF(0.5, 4));
        p.drawLine(r + QPointF(-4, -4), r + QPointF(-4, -0.5));
        p.drawLine(r + QPointF(-4, -4), r + QPointF(-0.5, -4));
    }
}

void SelectionOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.drawImage(rect(), shot_);

    const QRect s = selection();
    const bool hasSel = s.width() >= 1 && s.height() >= 1;
    p.fillRect(rect(), QColor(0, 0, 0, hasSel ? 140 : 60));

    if (hasSel) {
        const QRect src = sourceRect(s);
        p.drawImage(s, shot_, src);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(137, 180, 250), 2));
        p.drawRect(s);

        const QString label = QString("%1 × %2").arg(src.width()).arg(src.height());
        QRect box(s.left(), s.top() - 24, 110, 22);
        if (box.top() < 0) box.moveTop(s.top() + 4);
        p.fillRect(box, QColor(30, 30, 46, 220));
        p.setPen(QColor(205, 214, 244));
        p.drawText(box, Qt::AlignCenter, label);
                if (!dragging_ && !direct_ && tool_ == Select) {
            const int x0 = s.left(), x1 = s.right() + 1, xm = (x0 + x1) / 2;
            const int y0 = s.top(), y1 = s.bottom() + 1, ym = (y0 + y1) / 2;
            const QPoint pts[8] = {{x0, y0}, {xm, y0}, {x1, y0}, {x0, ym},
                                   {x1, ym}, {x0, y1}, {xm, y1}, {x1, y1}};
            p.save();
            p.setPen(QPen(QColor(30, 30, 46), 1));
            p.setBrush(QColor(137, 180, 250));
            for (const QPoint &c : pts) p.drawRect(QRect(c.x() - 4, c.y() - 4, 8, 8));
            p.restore();
        }
    }

    p.setRenderHint(QPainter::Antialiasing);

    // Ластик вычитает из аннотаций, поэтому при его штрихах они рисуются в отдельный прозрачный слой.
    bool needLayer = erasing_;
    for (const Item &it : items_)
        if (it.tool == Eraser) { needLayer = true; break; }

    if (needLayer && !textEdit_->isVisible()) {
        ensureLayer(); // готовый слой: пока двигается ластик, он не пересобирается
        p.drawImage(0, 0, layer_);
        if (drawing_) drawItem(p, cur_);
    } else if (needLayer) { // идёт ввод текста: слой собираем заново, без правимого объекта
        const qreal dpr = devicePixelRatioF();
        QImage tmp(size() * dpr, QImage::Format_ARGB32_Premultiplied);
        tmp.setDevicePixelRatio(dpr);
        tmp.fill(Qt::transparent);
        QPainter lp(&tmp);
        lp.setRenderHint(QPainter::Antialiasing);
        paintItems(lp);
        lp.end();
        p.drawImage(0, 0, tmp);
    } else {
        paintItems(p);
    }

    if (!drawing_ && !dragging_ && !textEdit_->isVisible() && hovered_ >= 0 &&
        hovered_ < items_.size())
        drawHandles(p, items_.at(hovered_));

    if (tool_ == Eraser) { // круг ластика вместо курсора
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 160), 3));
        p.drawEllipse(mousePos_, eraserSize_ / 2, eraserSize_ / 2);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawEllipse(mousePos_, eraserSize_ / 2, eraserSize_ / 2);
    }
}

void SelectionOverlay::resizeEvent(QResizeEvent *) {
    layerDirty_ = true;
    layoutBars();
}

void SelectionOverlay::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton) { emit cancelled(); return; }
    if (e->button() != Qt::LeftButton) return;

    commitText();
    if (!activated_) { activated_ = true; emit activated(); }

    const QPointF pos = e->position();
    mousePos_ = pos;

    // ручки наведённого объекта имеют приоритет над текущим инструментом
    if (hovered_ >= 0 && hovered_ < items_.size()) {
        const Item hit = items_.at(hovered_);
        const Handles h = handlesFor(hit);

        if (h.del.contains(pos)) { // крестик: убрать объект
            pushHistory();
            items_.removeAt(hovered_);
            hovered_ = selIdx_ = -1;
            update();
            return;
        }

        DragMode m = DragMode::None;
        if (h.line) {
            if (h.endA.contains(pos)) m = DragMode::EndA;
            else if (h.endB.contains(pos)) m = DragMode::EndB;
            else if (h.move.contains(pos)) m = DragMode::Move;
        } else {
            if (h.hasRot && h.rot.contains(pos)) m = DragMode::Rotate;
            else if (h.resize.contains(pos)) m = DragMode::Resize;
            else if (h.move.contains(pos)) m = DragMode::Move;
        }
        if (m != DragMode::None) {
            pushHistory();
            selIdx_ = hovered_;
            dragMode_ = m;
            dragOrig_ = hit;
            dragStart_ = pos;
            update();
            return;
        }
    }
    hovered_ = -1;

    switch (tool_) {
        case Select: {
        const int m = selHit(pos.toPoint());
        if (m) { // тянем существующую рамку
            selDrag_ = m;
            selOrig_ = sel_;
            selStart_ = pos.toPoint();
            return;
        }
        a_ = b_ = pos.toPoint(); // иначе начинаем новую
        dragging_ = true;
        break;
    }
    case Eraser: { // штрих ластика — обычный объект в списке; весь проход откатывается одним Ctrl+Z
        pushHistory();
        ensureLayer(); // слой со всем, что нарисовано до этого штриха
        Item er;
        er.tool = Eraser;
        er.size = eraserSize_;
        er.pts << pos;
        items_ << er;
        erasing_ = true;
        eraseSeg(pos, pos, er.size); // первая точка стирается сразу в готовом слое
        updateRect(pos, pos, eraserSize_ / 2 + 6);
        return;
    }
    case Rect:
    case Ellipse:
    case Line:
    case Arrow:
    case Pen:
    case Highlight:
    case Magnify:
    case Blur:
    case Pixelate:
    case Spotlight:
        drawing_ = true;
        cur_ = Item();
        cur_.tool = tool_;
        cur_.a = cur_.b = pos;
        cur_.color = color_;
        if (tool_ == Pen) cur_.pts << pos;
        break;
    case Step: {
        pushHistory();
        Item it;
        it.tool = Step;
        it.a = pos;
        it.color = color_;
        it.text = QString::number(stepNo_++);
        it.size = kStepSize;
        items_ << it;
        break;
    }
    case Sticker: {
        pushHistory();
        Item it;
        it.tool = Sticker;
        it.a = pos;
        it.color = color_;
        it.text = emoji_;
        it.size = kStickerSize;
        items_ << it;
        break;
    }
    case Text:
    case Balloon: {
        const QPointF off = tool_ == Balloon ? QPointF(10, 8) : QPointF();
        // клик по готовому тексту или выноске правит его, а не создаёт новый
        const int i = hitTest(pos);
        if (i >= 0 && (items_.at(i).tool == Text || items_.at(i).tool == Balloon)) {
            const Item &it = items_.at(i);
            editIdx_ = i;
            openText(it.a, it.text, it.tool == Balloon ? QPointF(10, 8) : QPointF());
            break;
        }
        editIdx_ = -1;
        balloonEdit_ = tool_ == Balloon;
        openText(pos, QString(), off);
        break;
    }
    case Image: // у вставленной картинки нет инструмента: она появляется по Ctrl+V
        break;
    }
    update();
}

// Двойной клик по тексту или выноске: правка содержимого.
void SelectionOverlay::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    commitText();
    const int i = hitTest(e->position());
    if (i < 0) return;
    const Item &it = items_.at(i);
    if (it.tool != Text && it.tool != Balloon) return;

    editIdx_ = i;
    hovered_ = -1;
    openText(it.a, it.text, it.tool == Balloon ? QPointF(10, 8) : QPointF());
    update();
}

void SelectionOverlay::mouseMoveEvent(QMouseEvent *e) {
    const QPointF pos = e->position();
    const QPointF prev = mousePos_;
    mousePos_ = pos;
        if (selDrag_) {
        applySelDrag(pos.toPoint());
        update();
        return;
    }

    if (dragMode_ != DragMode::None) {
        applyDrag(pos);
    } else if (erasing_) {
        // стираем только новый отрезок и перерисовываем только область вокруг него
        if (!items_.isEmpty() && items_.last().tool == Eraser) {
            eraseSeg(items_.last().pts.last(), pos, items_.last().size);
            items_.last().pts << pos;
        }
        updateRect(prev, pos, eraserSize_ / 2 + 6);
        return;
    } else if (tool_ == Eraser) { // ластик без нажатия: двигается только круг
        updateRect(prev, pos, eraserSize_ / 2 + 6);
        return;
    } else if (dragging_) {
        b_ = pos.toPoint();
    } else if (drawing_) {
        cur_.b = pos;
        if (cur_.tool == Pen) cur_.pts << pos;
    } else {
        updateHover(pos);
        return;
    }
    update();
}

void SelectionOverlay::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;

        if (selDrag_) {
        selDrag_ = 0;
        updateHover(e->position());
        update();
        return;
    }

    if (dragMode_ != DragMode::None) {
        dragMode_ = DragMode::None;
        hovered_ = selIdx_;
        updateHover(e->position());
        update();
        return;
    }
    if (erasing_) {
        erasing_ = false;
        update();
        return;
    }

    if (dragging_) {
        b_ = e->position().toPoint();
        dragging_ = false;
        const QRect s = QRect(a_, b_).normalized();
        if (s.width() >= 3 && s.height() >= 3) {
            sel_ = s;
            // после первого выделения возвращаем инструмент, которым рисовали в прошлый раз
            if (!toolRestored_ && !direct_ && settings::rememberTool()) {
                toolRestored_ = true;
                const int v = settings::lastTool();
                if (v > int(Select) && v != int(Image) && toolBtns_.contains(v)) setTool(Tool(v));
            }
            if (direct_) finish(SaveCopy);
        }
    } else if (drawing_) {
        drawing_ = false;
        const double dx = cur_.b.x() - cur_.a.x(), dy = cur_.b.y() - cur_.a.y();
        const bool ok = cur_.tool == Pen ? cur_.pts.size() > 1
                                         : std::hypot(dx, dy) > 3;
        if (ok) {
            pushHistory();
            items_ << cur_;
        }
    }
    update();
}

// Колёсико в режиме ластика меняет его размер.
void SelectionOverlay::wheelEvent(QWheelEvent *e) {
    if (tool_ != Eraser) {
        QWidget::wheelEvent(e);
        return;
    }
    eraserSize_ = qBound(6.0, eraserSize_ + (e->angleDelta().y() > 0 ? 4 : -4), 150.0);
    updateRect(mousePos_, mousePos_, 90);
}

void SelectionOverlay::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        emit cancelled();
        return;
    }

    if (!(e->modifiers() & Qt::ControlModifier)) {
        const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        const bool rs = e->modifiers() & Qt::AltModifier;
        switch (e->key()) {
        case Qt::Key_Left:  nudgeSelection(-step, 0, rs); return;
        case Qt::Key_Right: nudgeSelection(step, 0, rs); return;
        case Qt::Key_Up:    nudgeSelection(0, -step, rs); return;
        case Qt::Key_Down:  nudgeSelection(0, step, rs); return;
        default: break;
        }
    }

    if (e->modifiers() & Qt::ControlModifier) {
        switch (e->key()) {
        case Qt::Key_Z:
            if (e->modifiers() & Qt::ShiftModifier) redo();
            else undo();
            return;
        case Qt::Key_Y: redo(); return;
        case Qt::Key_V: pasteImage(); return;
        case Qt::Key_C: finish(Copy); return;
        case Qt::Key_S: finish(Save); return;
        case Qt::Key_U: finish(Upload); return;
        default: return;
        }
    }
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: finish(Copy); break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: deleteHovered(); break;
    case Qt::Key_S: setTool(Select); break;
    case Qt::Key_R: setTool(Rect); break;
    case Qt::Key_E: setTool(Ellipse); break;
    case Qt::Key_L: setTool(Line); break;
    case Qt::Key_A: setTool(Arrow); break;
    case Qt::Key_P: setTool(Pen); break;
    case Qt::Key_H: setTool(Highlight); break;
    case Qt::Key_T: setTool(Text); break;
    case Qt::Key_O: setTool(Balloon); break;
    case Qt::Key_N: setTool(Step); break;
    case Qt::Key_I: setTool(Sticker); break;
    case Qt::Key_M: setTool(Magnify); break;
    case Qt::Key_B: setTool(Blur); break;
    case Qt::Key_X: setTool(Pixelate); break;
    case Qt::Key_F: setTool(Spotlight); break;
    case Qt::Key_D: setTool(Eraser); break;
    case Qt::Key_C: cycleColor(); break;
    default: break;
    }
}

// Итог: снимок с аннотациями, обрезанный по области (или весь экран, если области нет).
// Аннотации собираются в отдельный слой, чтобы ластик стирал только их, а не снимок.
QImage SelectionOverlay::render() const {
    const QRect s = sel_.isNull() ? rect() : sel_;
    const double rx = double(shot_.width()) / qMax(1, width());
    const double ry = double(shot_.height()) / qMax(1, height());

    QImage full = shot_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage layer(full.size(), QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHint(QPainter::Antialiasing);
        lp.scale(rx, ry);
        for (const Item &it : items_) drawItem(lp, it);
    }
    {
        QPainter p(&full);
        p.drawImage(0, 0, layer);
    }
    return full.copy(sourceRect(s));
}