#include "SettingsDialog.h"
#include "JackpotFx.h"
#include "Portal.h"
#include "Settings.h"
#include "Share.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#ifndef JACKSHOT_VERSION
#define JACKSHOT_VERSION "dev"
#endif

namespace {

// Тёмная тема в цветах панели слоя (Catppuccin Mocha).
const char *kStyle = R"QSS(
QDialog { background: #1e1e2e; }
QLabel { color: #cdd6f4; }
QLabel#title { font-size: 22px; font-weight: bold; }
QLabel#sub { color: #a6adc8; }
QLabel#cardTitle { font-size: 13px; font-weight: bold; color: #89b4fa; }
QFrame#card { background: #313244; border-radius: 12px; }
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
    background: #181825; color: #cdd6f4; border: 1px solid #45475a;
    border-radius: 8px; padding: 6px 8px;
    selection-background-color: #89b4fa; selection-color: #1e1e2e;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border: 1px solid #89b4fa; }
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox QAbstractItemView {
    background: #181825; color: #cdd6f4; border: 1px solid #45475a;
    selection-background-color: #45475a; outline: 0;
}
QCheckBox { color: #cdd6f4; spacing: 8px; }
QCheckBox:disabled { color: #6c7086; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 5px; border: 1px solid #6c7086; background: #181825; }
QCheckBox::indicator:checked { background: #89b4fa; border: 1px solid #89b4fa; }
QPushButton { background: #45475a; color: #cdd6f4; border: 0; border-radius: 8px; padding: 8px 16px; }
QPushButton:hover { background: #585b70; }
QPushButton:disabled { background: #313244; color: #6c7086; }
QPushButton#primary { background: #89b4fa; color: #1e1e2e; font-weight: bold; }
QPushButton#primary:hover { background: #a6c8ff; }
QPushButton#primary:disabled { background: #45475a; color: #6c7086; }
QSlider::groove:horizontal { height: 6px; background: #181825; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #89b4fa; border-radius: 3px; }
QSlider::handle:horizontal { background: #cdd6f4; width: 16px; margin: -5px 0; border-radius: 8px; }
QListWidget { background: #181825; border: 0; border-radius: 12px; padding: 6px; outline: 0; }
QListWidget::item { color: #cdd6f4; padding: 10px 12px; border-radius: 8px; }
QListWidget::item:hover { background: #313244; }
QListWidget::item:selected { background: #45475a; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #45475a; border-radius: 5px; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)QSS";

QLabel *makeLabel(const QString &text, const char *objectName) {
    auto *l = new QLabel(text);
    l->setObjectName(objectName);
    l->setWordWrap(true);
    return l;
}

// Карточка: заголовок + содержимое.
QFrame *card(const QString &title, QWidget *body) {
    auto *f = new QFrame;
    f->setObjectName("card");
    auto *l = new QVBoxLayout(f);
    l->setContentsMargins(18, 14, 18, 16);
    l->setSpacing(10);
    l->addWidget(makeLabel(title, "cardTitle"));
    l->addWidget(body);
    return f;
}

// Тело карточки с формой «подпись — поле».
QWidget *formBody(QFormLayout **out) {
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    f->setContentsMargins(0, 0, 0, 0);
    f->setHorizontalSpacing(14);
    f->setVerticalSpacing(10);
    *out = f;
    return w;
}

// Страница: крупный заголовок, пояснение и карточки.
QWidget *page(const QString &title, const QString &sub, const QList<QWidget *> &cards) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(6, 0, 6, 0);
    l->setSpacing(14);
    l->addWidget(makeLabel(title, "title"));
    l->addWidget(makeLabel(sub, "sub"));
    for (QWidget *c : cards) l->addWidget(c);
    l->addStretch(1);
    return w;
}

} // namespace

SettingsDialog::SettingsDialog(bool firstRun, QWidget *parent)
    : QDialog(parent), firstRun_(firstRun) {
    setWindowTitle(firstRun ? "Jackshot — первая настройка" : "Jackshot — настройки");
    setStyleSheet(kStyle);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    if (firstRun_) {
        setMinimumWidth(620);
        root->addWidget(makeLabel("Добро пожаловать в Jackshot", "title"));
        root->addWidget(makeLabel("Выберите папку для скриншотов и способ запуска по клавише. "
                                  "Всё можно изменить позже: jackshot --settings",
                                  "sub"));
        root->addWidget(dirCard());
        root->addWidget(hotkeyCard());
        root->addStretch(1);
    } else {
        setMinimumSize(840, 600);

        auto *nav = new QListWidget;
        nav->setFixedWidth(190);
        nav->addItems({"Сохранение", "Оформление", "Клавиша", "Загрузка", "Jackpot"});

        auto *stack = new QStackedWidget;
        stack->addWidget(page("Сохранение", "Куда и в каком виде сохраняются снимки.",
                              {dirCard(), fileCard(), afterCard()}));
        stack->addWidget(page("Оформление", "Как выглядит панель и как рисуются объекты.",
                              {themeCard()}));
        stack->addWidget(page("Горячая клавиша", "Как запускать Jackshot по клавише.",
                              {hotkeyCard()}));
        stack->addWidget(page("Загрузка", "Куда отправляется снимок кнопкой «Загрузить».",
                              {uploadCard()}));
        stack->addWidget(page("Jackpot", "Шуточная функция: звук и эффект выстрелов после снимка.",
                              {jackpotCard()}));

        connect(nav, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
        nav->setCurrentRow(0);

        auto *body = new QHBoxLayout;
        body->setSpacing(16);
        body->addWidget(nav);
        body->addWidget(stack, 1);
        root->addLayout(body, 1);
    }

    // Кнопки
    auto *btns = new QHBoxLayout;
    btns->addWidget(makeLabel(QString("Jackshot ") + JACKSHOT_VERSION, "sub"));
    btns->addStretch(1);
    auto *cancel = new QPushButton(firstRun_ ? "Пропустить" : "Отмена");
    okBtn_ = new QPushButton(firstRun_ ? "Готово" : "Сохранить");
    okBtn_->setObjectName("primary");
    okBtn_->setDefault(true);
    btns->addWidget(cancel);
    btns->addWidget(okBtn_);
    root->addLayout(btns);

    connect(okBtn_, &QPushButton::clicked, this, [this] {
        if (apply()) accept();
    });
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (firstRun_) settings::setSetupDone(true); // «Пропустить»: дальше значения по умолчанию
        reject();
    });

    updatePreview();
    updateSnippets();
    updateFinishState();
    updateSoundStatus();
}

QWidget *SettingsDialog::dirCard() {
    QFormLayout *form;
    QWidget *body = formBody(&form);

    auto *row = new QWidget;
    auto *rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);
    dirEdit_ = new QLineEdit;
    dirEdit_->setPlaceholderText("Выберите папку для скриншотов");
    if (firstRun_) dirEdit_->setText(settings::defaultDirExists() ? settings::defaultDir() : QString());
    else dirEdit_->setText(settings::saveDir());
    auto *browse = new QPushButton("Обзор…");
    rl->addWidget(dirEdit_, 1);
    rl->addWidget(browse);
    form->addRow(row);

    connect(browse, &QPushButton::clicked, this, [this] {
        QString start = dirEdit_->text().trimmed();
        if (start.isEmpty()) start = QDir::homePath();
        const QString d = QFileDialog::getExistingDirectory(this, "Папка для скриншотов", start);
        if (!d.isEmpty()) dirEdit_->setText(d);
    });
    connect(dirEdit_, &QLineEdit::textChanged, this, [this] {
        updatePreview();
        updateFinishState();
    });
    return card("Папка для скриншотов", body);
}

QWidget *SettingsDialog::fileCard() {
    QFormLayout *form;
    QWidget *body = formBody(&form);

    auto *fmtRow = new QWidget;
    auto *fl = new QHBoxLayout(fmtRow);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(10);
    formatBox_ = new QComboBox;
    for (const QString &f : settings::supportedFormats()) formatBox_->addItem(f.toUpper(), f);
    formatBox_->setCurrentIndex(qMax(0, formatBox_->findData(settings::format())));
    qualitySpin_ = new QSpinBox;
    qualitySpin_->setRange(1, 100);
    qualitySpin_->setValue(settings::quality());
    qualitySpin_->setSuffix(" %");
    fl->addWidget(formatBox_);
    fl->addWidget(new QLabel("Качество (JPG, WebP):"));
    fl->addWidget(qualitySpin_);
    fl->addStretch(1);
    form->addRow("Формат", fmtRow);

    nameEdit_ = new QLineEdit(settings::nameTemplate());
    form->addRow("Имя файла", nameEdit_);
    previewLabel_ = makeLabel("", "sub");
    form->addRow("", previewLabel_);
    form->addRow("", makeLabel("Токены: {date} {time} {datetime} {timestamp} {n}", "sub"));

    connect(nameEdit_, &QLineEdit::textChanged, this, [this] { updatePreview(); });
    connect(formatBox_, &QComboBox::currentIndexChanged, this, [this] {
        const QString f = formatBox_->currentData().toString();
        qualitySpin_->setEnabled(f == "jpg" || f == "webp");
        updatePreview();
    });
    const QString f = formatBox_->currentData().toString();
    qualitySpin_->setEnabled(f == "jpg" || f == "webp");
    return card("Файл", body);
}

QWidget *SettingsDialog::afterCard() {
    QFormLayout *form;
    QWidget *body = formBody(&form);
    copyCheck_ = new QCheckBox("Копировать снимок в буфер обмена при сохранении");
    copyCheck_->setChecked(settings::copyOnSave());
    form->addRow(copyCheck_);
    return card("После сохранения", body);
}

QWidget *SettingsDialog::themeCard() {
    QFormLayout *form;
    QWidget *body = formBody(&form);

    auto *opRow = new QWidget;
    auto *ol = new QHBoxLayout(opRow);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->setSpacing(12);
    opacitySlider_ = new QSlider(Qt::Horizontal);
    opacitySlider_->setRange(30, 100);
    opacitySlider_->setValue(settings::barOpacity());
    opacityLabel_ = new QLabel;
    opacityLabel_->setMinimumWidth(44);
    ol->addWidget(opacitySlider_, 1);
    ol->addWidget(opacityLabel_);
    form->addRow("Прозрачность панели", opRow);
    connect(opacitySlider_, &QSlider::valueChanged, this,
            [this](int v) { opacityLabel_->setText(QString::number(v) + " %"); });
    opacityLabel_->setText(QString::number(opacitySlider_->value()) + " %");

    widthSpin_ = new QSpinBox;
    widthSpin_->setRange(1, 40);
    widthSpin_->setValue(settings::lineWidth());
    widthSpin_->setSuffix(" px");
    form->addRow("Толщина линий", widthSpin_);

    auto *cssBtn = new QPushButton("Открыть style.css");
    connect(cssBtn, &QPushButton::clicked, this, [] {
        settings::ensureStyleFile();
        QProcess::startDetached("xdg-open", {settings::styleFile()});
    });
    form->addRow("Своя тема", cssBtn);
    form->addRow("", makeLabel("style.css применяется к панели поверх встроенного оформления "
                               "(синтаксис Qt Style Sheets, не INI). Остальные параметры "
                               "рисования (цвета, размер наконечника стрелки, увеличение лупы) "
                               "лежат в ~/.config/jackshot/jackshot.conf.",
                               "sub"));
    return card("Панель и рисование", body);
}

QWidget *SettingsDialog::hotkeyCard() {
    auto *body = new QWidget;
    auto *l = new QVBoxLayout(body);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(10);

    const bool portalOk = portal::shortcutsAvailable();
    hotkeysCheck_ = new QCheckBox("Глобальная клавиша через портал рабочего стола");
    hotkeysCheck_->setChecked(portalOk && settings::globalHotkeys());
    hotkeysCheck_->setEnabled(portalOk);
    l->addWidget(hotkeysCheck_);

    l->addWidget(makeLabel(
        portalOk ? "Портал GlobalShortcuts доступен в вашей сессии."
                 : "Портал GlobalShortcuts в вашей сессии недоступен (так бывает на wlroots-"
                   "композиторах). Назначьте клавишу в конфиге композитора: строки ниже. "
                   "Настройки композитора программа не меняет.",
        "sub"));

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel("Клавиша"));
    keyEdit_ = new QLineEdit(settings::hotkey());
    keyRow->addWidget(keyEdit_, 1);
    l->addLayout(keyRow);

    snippets_ = new QPlainTextEdit;
    snippets_->setReadOnly(true);
    snippets_->setFixedHeight(190);
    l->addWidget(snippets_);

    auto *copyBtn = new QPushButton("Копировать строки");
    l->addWidget(copyBtn, 0, Qt::AlignRight);
    connect(copyBtn, &QPushButton::clicked, this, [this] { share::copyText(snippets_->toPlainText()); });
    connect(keyEdit_, &QLineEdit::textChanged, this, [this] { updateSnippets(); });

    l->addWidget(makeLabel("Режим через портал работает через фоновый процесс jackshot --daemon: "
                           "он запускается после сохранения настроек и при входе в сессию "
                           "(автозапуск). В Hyprland портал лишь регистрирует действие, "
                           "клавишу привяжите строкой с global и jackshot:capture; "
                           "проверить регистрацию можно командой hyprctl globalshortcuts.",
                           "sub"));
    return card("Горячая клавиша запуска", body);
}

QWidget *SettingsDialog::uploadCard() {
    QFormLayout *form;
    QWidget *body = formBody(&form);
    urlEdit_ = new QLineEdit(settings::uploadUrl());
    form->addRow("Адрес", urlEdit_);
    form->addRow("", makeLabel("Сервис должен принимать файл полем file и возвращать ссылку "
                               "простым текстом. Переменная окружения JACKSHOT_UPLOAD_URL "
                               "имеет приоритет над этим полем.",
                               "sub"));
    return card("Адрес загрузки", body);
}

QWidget *SettingsDialog::jackpotCard() {
    QFormLayout *form;
    QWidget *body = formBody(&form);

    jackpotCheck_ = new QCheckBox("Звук и эффект выстрелов после каждого снимка");
    jackpotCheck_->setChecked(jackpot::enabled());
    form->addRow(jackpotCheck_);

    auto *volRow = new QWidget;
    auto *vl = new QHBoxLayout(volRow);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(12);
    volumeSlider_ = new QSlider(Qt::Horizontal);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(jackpot::volume());
    volumeLabel_ = new QLabel;
    volumeLabel_->setMinimumWidth(44);
    vl->addWidget(volumeSlider_, 1);
    vl->addWidget(volumeLabel_);
    form->addRow("Громкость", volRow);
    connect(volumeSlider_, &QSlider::valueChanged, this,
            [this](int v) { volumeLabel_->setText(QString::number(v) + " %"); });
    volumeLabel_->setText(QString::number(volumeSlider_->value()) + " %");

    soundStatus_ = makeLabel("", "sub");
    form->addRow(soundStatus_);

    auto *btnRow = new QWidget;
    auto *bl = new QHBoxLayout(btnRow);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    auto *openBtn = new QPushButton("Открыть папку");
    auto *testBtn = new QPushButton("Проверить");
    bl->addWidget(openBtn);
    bl->addWidget(testBtn);
    bl->addStretch(1);
    form->addRow(btnRow);

    connect(openBtn, &QPushButton::clicked, this, [this] {
        QDir().mkpath(jackpot::soundDir());
        QProcess::startDetached("xdg-open", {jackpot::soundDir()});
        updateSoundStatus();
    });
    connect(testBtn, &QPushButton::clicked, this, [this] {
        jackpot::celebrate(true, volumeSlider_->value());
        updateSoundStatus();
    });
    return card("Jackpot", body);
}

void SettingsDialog::updatePreview() {
    if (!previewLabel_ || !formatBox_ || !nameEdit_) return;
    const QString ext = formatBox_->currentData().toString();
    QString dir = dirEdit_ ? dirEdit_->text().trimmed() : QString();
    if (dir.isEmpty()) dir = settings::defaultDir();
    previewLabel_->setText("Пример: " + settings::buildName(nameEdit_->text(), dir, ext) + "." + ext);
}

void SettingsDialog::updateSnippets() {
    if (!snippets_ || !keyEdit_) return;
    QString key = keyEdit_->text().trimmed();
    if (key.isEmpty()) key = "Print";
    snippets_->setPlainText(portal::bindSnippets(key));
}

void SettingsDialog::updateFinishState() {
    if (!okBtn_) return;
    okBtn_->setEnabled(!firstRun_ || !dirEdit_->text().trimmed().isEmpty());
}

void SettingsDialog::updateSoundStatus() {
    if (!soundStatus_) return;
    const QString file = jackpot::soundFile();
    soundStatus_->setText(
        file.isEmpty()
            ? "Звуковой файл не найден. Положите jackpot.ogg (или .wav, .opus, .flac, .mp3) в папку: " +
                  jackpot::soundDir() + ". Без него эффект покажется, но без звука."
            : "Звук: " + file);
}

bool SettingsDialog::apply() {
    QString dir = dirEdit_->text().trimmed();
    if (dir.isEmpty()) dir = settings::defaultDir();
    if (!QDir().mkpath(dir)) {
        QMessageBox::warning(this, "Jackshot", "Не удалось создать или открыть папку:\n" + dir);
        return false;
    }
    settings::setSaveDir(dir);

    if (!firstRun_) {
        settings::setFormat(formatBox_->currentData().toString());
        settings::setQuality(qualitySpin_->value());
        settings::setNameTemplate(nameEdit_->text());
        settings::setCopyOnSave(copyCheck_->isChecked());
        settings::setUploadUrl(urlEdit_->text());
        settings::put("bar/opacity", opacitySlider_->value());
        settings::put("draw/lineWidth", widthSpin_->value());
        settings::put("jackpot/enabled", jackpotCheck_->isChecked());
        settings::put("jackpot/volume", volumeSlider_->value());
    }
    settings::setGlobalHotkeys(hotkeysCheck_->isChecked());
    settings::setHotkey(keyEdit_->text());
    settings::setSetupDone(true);

    // Демон глобальной клавиши: старый останавливаем, чтобы новая клавиша применилась,
    // и запускаем заново, если режим включён.
    QProcess::execute("pkill", {"-f", "jackshot --daemon"});
    if (hotkeysCheck_->isChecked())
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--daemon"});
    return true;
}