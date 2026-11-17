#pragma once
#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QProcess;

// Фоновый процесс: регистрирует глобальную клавишу через портал GlobalShortcuts
// и по её нажатию запускает обычный снимок.
class ShortcutDaemon : public QObject {
    Q_OBJECT
public:
    enum Result { Ok, NoPortal, AlreadyRunning };

    explicit ShortcutDaemon(QObject *parent = nullptr);
    Result start();

private slots:
    void onCreateResponse(uint code, const QVariantMap &results);
    void onBindResponse(uint code, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp,
                     const QVariantMap &options);

private:
    QString requestPath(const QString &token) const;
    void watchRequest(const QString &path, const char *slot);
    void bindShortcuts();

    QString session_;
    QProcess *capture_ = nullptr;
};