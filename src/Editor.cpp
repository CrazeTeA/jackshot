#include "Editor.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QKeySequence>
#include <QToolBar>
#include "Share.h"

Editor::Editor(const QImage &img) : canvas_(new Canvas(img, this)) {
    setCentralWidget(canvas_);
    setWindowTitle("jackshot");
    resize(qMin(img.width() + 20, 1400), qMin(img.height() + 60, 900));

    auto *bar = addToolBar("tools");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // Группа взаимоисключающих инструментов.
    auto *group = new QActionGroup(this);
    auto addTool = [&](const QString &name, Canvas::Tool t, const QString &key, bool on = false) {
        QAction *a = bar->addAction(name);
        a->setCheckable(true);
        a->setChecked(on);
        a->setShortcut(QKeySequence(key));
        group->addAction(a);
        connect(a, &QAction::triggered, this, [=] { canvas_->setTool(t); });
    };
    addTool("Стрелка", Canvas::Arrow, "A", true);
    addTool("Рамка", Canvas::Rect, "R");
    addTool("Кисть", Canvas::Pen, "P");
    addTool("Стикер", Canvas::Sticker, "N");
    addTool("Текст", Canvas::Text, "T");
    bar->addSeparator();

    connect(bar->addAction("Цвет"), &QAction::triggered, this, [this] {
        QColor c = QColorDialog::getColor(Qt::red, this);
        if (c.isValid()) canvas_->setColor(c);
    });
    QAction *undo = bar->addAction("Отмена");
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, canvas_, &Canvas::undo);
    bar->addSeparator();

    QAction *save = bar->addAction("Сохранить");
    save->setShortcut(QKeySequence::Save);
    QAction *copy = bar->addAction("Копировать");
    copy->setShortcut(QKeySequence("Ctrl+C"));
    QAction *up = bar->addAction("Загрузить");
    up->setShortcut(QKeySequence("Ctrl+U"));
    connect(save, &QAction::triggered, this, &Editor::onSave);
    connect(copy, &QAction::triggered, this, &Editor::onCopy);
    connect(up, &QAction::triggered, this, &Editor::onUpload);

    auto *esc = new QAction(this);
    esc->setShortcut(Qt::Key_Escape);
    connect(esc, &QAction::triggered, this, &QWidget::close);
    addAction(esc);

    // Минимализм: плоский тёмный вид, без лишних рамок.
    setStyleSheet("QMainWindow,QToolBar{background:#1e1e2e;border:0;}"
                  "QToolButton{color:#cdd6f4;padding:4px 10px;border:0;}"
                  "QToolButton:checked,QToolButton:hover{background:#45475a;}");
}

void Editor::onSave() {
    QString path = share::defaultPath();
    if (share::save(canvas_->result(), path)) share::notify("Сохранено: " + path);
    else share::notify("Ошибка сохранения");
}

void Editor::onCopy() {
    share::notify(share::copyImage(canvas_->result())
                      ? "Скопировано в буфер" : "Ошибка: нужен wl-clipboard");
}

void Editor::onUpload() {
    QString tmp = share::defaultPath();
    share::save(canvas_->result(), tmp);
    QString err;
    QString url = share::upload(tmp, &err);
    if (url.isEmpty()) { share::notify("Ошибка загрузки: " + err); return; }
    share::copyText(url);
    share::notify("Ссылка скопирована: " + url);
}