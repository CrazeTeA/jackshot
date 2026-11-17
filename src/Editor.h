#pragma once
#include <QMainWindow>
#include "Canvas.h"

class Editor : public QMainWindow {
    Q_OBJECT
public:
    explicit Editor(const QImage &img);
private slots:
    void onSave();
    void onCopy();
    void onUpload();
private:
    Canvas *canvas_;
};