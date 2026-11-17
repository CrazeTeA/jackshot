#pragma once
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QString>

namespace portal {

// Есть ли в текущей сессии портал GlobalShortcuts (зависит от бэкенда композитора).
inline bool shortcutsAvailable() {
    QDBusInterface props("org.freedesktop.portal.Desktop",
                         "/org/freedesktop/portal/desktop",
                         "org.freedesktop.DBus.Properties",
                         QDBusConnection::sessionBus());
    if (!props.isValid()) return false;
    const QDBusMessage r = props.call("Get", "org.freedesktop.portal.GlobalShortcuts", "version");
    return r.type() == QDBusMessage::ReplyMessage;
}

// Готовые строки для ручного бинда в конфиге композитора.
inline QString bindSnippets(const QString &key) {
    return QString("# Mango (~/.config/mango/config.conf)\n"
                   "bind=NONE,%1,spawn,jackshot\n"
                   "\n"
                   "# Hyprland, Lua-конфиг (hyprland.lua): обычный бинд\n"
                   "hl.bind(\"%1\", hl.dsp.exec_cmd(\"jackshot\"))\n"
                   "\n"
                   "# Hyprland, Lua-конфиг: через демон (jackshot --daemon),\n"
                   "# проверка регистрации: hyprctl globalshortcuts\n"
                   "hl.bind(\"%1\", hl.dsp.global(\"jackshot:capture\"))\n"
                   "\n"
                   "# Hyprland, старый конфиг (hyprland.conf)\n"
                   "bind = , %1, exec, jackshot\n"
                   "bind = , %1, global, jackshot:capture\n"
                   "\n"
                   "# Sway\n"
                   "bindsym %1 exec jackshot\n")
        .arg(key);
}

} // namespace portal