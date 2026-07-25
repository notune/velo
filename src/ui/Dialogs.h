#pragma once
#include "engine/Exporter.h"
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QSlider;
class QCheckBox;
class QProgressBar;
class QLabel;
class Document;

// "File > New Sequence" with delivery presets.
class NewSequenceDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewSequenceDialog(QWidget *parent = nullptr);
    QString name() const;
    int videoWidth() const;
    int videoHeight() const;
    double fps() const;

private:
    QLineEdit *m_name;
    QComboBox *m_preset;
    QSpinBox *m_w, *m_h;
    QDoubleSpinBox *m_fps;
};

// Export settings + live progress (runs the Exporter thread itself).
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(Document *doc, const QString &seqId, QWidget *parent = nullptr);

private:
    void startExport();
    // Refuses (or confirms) an output path that would overwrite a file the
    // project reads from; may rewrite path to an accepted suggestion.
    bool confirmOutputPath(QString &path);
    Document *m_doc;
    QString m_seqId;
    QLineEdit *m_path;
    QComboBox *m_codec;
    QSpinBox *m_w, *m_h;
    QDoubleSpinBox *m_fps;
    QSlider *m_quality;
    QLabel *m_qualityLabel;
    QSpinBox *m_audioKbps;
    QCheckBox *m_audioOnly;
    QProgressBar *m_progress;
    QLabel *m_stage;
    QPushButton *m_exportBtn = nullptr;
    QWidget *m_settings = nullptr;  // disabled while an export runs
    Exporter *m_exporter = nullptr;
};

// Keyboard shortcut editor for all registered actions.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    ShortcutsDialog(const QList<QAction *> &actions, QWidget *parent = nullptr);
};
