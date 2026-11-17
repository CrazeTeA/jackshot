#pragma once
#include <QEventLoop>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Один запрос к порталу org.freedesktop.portal.Screenshot (без диалога выбора области).
class PortalShot : public QObject {
    Q_OBJECT
public:
    // Снимок всех экранов целиком. Пустое изображение при ошибке или отказе.
    QImage run(QString *err);

private slots:
    void onResponse(uint code, const QVariantMap &results);

private:
    QEventLoop loop_;
    bool done_ = false;
    uint code_ = 2;
    QString uri_;
};