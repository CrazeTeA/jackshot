#include "Daemon.h"
#include "Portal.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QList>
#include <QMap>
#include <QProcess>
#include <cstdio>

namespace {
const char *kService = "org.freedesktop.portal.Desktop";
const char *kPath = "/org/freedesktop/portal/desktop";
const char *kIface = "org.freedesktop.portal.GlobalShortcuts";
const char *kRequest = "org.freedesktop.portal.Request";
const char *kShortcutId = "capture"; // для Hyprland: bind = , Print, global, jackshot:capture

void log(const QString &msg) {
    std::fprintf(stderr, "jackshot: %s\n", msg.toLocal8Bit().constData());
}

void note(const QString &msg) {
    log(msg);
    QProcess::startDetached("notify-send", {"-a", "jackshot", "jackshot", msg});
}
} // namespace

// Элемент массива a(sa{sv}) из BindShortcuts: идентификатор действия и его свойства.
struct PortalShortcut {
    QString id;
    QMap<QString, QDBusVariant> props;
};
using PortalShortcuts = QList<PortalShortcut>;

static QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s) {
    arg.beginStructure();
    arg << s.id << s.props;
    arg.endStructure();
    return arg;
}

static const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s) {
    arg.beginStructure();
    arg >> s.id >> s.props;
    arg.endStructure();
    return arg;
}

ShortcutDaemon::ShortcutDaemon(QObject *parent) : QObject(parent) {
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcuts>();
}

// Путь объекта Request, по которому портал пришлёт ответ (формат описан в документации портала).
QString ShortcutDaemon::requestPath(const QString &token) const {
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    return QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

void ShortcutDaemon::watchRequest(const QString &path, const char *slot) {
    QDBusConnection::sessionBus().connect(kService, path, kRequest, "Response", this, slot);
}

ShortcutDaemon::Result ShortcutDaemon::start() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        log("нет соединения с сессионной шиной D-Bus");
        return NoPortal;
    }
    // второй демон не нужен
    if (!bus.registerService("org.jackshot.Daemon")) return AlreadyRunning;

    // Идентификатор приложения нужно сообщить порталу ДО любых других его вызовов,
    // иначе портал не свяжет демон с jackshot.desktop. Для этого .desktop-файл
    // с таким именем должен быть установлен.
    QDBusMessage reg = QDBusMessage::createMethodCall(
        kService, kPath, "org.freedesktop.host.portal.Registry", "Register");
    reg << QString("jackshot") << QVariantMap();
    const QDBusMessage rr = bus.call(reg);
    if (rr.type() == QDBusMessage::ErrorMessage)
        log("Registry.Register: " + rr.errorMessage() +
            " (проверьте, что установлен jackshot.desktop)");
    else
        log("идентификатор приложения jackshot зарегистрирован в портале");

    if (!portal::shortcutsAvailable()) {
        log("портал GlobalShortcuts недоступен в этой сессии");
        return NoPortal;
    }

    bus.connect(kService, kPath, kIface, "Activated", this,
                SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));

    const QString token = "jackshot_create";
    watchRequest(requestPath(token), SLOT(onCreateResponse(uint,QVariantMap)));

    QVariantMap opts;
    opts["handle_token"] = token;
    opts["session_handle_token"] = "jackshot_session";
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, "CreateSession");
    m << opts;
    const QDBusMessage r = bus.call(m);
    if (r.type() == QDBusMessage::ErrorMessage) {
        note("CreateSession: " + r.errorMessage());
        return NoPortal;
    }
    log("сеанс запрошен, жду ответа портала");
    return Ok;
}

void ShortcutDaemon::onCreateResponse(uint code, const QVariantMap &results) {
    if (code != 0) {
        note("не удалось создать сеанс глобальных клавиш (код " + QString::number(code) + ")");
        QCoreApplication::exit(1);
        return;
    }
    session_ = results.value("session_handle").toString();
    log("сеанс создан: " + session_);
    bindShortcuts();
}

void ShortcutDaemon::bindShortcuts() {
    const QString token = "jackshot_bind";
    watchRequest(requestPath(token), SLOT(onBindResponse(uint,QVariantMap)));

    PortalShortcut sc;
    sc.id = kShortcutId;
    sc.props["description"] = QDBusVariant(QString("Сделать скриншот (Jackshot)"));
    sc.props["preferred_trigger"] = QDBusVariant(settings::hotkey());
    const PortalShortcuts list{sc};

    QVariantMap opts;
    opts["handle_token"] = token;
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, "BindShortcuts");
    m << QVariant::fromValue(QDBusObjectPath(session_)) << QVariant::fromValue(list)
      << QString() << opts;
    QDBusConnection::sessionBus().asyncCall(m);
    log("клавиша «" + settings::hotkey() + "» отправлена порталу");
}

void ShortcutDaemon::onBindResponse(uint code, const QVariantMap &) {
    if (code == 0) log("глобальная клавиша зарегистрирована");
    else note("портал не назначил клавишу (код " + QString::number(code) + ")");
}

void ShortcutDaemon::onActivated(const QDBusObjectPath &, const QString &id, qulonglong,
                                 const QVariantMap &) {
    log("нажата клавиша: " + id);
    if (id != kShortcutId) return;
    // пока предыдущий снимок открыт, повторные нажатия игнорируем
    if (capture_ && capture_->state() != QProcess::NotRunning) return;
    if (!capture_) capture_ = new QProcess(this);
    capture_->setProcessChannelMode(QProcess::ForwardedChannels);
    capture_->start(QCoreApplication::applicationFilePath(), {});
}