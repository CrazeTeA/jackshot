#include "JackpotFx.h"
#include "Settings.h"
#include <QDir>
#include <QFileInfo>
#include <LayerShellQt/Window>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPolygonF>
#include <QProcess>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QWindow>
#include <cmath>
#include <QDebug>

namespace {
const double kPi = 3.14159265358979323846;
const int kDurationMs = 2400;
const int kFadeMs = 600;

// Тонкая царапина, расходящаяся от дырки (координаты в долях радиуса кратера).
struct Scratch {
    QPointF a, b;
    double width;
    int alpha;
};

// Зерно матового ореола или блестящий осколок.
struct Speck {
    QPointF p;
    double size;
    int alpha;
};

// Один выстрел: место на экране, момент и форма кратера. Все смещения заданы в долях
// радиуса R, поэтому кратер одинаково выглядит на любом мониторе.
struct Shot {
    QPointF pos;                // место попадания в долях ширины и высоты экрана
    int at = 0;                 // момент выстрела, мс
    QVector<QPointF> core;      // рваный контур тёмной дырки
    QVector<QPointF> rim;       // неровное светлое кольцо вокруг неё
    QVector<Scratch> scratches;
    QVector<Speck> specks;      // зерно ореола
    QVector<Speck> shards;      // яркие осколки
    QImage cache;               // готовая картинка кратера (создаётся при первой отрисовке)
    int cacheR = 0;
        QImage tex;          // фото отверстия из пользовательской папки (если есть)
    double rot = 0;      // случайный поворот, чтобы дырки не повторялись
    bool mirror = false;
};

QPointF polar(double angle, double r) { return QPointF(std::cos(angle) * r, std::sin(angle) * r); }

// Случайный замкнутый контур с неровным радиусом.
QVector<QPointF> jaggedRing(QRandomGenerator *rng, int n, double r, double jitter) {
    QVector<QPointF> v;
    const double base = rng->generateDouble() * 2 * kPi;
    for (int i = 0; i < n; ++i) {
        const double a = base + 2 * kPi * i / n + (rng->generateDouble() - 0.5) * 0.35;
        v << polar(a, r + (rng->generateDouble() - 0.5) * 2 * jitter);
    }
    return v;
}

Shot makeCrater(QRandomGenerator *rng, const QPointF &pos, int at) {
    auto rnd = [&] { return rng->generateDouble(); };
    Shot s;
    s.pos = pos;
    s.at = at;
    s.core = jaggedRing(rng, 12, 0.15, 0.05);
    s.rim = jaggedRing(rng, 16, 0.27, 0.035);

    // царапины: в основном короткие, немного длинных «волосков»
    for (int i = 0; i < 110; ++i) {
        const double ang = rnd() * 2 * kPi;
        const double r0 = 0.24 + rnd() * 0.16;
        const bool hair = rnd() < 0.08;
        const double len = hair ? 0.9 + rnd() * 0.6 : 0.2 + rnd() * 0.5;
        const double bend = (rnd() - 0.5) * 0.08;
        s.scratches.append({polar(ang, r0), polar(ang + bend, r0 + len),
                            0.5 + rnd() * 0.6, 90 + rng->bounded(120)});
    }

    // зерно ореола: плотнее у центра, реже к краю
    for (int i = 0; i < 260; ++i) {
        const double r = 0.28 + 0.72 * std::sqrt(rnd());
        s.specks.append({polar(rnd() * 2 * kPi, r), 0.6 + rnd() * 1.0,
                         int(40 + 110 * (1.0 - r) + rng->bounded(30))});
    }

    // блестящие осколки
    for (int i = 0; i < 16; ++i)
        s.shards.append({polar(rnd() * 2 * kPi, 0.3 + rnd() * 0.9), 1.0 + rnd() * 1.4,
                         200 + rng->bounded(56)});
    return s;
}

QString holesDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/jackshot/jackpot-holes";
}

