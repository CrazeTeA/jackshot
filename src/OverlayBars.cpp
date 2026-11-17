// Панель инструментов, палитра эмодзи и их расположение на слое.
#include "SelectionOverlay.h"
#include "Icons.h"
#include "Settings.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QToolButton>
#include <functional>

namespace {
const int kEdgeSnap = 140; // расстояние до края, при котором панель меняет ориентацию

// Точки на краю панели: за них панель перетаскивается.
class Grip : public QWidget {
public:
    explicit Grip(QWidget *bar) : QWidget(bar), bar_(bar) {
        setCursor(Qt::SizeAllCursor);
        setHorizontalBar(false);
    }

    void setHorizontalBar(bool h) {
        horizontal_ = h;
        if (h) {
            setMinimumSize(16, 0);
            setMaximumSize(16, QWIDGETSIZE_MAX);
        } else {
            setMinimumSize(0, 16);
            setMaximumSize(QWIDGETSIZE_MAX, 16);
        }
        update();
    }

    std::function<void(const QPoint &)> onMoved; // позиция курсора в координатах слоя
    std::function<void()> onDone;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(108, 112, 134));
        for (int i = -1; i <= 1; ++i) {
            if (horizontal_)
                p.drawEllipse(QPointF(width() / 2.0, height() / 2.0 + i * 8), 2, 2);
            else
                p.drawEllipse(QPointF(width() / 2.0 + i * 8, height() / 2.0), 2, 2);
        }
    }
    void mousePressEvent(QMouseEvent *e) override { press_ = e->position().toPoint(); }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
        QWidget *par = bar_->parentWidget();
        QPoint np = bar_->pos() + (e->position().toPoint() - press_);
        if (par) {
            np.setX(qBound(0, np.x(), qMax(0, par->width() - bar_->width())));
            np.setY(qBound(0, np.y(), qMax(0, par->height() - bar_->height())));
        }
        bar_->move(np);
        if (onMoved) onMoved(par ? mapTo(par, e->position().toPoint()) : QPoint());
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (onDone) onDone();
    }

private:
    QWidget *bar_;
    QPoint press_;
    bool horizontal_ = false;
};
} // namespace

