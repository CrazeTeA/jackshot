#pragma once
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QString>
#include "Capture.h"
#include "Settings.h"

namespace share {

// Путь для нового снимка: папка, формат и шаблон имени берутся из настроек.
inline QString defaultPath() {
    const QString dir = settings::saveDir();
    QDir().mkpath(dir);
    const QString ext = settings::format();
    return dir + "/" + settings::buildName(settings::nameTemplate(), dir, ext) + "." + ext;
}

inline bool savePdf(const QImage &img, const QString &path) {
    QPdfWriter w(path);
    w.setResolution(96);
    w.setPageSize(QPageSize(QSizeF(img.width() * 25.4 / 96.0, img.height() * 25.4 / 96.0),
                            QPageSize::Millimeter));
    w.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    QPainter p(&w);
    if (!p.isActive()) return false;
    p.drawImage(QRect(0, 0, w.width(), w.height()), img);
    return p.end();
}

// Формат определяется по расширению пути.
inline bool save(const QImage &img, const QString &path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "pdf") return savePdf(img, path);
    if (ext == "jpg" || ext == "jpeg")
        return img.convertToFormat(QImage::Format_RGB32).save(path, "JPG", settings::quality());
    if (ext == "webp") return img.save(path, "WEBP", settings::quality());
    if (ext == "bmp") return img.save(path, "BMP");
    return img.save(path, "PNG");
}

// Буфер обмена через wl-copy (в Wayland клиентский QClipboard
// теряется после закрытия окна, а wl-copy держит данные сам).
inline bool copyImage(const QImage &img) {
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    QProcess p;
    p.start("wl-copy", {"-t", "image/png"});
    if (!p.waitForStarted()) return false;
    p.write(png);
    p.closeWriteChannel();
    return p.waitForFinished(5000) && p.exitCode() == 0;
}

inline void copyText(const QString &text) {
    QProcess p;
    p.start("wl-copy", {});
    if (p.waitForStarted()) {
        p.write(text.toUtf8());
        p.closeWriteChannel();
        p.waitForFinished(3000);
    }
}

// Загрузка через curl. Адрес: переменная JACKSHOT_UPLOAD_URL, иначе из настроек.
// Ответ ожидается как простой текст со ссылкой.
inline QString upload(const QString &file, QString *err) {
    const QString url = qEnvironmentVariable("JACKSHOT_UPLOAD_URL", settings::uploadUrl());
    QByteArray out = capture::run(
        "curl", {"-sS", "--fail", "-A", "jackshot/0.6", "-F", "file=@" + file, url},
        err, 60000);
    return QString::fromUtf8(out).trimmed();
}

inline void notify(const QString &msg) {
    QProcess::startDetached("notify-send", {"-a", "jackshot", "jackshot", msg});
}

} // namespace share