// Загружает фото отверстий и приводит их к картинке с прозрачностью:
//  - есть альфа-канал: берём как есть;
//  - светлый фон: белое исчезает, тёмные трещины остаются (+ светлый контур для тёмных обоев);
//  - тёмный фон: чёрное исчезает, светлое остаётся.
QVector<QImage> loadHoleTextures() {
    QVector<QImage> out;
        const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.webp"};
    QFileInfoList files = QDir(holesDir()).entryInfoList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty())
        files = QDir(":/jackpot").entryInfoList(filters, QDir::Files, QDir::Name);

    auto lum = [](QRgb c) { return (qRed(c) * 30 + qGreen(c) * 59 + qBlue(c) * 11) / 100; };

    for (const QFileInfo &fi : files) {
        QImage src(fi.absoluteFilePath());
        if (src.isNull()) continue;
        const bool hadAlpha = src.hasAlphaChannel();
        src = src.convertToFormat(QImage::Format_ARGB32);
                qInfo() << "jackpot: текстура" << fi.absoluteFilePath() << "альфа:" << hadAlpha
                << "размер:" << src.size();
        const int w = src.width(), h = src.height();

                       if (hadAlpha) {
            out << src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            continue;
        }

        // фон определяем по среднему значению яркости четырёх углов
        const int corners = (lum(src.pixel(0, 0)) + lum(src.pixel(w - 1, 0)) +
                             lum(src.pixel(0, h - 1)) + lum(src.pixel(w - 1, h - 1))) / 4;

        if (corners < 128) { // тёмный фон: яркость -> прозрачность
            for (int y = 0; y < h; ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(src.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
                    const int a = qMax(r, qMax(g, b));
                    line[x] = a ? qRgba(r * 255 / a, g * 255 / a, b * 255 / a, a) : qRgba(0, 0, 0, 0);
                }
            }
            out << src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            continue;
        }

        // светлый фон: тёмное -> непрозрачное, белое -> прозрачное
        QImage dark(w, h, QImage::Format_ARGB32_Premultiplied);
        QImage halo(w, h, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < h; ++y) {
            const QRgb *in = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            QRgb *d = reinterpret_cast<QRgb *>(dark.scanLine(y));
            QRgb *hl = reinterpret_cast<QRgb *>(halo.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int a = qBound(0, int((255 - lum(in[x]) - 12) * 1.7), 255);
                d[x] = qRgba(22 * a / 255, 28 * a / 255, 38 * a / 255, a);
                hl[x] = qRgba(235 * a / 255, 242 * a / 255, 255 * a / 255, a);
            }
        }
        QImage combined(w, h, QImage::Format_ARGB32_Premultiplied);
        combined.fill(Qt::transparent);
        QPainter cp(&combined);
        cp.setOpacity(0.5);
        const int o = qMax(1, w / 300); // светлый контур вокруг тёмных линий
        for (const QPoint &d : {QPoint(o, 0), QPoint(-o, 0), QPoint(0, o), QPoint(0, -o)})
            cp.drawImage(d, halo);
        cp.setOpacity(1.0);
        cp.drawImage(0, 0, dark);
        cp.end();
        out << combined;
    }
    return out;
}

// Выстрелы идут кучками (как на референсе): три группы, внутри каждой — быстрая очередь.
QVector<Shot> makeShots() {
    QRandomGenerator *rng = QRandomGenerator::global();
    auto rnd = [&] { return rng->generateDouble(); };

    const QSize scr = QGuiApplication::primaryScreen()->size();
    const double minDim = qMin(scr.width(), scr.height());
    const int groups[] = {4, 4, 3};
        const QVector<QImage> textures = loadHoleTextures();

    QVector<Shot> v;
    int index = 0;
    for (int g : groups) {
        const QPointF center(0.2 + rnd() * 0.6, 0.28 + rnd() * 0.44);
        for (int i = 0; i < g; ++i, ++index) {
            QPointF pos;
            for (int tries = 0; tries < 30; ++tries) {
                pos = center + QPointF((rnd() + rnd() - 1) * 0.12, (rnd() + rnd() - 1) * 0.12);
                pos.setX(qBound(0.08, pos.x(), 0.92));
                pos.setY(qBound(0.10, pos.y(), 0.90));
                bool ok = true;
                for (const Shot &o : v) { // кратеры не должны налезать друг на друга
                    const double dx = (pos.x() - o.pos.x()) * scr.width() / minDim;
                    const double dy = (pos.y() - o.pos.y()) * scr.height() / minDim;
                    if (std::hypot(dx, dy) < 0.19) { ok = false; break; }
                }
                if (ok) break;
            }
                        Shot shot = makeCrater(rng, pos, index * 78 + rng->bounded(25));
            if (!textures.isEmpty()) {
                shot.tex = textures[rng->bounded(textures.size())];
                shot.rot = rnd() * 360.0;
                shot.mirror = rng->bounded(2);
            }
            v << shot;
        }
    }
    return v;
}

