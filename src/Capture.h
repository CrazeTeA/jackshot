#pragma once
#include <QByteArray>
#include <QEventLoop>
#include <QImage>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace capture {

// Запускает внешнюю программу и возвращает stdout (пусто при ошибке).
// По таймауту процесс убивается.
inline QByteArray run(const QString &prog, const QStringList &args,
                      QString *err, int timeoutMs = 15000) {
    QProcess p;
    p.setStandardInputFile(QProcess::nullDevice());
    p.setProcessChannelMode(QProcess::SeparateChannels);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&p,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        &loop, &QEventLoop::quit);
    QObject::connect(&p, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    p.start(prog, args);
    timer.start(timeoutMs);
    if (p.state() != QProcess::NotRunning)
        loop.exec();

    if (p.state() != QProcess::NotRunning) {
        p.kill();
        p.waitForFinished(2000);
        if (err) *err = prog + ": таймаут";
        return {};
    }
    if (p.error() == QProcess::FailedToStart) {
        if (err) *err = prog + ": не удалось запустить (установлен ли он?)";
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (err) *err = prog + ": " +
            QString::fromUtf8(p.readAllStandardError()).trimmed();
        return {};
    }
    return p.readAllStandardOutput();
}

// Запасной способ снимка через портал Screenshot (Screenshot.cpp): нужен там,
// где grim не работает, например в KDE и GNOME (нет протокола wlr-screencopy).
QImage portalGrab(const QString &output, QString *err);

// Снимок: сначала grim (wlroots-композиторы: Mango, Hyprland, Sway), затем портал.
// output пустой — все мониторы сразу, иначе один (например "DP-1").
inline QImage grab(const QString &output, QString *err) {
    QStringList args;
    if (!output.isEmpty()) args << "-o" << output;
    args << "-t" << "ppm" << "-"; // ppm быстрее, чем png

    QString gerr;
    const QByteArray data = run("grim", args, &gerr, 15000);
    QImage img;
    if (!data.isEmpty()) img.loadFromData(data, "PPM");
    if (!img.isNull()) return img;

    QString perr;
    img = portalGrab(output, &perr);
    if (img.isNull() && err) *err = gerr + " | " + perr;
    return img;
}

} // namespace capture