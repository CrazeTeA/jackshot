#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QList>
#include <QProcess>
#include <QScreen>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Capture.h"
#include "Daemon.h"
#include "Editor.h"
#include "JackpotFx.h"
#include "SelectionOverlay.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Share.h"

#ifndef JACKSHOT_VERSION
#define JACKSHOT_VERSION "dev"
#endif

static void setupApp(QApplication &app) {
    app.setApplicationName("jackshot");
    app.setApplicationVersion(JACKSHOT_VERSION);
    app.setDesktopFileName("jackshot");
}

static void printHelp() {
    std::puts("jackshot " JACKSHOT_VERSION " - скриншоты с аннотациями для Wayland\n"
              "\n"
              "Использование: jackshot [опции]\n"
              "  -r            область + аннотации на замороженном экране (по умолчанию)\n"
              "  -f            весь экран, открыть редактор\n"
              "  -d            без редактора: сразу сохранить и скопировать\n"
              "  -l, --last    область: взять рамку последнего снимка (с -d сразу снять её)\n"
              "  -t, --delay N снимок через N секунд (по умолчанию из настроек)\n"
              "  --daemon      фоновый режим: глобальная клавиша через портал\n"
              "  --settings    окно настроек\n"
              "  --setup       мастер первой настройки\n"
              "  -v, --version показать версию\n"
              "  -h, --help    эта справка");
}

// При самом первом запуске показываем мастер, пока на экране ничего не заморожено.
static void firstRunIfNeeded() {
    if (!settings::setupDone()) {
        SettingsDialog dlg(true);
        dlg.exec();
    }
}

static void deliver(const QImage &img, SelectionOverlay::Action a) {
    const QString path = share::defaultPath();
    switch (a) {
    case SelectionOverlay::Save: {
        const bool saved = share::save(img, path);
        if (saved && settings::copyOnSave()) share::copyImage(img);
        share::notify(saved ? "Сохранено: " + path : "Ошибка сохранения");
        break;
    }
    case SelectionOverlay::Copy:
        share::notify(share::copyImage(img) ? "Скопировано в буфер"
                                            : "Ошибка: нужен wl-clipboard");
        break;
    case SelectionOverlay::SaveCopy: {
        const bool saved = share::save(img, path);
        const bool copied = share::copyImage(img);
        share::notify(saved ? (copied ? "Сохранено и скопировано: " + path
                                      : "Сохранено (буфер недоступен): " + path)
                            : "Ошибка сохранения");
        break;
    }
    case SelectionOverlay::Upload: {
        if (!share::save(img, path)) { share::notify("Ошибка сохранения"); break; }
        QString err;
        const QString url = share::upload(path, &err);
        if (url.isEmpty()) { share::notify("Ошибка загрузки: " + err); break; }
        share::copyText(url);
        share::notify("Ссылка скопирована: " + url);
        break;
    }
    }
}

// Фоновый режим: слушаем глобальную клавишу и запускаем снимок.
static int runDaemon(int &argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("jackshot");
    app.setApplicationVersion(JACKSHOT_VERSION);

    if (!settings::globalHotkeys()) {
        std::fputs("jackshot: глобальные клавиши выключены (jackshot --settings)\n", stderr);
        return 0;
    }
    ShortcutDaemon daemon;
    switch (daemon.start()) {
    case ShortcutDaemon::NoPortal:
        std::fputs("jackshot: портал GlobalShortcuts недоступен в этой сессии\n", stderr);
        return 1;
    case ShortcutDaemon::AlreadyRunning:
        std::fputs("jackshot: демон уже запущен на этой шине D-Bus "
                   "(pgrep -a jackshot; остановить: pkill -f \"jackshot --daemon\")\n",
                   stderr);
        return 0;
    case ShortcutDaemon::Ok:
        break;
    }
    return app.exec();
}

static int runDialog(int &argc, char **argv, bool firstRun) {
    QApplication app(argc, argv);
    setupApp(app);
    SettingsDialog dlg(firstRun);
    dlg.exec();
    return 0;
}

// Пауза перед снимком. Подсказка гаснет до съёмки, чтобы не попасть в кадр.
static void waitBeforeCapture(int sec) {
    if (sec <= 0) return;
    if (sec >= 3)
        QProcess::startDetached("notify-send", {"-a", "jackshot", "-t",
                                                QString::number((sec - 1) * 1000), "jackshot",
                                                QString("Снимок через %1 с").arg(sec)});
    QEventLoop loop;
    QTimer::singleShot(sec * 1000, &loop, &QEventLoop::quit);
    loop.exec();
}