void SelectionOverlay::buildToolbar() {
    // встроенный стиль + прозрачность + пользовательский style.css (он применяется последним)
    const int alpha = qRound(255 * settings::barOpacity() / 100.0);
    const QString base =
        QString("#toolbar{background:rgba(30,30,46,%1);border-radius:10px;}"
                "QToolButton{border:0;border-radius:6px;background:transparent;}"
                "QToolButton:checked,QToolButton:hover{background:#45475a;}")
            .arg(alpha);

    toolbar_ = new QWidget(this);
    toolbar_->setObjectName("toolbar");
    toolbar_->setAttribute(Qt::WA_StyledBackground);
    toolbar_->setAttribute(Qt::WA_NoMousePropagation);
    toolbar_->setCursor(Qt::ArrowCursor);
    toolbar_->setStyleSheet(base + "\n" + settings::userStyle());

    barLay_ = new QBoxLayout(QBoxLayout::TopToBottom, toolbar_);
    barLay_->setContentsMargins(6, 4, 6, 6);
    barLay_->setSpacing(2);
    QBoxLayout *lay = barLay_;

    auto *grip = new Grip(toolbar_);
    grip->onMoved = [this](const QPoint &ptr) {
        updateBarOrientation(ptr);
        placeEmojiBar();
    };
    grip->onDone = [this] { saveBarPos(); };
    grip_ = grip;
    lay->addWidget(grip);

    QColor iconCol(settings::iconColor());
    if (!iconCol.isValid()) iconCol = QColor(205, 214, 244);

    auto makeBtn = [&](icons::Kind k, const QString &tip, bool checkable) {
        auto *b = new QToolButton(toolbar_);
        b->setIcon(icons::make(k, iconCol));
        b->setIconSize(QSize(22, 22));
        b->setFixedSize(38, 38);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setFocusPolicy(Qt::NoFocus);
        lay->addWidget(b);
        return b;
    };
    auto addTool = [&](icons::Kind k, const QString &tip, Tool t) {
        QToolButton *b = makeBtn(k, tip, true);
        toolBtns_.insert(int(t), b);
        connect(b, &QToolButton::clicked, this, [this, t] { setTool(t); });
    };
    auto addSep = [&] {
        auto *sep = new QFrame(toolbar_);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color:#45475a;");
        seps_ << sep;
        lay->addWidget(sep);
    };

    addTool(icons::Kind::Select, "Область (S)", Select);
    addSep();
    addTool(icons::Kind::Rect, "Рамка (R)", Rect);
    addTool(icons::Kind::Ellipse, "Эллипс (E)", Ellipse);
    addTool(icons::Kind::Line, "Линия (L)", Line);
    addTool(icons::Kind::Arrow, "Стрелка (A)", Arrow);
    addTool(icons::Kind::Pen, "Кисть (P)", Pen);
    addTool(icons::Kind::Highlight, "Маркер (H)", Highlight);
    addTool(icons::Kind::Text, "Текст (T)", Text);
    addTool(icons::Kind::Step, "Шаг (N)", Step);
    addTool(icons::Kind::Sticker, "Эмодзи (I)", Sticker);
    addTool(icons::Kind::Magnify, "Лупа (M)", Magnify);
    addTool(icons::Kind::Blur, "Размытие (B)", Blur);
    addTool(icons::Kind::Pixelate, "Мозаика (X)", Pixelate);
    addTool(icons::Kind::Spotlight, "Фокус: затемнить всё вокруг (F)", Spotlight);
    addSep();

    colorBtn_ = makeBtn(icons::Kind::Select, "Цвет (C)", false);
    connect(colorBtn_, &QToolButton::clicked, this, &SelectionOverlay::cycleColor);

    QToolButton *undoBtn = makeBtn(icons::Kind::Undo, "Отмена (Ctrl+Z)", false);
    connect(undoBtn, &QToolButton::clicked, this, &SelectionOverlay::undo);
    QToolButton *redoBtn = makeBtn(icons::Kind::Redo, "Вернуть (Ctrl+Shift+Z)", false);
    connect(redoBtn, &QToolButton::clicked, this, &SelectionOverlay::redo);
    addSep();
    QToolButton *saveBtn = makeBtn(icons::Kind::Save, "Сохранить (Ctrl+S)", false);
    connect(saveBtn, &QToolButton::clicked, this, [this] { finish(Save); });
    QToolButton *copyBtn = makeBtn(icons::Kind::Copy, "Копировать (Enter)", false);
    connect(copyBtn, &QToolButton::clicked, this, [this] { finish(Copy); });
    QToolButton *upBtn = makeBtn(icons::Kind::Upload, "Загрузить (Ctrl+U)", false);
    connect(upBtn, &QToolButton::clicked, this, [this] { finish(Upload); });
    addSep();
    QToolButton *gearBtn = makeBtn(icons::Kind::Gear, "Настройки", false);
    connect(gearBtn, &QToolButton::clicked, this, &SelectionOverlay::openSettings);
    QToolButton *closeBtn = makeBtn(icons::Kind::Close, "Закрыть (Esc)", false);
    connect(closeBtn, &QToolButton::clicked, this, [this] { emit cancelled(); });
}

void SelectionOverlay::buildEmojiBar() {
    const int alpha = qRound(255 * settings::barOpacity() / 100.0);
    const QString base =
        QString("#emojibar{background:rgba(30,30,46,%1);border-radius:8px;}"
                "QToolButton{font-size:22px;padding:2px 4px;border:0;border-radius:4px;}"
                "QToolButton:checked,QToolButton:hover{background:#45475a;}")
            .arg(alpha);

    emojiBar_ = new QWidget(this);
    emojiBar_->setObjectName("emojibar");
    emojiBar_->setAttribute(Qt::WA_StyledBackground);
    emojiBar_->setAttribute(Qt::WA_NoMousePropagation);
    emojiBar_->setCursor(Qt::ArrowCursor);
    emojiBar_->setStyleSheet(base + "\n" + settings::userStyle());

    auto *grid = new QGridLayout(emojiBar_);
    grid->setContentsMargins(8, 6, 8, 6);
    grid->setSpacing(2);

    const QStringList list = {
        QString::fromUtf8("👍"), QString::fromUtf8("👎"), QString::fromUtf8("❗"),
        QString::fromUtf8("❓"), QString::fromUtf8("✅"), QString::fromUtf8("❌"),
        QString::fromUtf8("⭐"), QString::fromUtf8("❤️"), QString::fromUtf8("😀"),
        QString::fromUtf8("😂"), QString::fromUtf8("😮"), QString::fromUtf8("😡"),
        QString::fromUtf8("🔥"), QString::fromUtf8("👀"), QString::fromUtf8("⚠️"),
        QString::fromUtf8("💡"), QString::fromUtf8("👉"), QString::fromUtf8("📌"),
        QString::fromUtf8("🎉"), QString::fromUtf8("🐞")};

    emoji_ = list.first();
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < list.size(); ++i) {
        auto *b = new QToolButton(emojiBar_);
        b->setText(list.at(i));
        b->setCheckable(true);
        b->setChecked(i == 0);
        b->setFocusPolicy(Qt::NoFocus);
        group->addButton(b);
        grid->addWidget(b, i / 10, i % 10);
        const QString e = list.at(i);
        connect(b, &QToolButton::clicked, this, [this, e] { emoji_ = e; });
    }
}

