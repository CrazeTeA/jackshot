#include "Screenshot.h"
#include "Capture.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QUrl>

namespace {
const char *kService = "org.freedesktop.portal.Desktop";
const char *kPath = "/org/freedesktop/portal/desktop";
} // namespace

QImage PortalShot::run(QString *err) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (err) *err = "нет соединения с D-Bus";
        return {};
    }

    // ответ придёт сигналом на объект Request, путь которого строится по токену
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString token = "jackshot_shot";
    const QString path =
        QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(kService, path, "org.freedesktop.portal.Request", "Response", this,
                SLOT(onResponse(uint,QVariantMap)));

    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath,
                                                    "org.freedesktop.portal.Screenshot",
                                                    "Screenshot");
    QVariantMap opts;
    opts["handle_token"] = token;
    opts["interactive"] = false;
    opts["modal"] = false;
    m << QString() << opts;
    const QDBusMessage r = bus.call(m);
    if (r.type() == QDBusMessage::ErrorMessage) {
        if (err) *err = "портал Screenshot: " + r.errorMessage();
        return {};
    }

    // при первом обращении портал может ждать разрешения пользователя
    if (!done_) {
        QTimer::singleShot(60000, &loop_, &QEventLoop::quit);
        loop_.exec();
    }
    if (!done_ || code_ != 0 || uri_.isEmpty()) {
        if (err) *err = "портал не сделал снимок (отказ или не поддерживается)";
        return {};
    }

    const QString file = QUrl(uri_).toLocalFile();
    QImage img(file);
    QFile::remove(file); // портал оставляет файл в папке пользователя, он нам не нужен
    if (img.isNull() && err) *err = "не удалось прочитать снимок портала: " + file;
    return img;
}

void PortalShot::onResponse(uint code, const QVariantMap &results) {
    code_ = code;
    uri_ = results.value("uri").toString();
    done_ = true;
    loop_.quit();
}

// Портал отдаёт общий снимок всех мониторов; нужный монитор вырезаем по его положению.
QImage capture::portalGrab(const QString &output, QString *err) {
    static QImage full;
    static QString failure;
    static bool tried = false;
    if (!tried) { // за один запуск портал спрашиваем один раз
        tried = true;
        PortalShot shot;
        full = shot.run(&failure);
    }
    if (full.isNull()) {
        if (err) *err = failure;
        return {};
    }
    if (output.isEmpty()) return full;

    QScreen *target = nullptr;
    for (QScreen *s : QGuiApplication::screens())
        if (s->name() == output) target = s;
    if (!target) return full;

    const QRect virt = QGuiApplication::primaryScreen()->virtualGeometry();
    const double sx = double(full.width()) / qMax(1, virt.width());
    const double sy = double(full.height()) / qMax(1, virt.height());
    const QRect g = target->geometry();
    const QRect r(qRound((g.x() - virt.x()) * sx), qRound((g.y() - virt.y()) * sy),
                  qRound(g.width() * sx), qRound(g.height() * sy));
    return full.copy(r.intersected(full.rect()));
}