// Весь экран: редактор в обычном окне или -d.
static int runFull(int &argc, char **argv, bool direct, int delay) {
    QApplication app(argc, argv);
    setupApp(app);
    firstRunIfNeeded();
    waitBeforeCapture(delay);

    QString err;
    QImage img = capture::grab({}, &err);
    if (img.isNull()) {
        share::notify("Снимок не сделан: " + err);
        return 1;
    }
    if (direct) {
        deliver(img, SelectionOverlay::SaveCopy);
        jackpot::celebrate();
        return 0;
    }
    Editor w(img);
    w.show();
    return app.exec();
}

// Область + аннотации прямо на замороженном экране.
static int runRegion(int &argc, char **argv, bool direct, int delay, bool useLast) {
    QApplication app(argc, argv);
    setupApp(app);
    firstRunIfNeeded();
    waitBeforeCapture(delay);

    QString lastName;
    QRect lastRect;
    const bool haveLast = useLast && settings::lastRegion(&lastName, &lastRect);
    if (useLast && !haveLast) share::notify("Последней области ещё нет: выберите её мышью");

    QList<SelectionOverlay *> overlays;
    SelectionOverlay *presetOverlay = nullptr; // оверлей с готовой рамкой (--last)
    QImage result;
    SelectionOverlay::Action action = SelectionOverlay::Copy;

    for (QScreen *screen : QGuiApplication::screens()) {
        QString err;
        QImage shot = capture::grab(screen->name(), &err);
        if (shot.isNull()) {
            share::notify("Снимок не сделан: " + err);
            qDeleteAll(overlays);
            return 1;
        }
        // -l -d: ту же область снимаем сразу, без оверлея
        if (haveLast && direct && screen->name() == lastName) {
            const double rx = double(shot.width()) / qMax(1, screen->size().width());
            const double ry = double(shot.height()) / qMax(1, screen->size().height());
            const QRect src = QRect(qRound(lastRect.x() * rx), qRound(lastRect.y() * ry),
                                    qRound(lastRect.width() * rx), qRound(lastRect.height() * ry))
                                  .intersected(shot.rect());
            if (!src.isEmpty()) {
                qDeleteAll(overlays);
                deliver(shot.copy(src), SelectionOverlay::SaveCopy);
                jackpot::celebrate();
                return 0;
            }
        }
        auto *o = new SelectionOverlay(screen, shot, direct);
        if (haveLast && !direct && screen->name() == lastName) {
            o->presetRegion(lastRect);
            presetOverlay = o;
        }
        QObject::connect(o, &SelectionOverlay::activated, &app, [&overlays, o] {
            for (auto *x : overlays)
                if (x != o) x->hide();
        });
        QObject::connect(o, &SelectionOverlay::finished, &app,
                         [&](const QImage &img, SelectionOverlay::Action a) {
                             result = img;
                             action = a;
                             QApplication::quit();
                         });
        QObject::connect(o, &SelectionOverlay::cancelled, &app, [] { QApplication::quit(); });
        overlays << o;
    }

    for (auto *o : overlays) o->start();
    if (presetOverlay) // готовая рамка есть только на одном мониторе, остальные прячем
        for (auto *x : overlays)
            if (x != presetOverlay) x->hide();
    app.exec();
    qDeleteAll(overlays);
    overlays.clear();

    if (result.isNull()) return 1; // отмена
    deliver(result, action);
    jackpot::celebrate(); // звук и эффект выстрелов (если включено в настройках)
    return 0;
}

int main(int argc, char **argv) {
    bool full = false, direct = false, openSettings = false, openSetup = false, daemon = false;
    bool useLast = false;
    int delay = -1; // -1: взять из настроек

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) {
            printHelp();
            return 0;
        } else if (!std::strcmp(a, "-v") || !std::strcmp(a, "--version")) {
            std::puts("jackshot " JACKSHOT_VERSION);
            return 0;
        } else if (!std::strcmp(a, "-f")) {
            full = true;
        } else if (!std::strcmp(a, "-d")) {
            direct = true;
        } else if (!std::strcmp(a, "-r")) {
            full = false;
        } else if (!std::strcmp(a, "-l") || !std::strcmp(a, "--last")) {
            useLast = true;
        } else if (!std::strcmp(a, "-t") || !std::strcmp(a, "--delay")) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "jackshot: после %s нужно число секунд\n", a);
                return 2;
            }
            delay = qBound(0, std::atoi(argv[++i]), 60);
        } else if (!std::strcmp(a, "--daemon")) {
            daemon = true;
        } else if (!std::strcmp(a, "--settings")) {
            openSettings = true;
        } else if (!std::strcmp(a, "--setup")) {
            openSetup = true;
        } else {
            std::fprintf(stderr, "jackshot: неизвестная опция %s\n", a);
            return 2;
        }
    }

    if (daemon) return runDaemon(argc, argv);
    if (openSetup) return runDialog(argc, argv, true);
    if (openSettings) return runDialog(argc, argv, false);
    if (delay < 0) delay = settings::captureDelay();
    return full ? runFull(argc, argv, direct, delay)
                : runRegion(argc, argv, direct, delay, useLast);
}