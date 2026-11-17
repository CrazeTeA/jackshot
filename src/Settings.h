#pragma once
#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImageWriter>
#include <QList>
#include <QRect>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariant>

// Все настройки лежат в ~/.config/jackshot/jackshot.conf
namespace settings {

inline QVariant get(const QString &key, const QVariant &def = QVariant()) {
    QSettings s("jackshot", "jackshot");
    return s.value(key, def);
}

inline void put(const QString &key, const QVariant &value) {
    QSettings s("jackshot", "jackshot");
    s.setValue(key, value);
}

inline QString defaultDir() {
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Screenshots";
}
inline bool defaultDirExists() { return QDir(defaultDir()).exists(); }

inline bool setupDone() { return get("setup/done", false).toBool(); }
inline void setSetupDone(bool v) { put("setup/done", v); }

inline QString saveDir() {
    const QString d = get("save/dir").toString().trimmed();
    return d.isEmpty() ? defaultDir() : d;
}
inline void setSaveDir(const QString &d) { put("save/dir", d); }

// Форматы, которые реально умеет писать эта сборка Qt (+ PDF всегда).
inline QStringList supportedFormats() {
    const QList<QByteArray> w = QImageWriter::supportedImageFormats();
    auto has = [&](const char *f) { return w.contains(QByteArray(f)); };
    QStringList out{"png"};
    if (has("jpg") || has("jpeg")) out << "jpg";
    if (has("webp")) out << "webp";
    if (has("bmp")) out << "bmp";
    out << "pdf";
    return out;
}

inline QString format() {
    const QString f = get("file/format", "png").toString().toLower();
    return supportedFormats().contains(f) ? f : QString("png");
}
inline void setFormat(const QString &f) { put("file/format", f.toLower()); }

inline int quality() { return qBound(1, get("file/quality", 90).toInt(), 100); }
inline void setQuality(int q) { put("file/quality", q); }

inline QString nameTemplate() {
    const QString t = get("file/template", "Screenshot_{date}_{time}").toString();
    return t.trimmed().isEmpty() ? QString("Screenshot_{date}_{time}") : t;
}
inline void setNameTemplate(const QString &t) { put("file/template", t); }

inline bool copyOnSave() { return get("behavior/copyOnSave", false).toBool(); }
inline void setCopyOnSave(bool v) { put("behavior/copyOnSave", v); }

inline QString uploadUrl() {
    const QString u = get("upload/url", "https://0x0.st").toString().trimmed();
    return u.isEmpty() ? QString("https://0x0.st") : u;
}
inline void setUploadUrl(const QString &u) { put("upload/url", u); }

inline bool globalHotkeys() { return get("hotkeys/global", false).toBool(); }
inline void setGlobalHotkeys(bool v) { put("hotkeys/global", v); }

inline QString hotkey() {
    const QString k = get("hotkeys/key", "Print").toString().trimmed();
    return k.isEmpty() ? QString("Print") : k;
}
inline void setHotkey(const QString &k) { put("hotkeys/key", k); }

// --- Внешний вид и рисование (правятся в jackshot.conf) ---

inline int lineWidth() { return qBound(1, get("draw/lineWidth", 4).toInt(), 40); }
inline int arrowHead() { return qBound(6, get("draw/arrowHead", 18).toInt(), 80); }
inline int highlightAlpha() { return qBound(10, get("draw/highlightAlpha", 100).toInt(), 255); }
inline double magnifyZoom() { return qBound(1.2, get("draw/magnifyZoom", 2.0).toDouble(), 8.0); }
inline int barOpacity() { return qBound(30, get("bar/opacity", 90).toInt(), 100); }
inline int spotlightAlpha() { return qBound(40, get("draw/spotlightAlpha", 150).toInt(), 240); }

// Запоминать последний инструмент рисования: после выделения области включается он.
inline bool rememberTool() { return get("behavior/rememberTool", true).toBool(); }
inline int lastTool() { return get("ui/lastTool", 0).toInt(); }
inline void setLastTool(int t) { put("ui/lastTool", t); }

// Задержка перед снимком, секунды (0 — без задержки). `jackshot --delay N` важнее.
inline int captureDelay() { return qBound(0, get("capture/delay", 0).toInt(), 60); }
inline void setCaptureDelay(int s) { put("capture/delay", s); }

// Последняя выбранная область: монитор и рамка в логических координатах.
// Рамка хранится строкой через пробел: запятые QSettings в INI читает как список.
inline void setLastRegion(const QString &screen, const QRect &r) {
    put("capture/lastScreen", screen);
    put("capture/lastRect", QString("%1 %2 %3 %4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
}
inline bool lastRegion(QString *screen, QRect *r) {
    const QStringList p = get("capture/lastRect").toString().split(' ', Qt::SkipEmptyParts);
    if (p.size() != 4) return false;
    const QRect q(p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt());
    const QString name = get("capture/lastScreen").toString();
    if (name.isEmpty() || q.width() < 8 || q.height() < 8) return false;
    *screen = name;
    *r = q;
    return true;
}
inline QString iconColor() { return get("theme/iconColor", "#cdd6f4").toString(); }

// Палитра цветов: «#rrggbb,#rrggbb,...». В INI запятые QSettings читает как список.
inline QList<QColor> palette() {
    QStringList parts =
        get("draw/palette", "#ff3c3c,#ffc800,#50c850,#3c8cff,#ffffff").toStringList();
    if (parts.size() == 1) parts = parts.first().split(',');
    QList<QColor> out;
    for (const QString &s : parts) {
        const QColor c(s.trimmed());
        if (c.isValid()) out << c;
    }
    if (out.isEmpty())
        out = {QColor(255, 60, 60), QColor(255, 200, 0), QColor(80, 200, 80),
               QColor(60, 140, 255), QColor(255, 255, 255)};
    return out;
}

// Пользовательские стили панели: ~/.config/jackshot/style.css (Qt Style Sheets).
inline QString styleFile() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           "/jackshot/style.css";
}

inline QString userStyle() {
    QFile f(styleFile());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

// Создаёт файл-шаблон, если его ещё нет (все строки закомментированы).
inline void ensureStyleFile() {
    if (QFile::exists(styleFile())) return;
    QDir().mkpath(QFileInfo(styleFile()).absolutePath());
    QFile f(styleFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    f.write("/* Jackshot: стили панели (Qt Style Sheets, похоже на CSS).\n"
            "   Применяется поверх встроенного оформления при каждом запуске.\n"
            "   Селекторы: #toolbar, #emojibar, QToolButton (:hover, :checked). */\n\n"
            "/* #toolbar { background: rgba(24, 24, 37, 200); border-radius: 16px; } */\n"
            "/* QToolButton:hover { background: #585b70; } */\n"
            "/* QToolButton:checked { background: #89b4fa; } */\n"
            "/* #emojibar { background: rgba(24, 24, 37, 200); } */\n");
}

// Имя файла без расширения по шаблону. Токены: {date} {time} {datetime} {timestamp} {n}.
// Если файл уже есть: с {n} подбирается свободный номер, иначе добавляется _1, _2, ...
inline QString buildName(const QString &tpl, const QString &dir, const QString &ext) {
    const QDateTime now = QDateTime::currentDateTime();
    auto expand = [&](int n) {
        QString s = tpl;
        s.replace("{datetime}", now.toString("yyyyMMdd_HHmmss"));
        s.replace("{date}", now.toString("yyyy-MM-dd"));
        s.replace("{time}", now.toString("HH-mm-ss"));
        s.replace("{timestamp}", QString::number(now.toSecsSinceEpoch()));
        s.replace("{n}", QString::number(n));
        s.replace('/', '_');
        s.replace('\\', '_');
        s.replace(':', '-');
        s = s.trimmed();
        return s.isEmpty() ? QString("jackshot") : s;
    };

    int n = 1, suffix = 1;
    const bool hasN = tpl.contains("{n}");
    QString name = expand(n);
    const QString base = name;
    while (QFile::exists(dir + "/" + name + "." + ext) && n < 100000) {
        if (hasN) name = expand(++n);
        else name = base + "_" + QString::number(suffix++);
    }
    return name;
}

} // namespace settings