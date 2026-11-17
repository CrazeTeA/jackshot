#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;

// Окно настроек. В режиме firstRun это мастер первого запуска:
// только папка и горячая клавиша, «Готово» недоступно, пока папка не выбрана.
class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(bool firstRun, QWidget *parent = nullptr);

private:
    QWidget *dirCard();
    QWidget *fileCard();
    QWidget *afterCard();
    QWidget *themeCard();
    QWidget *hotkeyCard();
    QWidget *uploadCard();
    QWidget *jackpotCard();

    void updatePreview();
    void updateSnippets();
    void updateFinishState();
    void updateSoundStatus();
    bool apply();

    bool firstRun_;
    QLineEdit *dirEdit_ = nullptr;
    QComboBox *formatBox_ = nullptr;
    QSpinBox *qualitySpin_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLabel *previewLabel_ = nullptr;
    QCheckBox *copyCheck_ = nullptr;
    QSlider *opacitySlider_ = nullptr;
    QLabel *opacityLabel_ = nullptr;
    QSpinBox *widthSpin_ = nullptr;
    QLineEdit *urlEdit_ = nullptr;
    QCheckBox *hotkeysCheck_ = nullptr;
    QLineEdit *keyEdit_ = nullptr;
    QPlainTextEdit *snippets_ = nullptr;
    QCheckBox *jackpotCheck_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QLabel *volumeLabel_ = nullptr;
    QLabel *soundStatus_ = nullptr;
    QPushButton *okBtn_ = nullptr;
};