void SelectionOverlay::updateColorIcon() {
    QPixmap pm(48, 48);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(205, 214, 244), 1.5));
    p.setBrush(color_);
    p.drawEllipse(QPointF(12, 12), 8, 8);
    p.end();
    colorBtn_->setIcon(QIcon(pm));
}

// Слой лежит выше обычных окон, поэтому настройки открываем отдельным процессом.
void SelectionOverlay::openSettings() {
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--settings"});
    emit cancelled();
}

// Стоячая панель (вертикальная) или лежачая (горизонтальная).
void SelectionOverlay::setBarOrientation(bool horizontal) {
    horizontal_ = horizontal;
    barLay_->setDirection(horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    barLay_->setContentsMargins(horizontal ? QMargins(4, 6, 6, 6) : QMargins(6, 4, 6, 6));
    static_cast<Grip *>(grip_)->setHorizontalBar(horizontal);
    for (QFrame *s : seps_) s->setFrameShape(horizontal ? QFrame::VLine : QFrame::HLine);
    barLay_->invalidate();
    toolbar_->adjustSize();
}

// Ближайший к курсору край экрана решает: сверху/снизу — лежачая, слева/справа — стоячая.
void SelectionOverlay::updateBarOrientation(const QPoint &ptr) {
    const int dl = ptr.x(), dr = width() - ptr.x();
    const int dt = ptr.y(), db = height() - ptr.y();
    const int mH = qMin(dt, db);
    const int mV = qMin(dl, dr);

    bool wantH = horizontal_;
    if (qMin(mH, mV) < kEdgeSnap) wantH = mH < mV;
    if (wantH == horizontal_) return;

    setBarOrientation(wantH);
    QPoint np = toolbar_->pos();
    np.setX(qBound(0, np.x(), qMax(0, width() - toolbar_->width())));
    np.setY(qBound(0, np.y(), qMax(0, height() - toolbar_->height())));
    toolbar_->move(np);
}

void SelectionOverlay::layoutBars() {
    if (!barPlaced_) {
        QSettings s("jackshot", "jackshot");
        setBarOrientation(s.value("toolbar/horizontal", false).toBool());
        const QPoint def(16, qMax(0, (height() - toolbar_->height()) / 2));
        toolbar_->move(s.value("toolbar/pos", def).toPoint());
        barPlaced_ = true;
    }
    toolbar_->adjustSize();
    QPoint np = toolbar_->pos();
    np.setX(qBound(0, np.x(), qMax(0, width() - toolbar_->width())));
    np.setY(qBound(0, np.y(), qMax(0, height() - toolbar_->height())));
    toolbar_->move(np);
    placeEmojiBar();
}

void SelectionOverlay::placeEmojiBar() {
    emojiBar_->adjustSize();
    int x, y;
    if (horizontal_) {
        // лежачая панель: палитра под ней, а если снизу нет места — над ней
        x = qBound(0, toolbar_->x(), qMax(0, width() - emojiBar_->width()));
        y = toolbar_->y() + toolbar_->height() + 8;
        if (y + emojiBar_->height() > height()) y = toolbar_->y() - emojiBar_->height() - 8;
    } else {
        // стоячая панель: палитра справа от неё, а если справа нет места — слева
        x = toolbar_->x() + toolbar_->width() + 8;
        if (x + emojiBar_->width() > width()) x = toolbar_->x() - emojiBar_->width() - 8;
        y = qBound(0, toolbar_->y(), qMax(0, height() - emojiBar_->height()));
    }
    emojiBar_->move(qMax(0, x), qMax(0, y));
}

void SelectionOverlay::saveBarPos() {
    QSettings s("jackshot", "jackshot");
    s.setValue("toolbar/pos", toolbar_->pos());
    s.setValue("toolbar/horizontal", horizontal_);
}