// Рисует один кратер в прозрачную картинку со стороной 4R (учитывает масштаб экрана).
QImage renderCrater(const Shot &s, int R, qreal dpr) {
    const double S = 4.0 * R;
    QImage img(QSize(qRound(S * dpr), qRound(S * dpr)), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
        if (!s.tex.isNull()) {
        QPainter tp(&img);
        tp.setRenderHint(QPainter::Antialiasing);
        tp.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPointF mid(S / 2, S / 2);

        // тёмная подложка, чтобы отверстие читалось и на светлом фоне
      /*  QRadialGradient under(mid, R);
        under.setColorAt(0, QColor(0, 0, 0, 50));
        under.setColorAt(1, QColor(0, 0, 0, 0));
        tp.setPen(Qt::NoPen);
        tp.setBrush(under);
        tp.drawEllipse(mid, R, R); */

        tp.translate(mid);
        tp.rotate(s.rot);
        if (s.mirror) tp.scale(-1, 1);
        const double side = 3.6 * R; // размер фото относительно кратера
        const double h = side * s.tex.height() / s.tex.width();
        tp.drawImage(QRectF(-side / 2, -h / 2, side, h), s.tex);
        return img;
    }

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(S / 2, S / 2);
    auto at = [&](const QPointF &u) { return c + u * R; };

    // 0. мягкая тёмная подложка: чтобы кратер был виден и на светлом рабочем столе
    QRadialGradient shadow(c, R * 1.15);
    shadow.setColorAt(0, QColor(0, 0, 0, 120));
    shadow.setColorAt(0.7, QColor(0, 0, 0, 70));
    shadow.setColorAt(1, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(c, R * 1.15, R * 1.15);

    // 1. матовый ореол
    QRadialGradient frost(c, R);
    frost.setColorAt(0.0, QColor(190, 210, 232, 150));
    frost.setColorAt(0.55, QColor(150, 176, 208, 90));
    frost.setColorAt(1.0, QColor(120, 150, 190, 0));
    p.setBrush(frost);
    p.drawEllipse(c, R, R);

    // 2. зерно ореола
    for (const Speck &sp : s.specks) {
        p.setBrush(QColor(205, 222, 245, sp.alpha));
        p.drawEllipse(at(sp.p), sp.size, sp.size);
    }

    // 3. царапины
    for (const Scratch &sc : s.scratches) {
        p.setPen(QPen(QColor(215, 230, 250, sc.alpha), sc.width, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(at(sc.a), at(sc.b));
    }

    // 4. светлое кольцо раздробленного стекла: широкое свечение, плотный слой и яркая кромка
    QPolygonF rim;
    for (const QPointF &u : s.rim) rim << at(u);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(150, 190, 255, 70), R * 0.16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolygon(rim);
    p.setPen(QPen(QColor(235, 242, 255, 205), R * 0.08, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolygon(rim);
    p.setPen(QPen(QColor(255, 255, 255, 255), R * 0.035, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolygon(rim);

    // 5. тёмная рваная дырка
    QPolygonF core;
    for (const QPointF &u : s.core) core << at(u);
    p.setPen(QPen(QColor(210, 225, 245, 190), 1.2));
    p.setBrush(QColor(6, 8, 12));
    p.drawPolygon(core);

    // 6. блестящие осколки
    p.setPen(Qt::NoPen);
    for (const Speck &sh : s.shards) {
        p.setBrush(QColor(255, 255, 255, sh.alpha));
        p.drawEllipse(at(sh.p), sh.size, sh.size);
    }
    return img;
}

// Прозрачный слой поверх всего экрана с анимацией; мышь и клавиатуру не перехватывает.
class FxWindow : public QWidget {
public:
    FxWindow(QScreen *screen, const QVector<Shot> &shots) : screen_(screen), shots_(shots) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }

    void start() {
        create();
        windowHandle()->setScreen(screen_);
        auto *ls = LayerShellQt::Window::get(windowHandle());
        ls->setScope("jackshot-fx");
        ls->setLayer(LayerShellQt::Window::LayerOverlay);
        ls->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
            LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        ls->setExclusiveZone(-1);
        ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        windowHandle()->setMask(QRegion(0, 0, 1, 1)); // область ввода 1×1: клики идут сквозь эффект

        clock_.start();
        show();
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { update(); });
        timer->start(16);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const qint64 t = clock_.elapsed();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // в конце всё плавно гаснет
        const double fade = t > kDurationMs - kFadeMs ? qMax(0.0, double(kDurationMs - t) / kFadeMs) : 1.0;
        const double minDim = qMin(width(), height());
        const int R = qMax(24, qRound(minDim * 0.085)); // радиус кратера
        const qreal dpr = devicePixelRatioF();

        // лёгкий холодный блик на весь экран при каждом выстреле
        double flash = 0;
        for (const Shot &s : shots_) {
            const qint64 age = t - s.at;
            if (age >= 0 && age < 90) flash = qMax(flash, 1.0 - age / 90.0);
        }
        if (flash > 0) p.fillRect(rect(), QColor(225, 235, 255, int(38 * flash * fade)));

        for (Shot &s : shots_) {
            const qint64 age = t - s.at;
            if (age < 0) continue;
            if (s.cache.isNull() || s.cacheR != R) { // кратер рисуется один раз, дальше только показывается
                s.cache = renderCrater(s, R, dpr);
                s.cacheR = R;
            }
            const QPointF c(s.pos.x() * width(), s.pos.y() * height());
            const double side = 4.0 * R;

            // «хлопок» при попадании: кратер на миг крупнее и ярче
            const double pop = age < 80 ? 1.0 + 0.28 * (1.0 - age / 80.0) : 1.0;
            p.setOpacity(fade * qMin(1.0, 0.4 + age / 25.0));
            p.drawImage(QRectF(c.x() - side * pop / 2, c.y() - side * pop / 2, side * pop, side * pop),
                        s.cache);
            p.setOpacity(1.0);

            // вспышка в момент попадания
            if (age < 130) {
                const double k = 1.0 - age / 130.0;
                QRadialGradient glow(c, R * 1.1);
                glow.setColorAt(0, QColor(255, 255, 255, int(210 * k * fade)));
                glow.setColorAt(0.5, QColor(190, 215, 255, int(110 * k * fade)));
                glow.setColorAt(1, QColor(150, 190, 255, 0));
                p.setPen(Qt::NoPen);
                p.setBrush(glow);
                p.drawEllipse(c, R * 1.1, R * 1.1);
            }
        }
    }

private:
    QScreen *screen_;
    QVector<Shot> shots_;
    QElapsedTimer clock_;
};
} // namespace

namespace jackpot {

bool enabled() { return settings::get("jackpot/enabled", true).toBool(); }
int volume() { return qBound(0, settings::get("jackpot/volume", 80).toInt(), 100); }

QString soundDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/jackshot";
}

QString soundFile() {
    const QString custom = settings::get("jackpot/sound").toString().trimmed();
    if (!custom.isEmpty() && QFile::exists(custom)) return custom;
    for (const char *ext : {"ogg", "wav", "opus", "flac", "mp3"}) {
        const QString f = soundDir() + "/jackpot." + ext;
        if (QFile::exists(f)) return f;
    }
    return {};
}

void playSound(int vol) {
    const QString file = soundFile();
    if (file.isEmpty()) return;
    if (vol < 0) vol = volume();

    auto has = [](const char *bin) { return !QStandardPaths::findExecutable(bin).isEmpty(); };
    if (has("pw-play"))
        QProcess::startDetached("pw-play", {"--volume", QString::number(vol / 100.0, 'f', 2), file});
    else if (has("paplay"))
        QProcess::startDetached("paplay", {QString("--volume=%1").arg(vol * 655), file});
    else if (has("ffplay"))
        QProcess::startDetached("ffplay", {"-nodisp", "-autoexit", "-loglevel", "quiet", "-volume",
                                           QString::number(vol), file});
    else if (has("mpv"))
        QProcess::startDetached("mpv", {"--no-video", "--really-quiet",
                                        QString("--volume=%1").arg(vol), file});
}

void celebrate(bool force, int vol) {
    if (!force && !enabled()) return;
    playSound(vol);

    const QVector<Shot> shots = makeShots();
    QList<FxWindow *> windows;
    for (QScreen *screen : QGuiApplication::screens()) {
        auto *w = new FxWindow(screen, shots);
        windows << w;
        w->start();
    }

    QEventLoop loop;
    QTimer::singleShot(kDurationMs + 80, &loop, &QEventLoop::quit);
    loop.exec();
    qDeleteAll(windows);
}

} // namespace jackpot
