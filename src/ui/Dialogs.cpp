#include "ui/Dialogs.h"
#include "core/Document.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMutexLocker>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------- NewSequenceDialog
struct SeqPreset {
    const char *name;
    int w, h;
    double fps;
};
static const SeqPreset kPresets[] = {
    {"YouTube 1080p", 1920, 1080, 30},
    {"YouTube 1080p 60", 1920, 1080, 60},
    {"YouTube 4K (UHD)", 3840, 2160, 30},
    {"Cinema 4K DCI 24", 4096, 2160, 24},
    {"Film 1080p 24", 1920, 1080, 24},
    {"PAL 1080p 25", 1920, 1080, 25},
    {"TikTok / Reels / Shorts", 1080, 1920, 30},
    {"Instagram Square", 1080, 1080, 30},
    {"720p", 1280, 720, 30},
    {"Custom", 0, 0, 0},
};

NewSequenceDialog::NewSequenceDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("New Sequence"));
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(tr("Sequence 01"));
    m_preset = new QComboBox;
    for (const auto &p : kPresets) m_preset->addItem(p.name);
    m_w = new QSpinBox;
    m_w->setRange(16, 16384);
    m_w->setValue(1920);
    m_h = new QSpinBox;
    m_h->setRange(16, 16384);
    m_h->setValue(1080);
    m_fps = new QDoubleSpinBox;
    m_fps->setRange(1, 240);
    m_fps->setValue(30);
    m_fps->setDecimals(3);
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Preset:"), m_preset);
    form->addRow(tr("Width:"), m_w);
    form->addRow(tr("Height:"), m_h);
    form->addRow(tr("Frame rate:"), m_fps);
    lay->addLayout(form);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int idx) {
        const auto &p = kPresets[idx];
        if (p.w > 0) {
            m_w->setValue(p.w);
            m_h->setValue(p.h);
            m_fps->setValue(p.fps);
        }
    });
    auto toCustom = [this] {
        m_preset->blockSignals(true);
        m_preset->setCurrentIndex(int(std::size(kPresets)) - 1);
        m_preset->blockSignals(false);
    };
    connect(m_w, &QSpinBox::valueChanged, this, [this, toCustom] {
        const auto &p = kPresets[m_preset->currentIndex()];
        if (p.w > 0 && m_w->value() != p.w) toCustom();
    });
    connect(m_h, &QSpinBox::valueChanged, this, [this, toCustom] {
        const auto &p = kPresets[m_preset->currentIndex()];
        if (p.w > 0 && m_h->value() != p.h) toCustom();
    });
}

QString NewSequenceDialog::name() const { return m_name->text().trimmed(); }
int NewSequenceDialog::videoWidth() const { return m_w->value(); }
int NewSequenceDialog::videoHeight() const { return m_h->value(); }
double NewSequenceDialog::fps() const { return m_fps->value(); }

// --------------------------------------------------------------- ExportDialog
static QString codecLabel(const QString &enc) {
    if (enc == "libx264") return "H.264 (x264)";
    if (enc == "libx265") return "H.265 / HEVC (x265)";
    if (enc == "h264_nvenc") return "H.264 (NVIDIA NVENC)";
    if (enc == "hevc_nvenc") return "HEVC (NVIDIA NVENC)";
    if (enc == "libvpx-vp9") return "VP9 (WebM)";
    if (enc == "libsvtav1") return "AV1 (SVT)";
    if (enc == "prores_ks") return "Apple ProRes";
    return enc;
}

static QString defaultExtension(const QString &enc) {
    if (enc == "libvpx-vp9") return "webm";
    if (enc == "prores_ks") return "mov";
    return "mp4";
}

ExportDialog::ExportDialog(Document *doc, const QString &seqId, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_seqId(seqId) {
    setWindowTitle(tr("Export"));
    setMinimumWidth(460);
    const Sequence *seq = doc->project().sequenceByIdConst(seqId);

    auto *lay = new QVBoxLayout(this);
    m_settings = new QWidget;
    auto *form = new QFormLayout(m_settings);
    form->setContentsMargins(0, 0, 0, 0);

    auto *pathRow = new QHBoxLayout;
    m_path = new QLineEdit(QDir::homePath() + "/" +
                           (seq ? seq->name : "export") + ".mp4");
    auto *browse = new QPushButton(tr("…"));
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);
    form->addRow(tr("Output file:"), pathRow);

    m_codec = new QComboBox;
    for (const QString &enc : Exporter::availableEncoders())
        m_codec->addItem(codecLabel(enc), enc);
    if (m_codec->count() == 0) m_codec->addItem("H.264 (x264)", "libx264");
    form->addRow(tr("Video codec:"), m_codec);

    auto *sizeRow = new QHBoxLayout;
    m_w = new QSpinBox;
    m_w->setRange(16, 16384);
    m_w->setValue(seq ? seq->width : 1920);
    m_h = new QSpinBox;
    m_h->setRange(16, 16384);
    m_h->setValue(seq ? seq->height : 1080);
    m_fps = new QDoubleSpinBox;
    m_fps->setRange(1, 240);
    m_fps->setDecimals(3);
    m_fps->setValue(seq ? seq->fps : 30);
    sizeRow->addWidget(m_w);
    sizeRow->addWidget(new QLabel("×"));
    sizeRow->addWidget(m_h);
    sizeRow->addWidget(new QLabel(tr("@")));
    sizeRow->addWidget(m_fps);
    sizeRow->addWidget(new QLabel(tr("fps")));
    sizeRow->addStretch(1);
    form->addRow(tr("Resolution:"), sizeRow);

    auto *qRow = new QHBoxLayout;
    m_quality = new QSlider(Qt::Horizontal);
    m_quality->setRange(0, 51);
    m_quality->setValue(20);
    m_quality->setInvertedAppearance(true);
    m_qualityLabel = new QLabel("CRF 20");
    qRow->addWidget(m_quality, 1);
    qRow->addWidget(m_qualityLabel);
    form->addRow(tr("Quality:"), qRow);
    connect(m_quality, &QSlider::valueChanged, this, [this](int v) {
        m_qualityLabel->setText(QString("CRF %1").arg(v));
    });

    m_audioKbps = new QSpinBox;
    m_audioKbps->setRange(64, 512);
    m_audioKbps->setValue(192);
    m_audioKbps->setSuffix(" kbps");
    form->addRow(tr("Audio bitrate:"), m_audioKbps);

    m_audioOnly = new QCheckBox(tr("Audio only"));
    form->addRow(QString(), m_audioOnly);
    lay->addWidget(m_settings);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_stage = new QLabel(tr("Ready"));
    m_stage->setStyleSheet("color: #9a9ea6;");
    lay->addWidget(m_progress);
    lay->addWidget(m_stage);

    auto *buttons = new QDialogButtonBox;
    m_exportBtn = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    auto *closeBtn = buttons->addButton(QDialogButtonBox::Close);
    lay->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(
            this, tr("Export to"), m_path->text(),
            tr("Video (*.mp4 *.mov *.mkv *.webm);;All files (*)"));
        if (!f.isEmpty()) m_path->setText(f);
    });
    connect(m_codec, &QComboBox::currentIndexChanged, this, [this] {
        QString p = m_path->text();
        const int dot = p.lastIndexOf('.');
        if (dot > 0)
            m_path->setText(p.left(dot + 1) +
                            defaultExtension(m_codec->currentData().toString()));
    });
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(closeBtn, &QPushButton::clicked, this, [this] {
        if (m_exporter && m_exporter->isRunning()) {
            m_exporter->cancel();
            m_exporter->wait(5000);
        }
        reject();
    });
}

// "clip.mp4" -> "clip (export).mp4", skipping names that clash or exist.
static QString suggestFreeName(Document *doc, const QString &path) {
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
    for (int n = 1; n <= 99; ++n) {
        const QString tag =
            n == 1 ? QString(" (export)") : QString(" (export %1)").arg(n);
        const QString cand =
            fi.absolutePath() + "/" + fi.completeBaseName() + tag + suffix;
        if (QFile::exists(cand)) continue;
        QMutexLocker lock(doc->mutex());
        if (!Exporter::checkOutputPath(doc->project(), cand)) return cand;
    }
    return QString();
}

bool ExportDialog::confirmOutputPath(QString &path) {
    OutputConflict clash;
    {
        QMutexLocker lock(m_doc->mutex());
        clash = Exporter::checkOutputPath(m_doc->project(), path);
    }
    if (!clash) return true;

    if (!clash.blocking()) {  // unused bin item: the user's call
        return QMessageBox::warning(
                   this, tr("Overwrite media?"),
                   tr("%1 is in the media bin. Exporting there replaces the "
                      "imported file and the bin item will point at the "
                      "export instead.\n\nContinue?")
                       .arg(clash.path),
                   QMessageBox::Yes | QMessageBox::No,
                   QMessageBox::No) == QMessageBox::Yes;
    }

    const QString what =
        clash.kind == OutputConflict::ProjectFile
            ? tr("%1 is this project file.").arg(clash.path)
            : tr("“%1” is a source used in the timeline (%2).")
                  .arg(clash.name, clash.path);
    QMessageBox box(QMessageBox::Warning, tr("Cannot export there"),
                    what + "\n\n" +
                        tr("Encoding writes the file while the timeline is "
                           "still reading from it, which would blacken part of "
                           "the export and destroy the original."),
                    QMessageBox::NoButton, this);
    const QString suggestion = suggestFreeName(m_doc, path);
    QPushButton *useSuggested =
        suggestion.isEmpty()
            ? nullptr
            : box.addButton(tr("Export to “%1” instead")
                                .arg(QFileInfo(suggestion).fileName()),
                            QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(useSuggested ? useSuggested : nullptr);
    box.exec();
    if (useSuggested && box.clickedButton() == useSuggested) {
        m_path->setText(suggestion);
        path = suggestion;
        return true;
    }
    return false;
}

void ExportDialog::startExport() {
    if (m_exporter && m_exporter->isRunning()) return;
    ExportSettings s;
    s.outputPath = m_path->text().trimmed();
    if (s.outputPath.isEmpty()) return;
    if (!confirmOutputPath(s.outputPath)) return;
    s.videoCodec = m_codec->currentData().toString();
    s.width = m_w->value();
    s.height = m_h->value();
    s.fps = m_fps->value();
    s.crf = m_quality->value();
    s.audioBitrateKbps = m_audioKbps->value();
    s.audioOnly = m_audioOnly->isChecked();
    m_exportBtn->setEnabled(false);
    m_settings->setEnabled(false);  // no changing settings mid-export
    m_stage->setText(tr("Starting…"));
    m_exporter = new Exporter(&m_doc->project(), m_doc->mutex(), m_seqId, s, this);
    connect(m_exporter, &Exporter::progress, this,
            [this](int pct, const QString &stage) {
                m_progress->setValue(pct);
                m_stage->setText(stage);
            });
    connect(m_exporter, &Exporter::finished, this,
            [this](bool ok, const QString &msg) {
                m_exportBtn->setEnabled(true);
                m_settings->setEnabled(true);
                if (ok) {
                    m_progress->setValue(100);
                    m_stage->setText(tr("Exported to %1").arg(msg));
                } else {
                    m_stage->setText(msg);
                    QMessageBox::warning(this, tr("Export failed"), msg);
                }
            });
    m_exporter->start();
}

// ------------------------------------------------------------ ShortcutsDialog
ShortcutsDialog::ShortcutsDialog(const QList<QAction *> &actions, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(480, 560);
    auto *lay = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Click a shortcut field and press the new key "
                               "combination. Changes are saved immediately."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #9a9ea6;");
    lay->addWidget(hint);

    auto *table = new QTableWidget(actions.size(), 2);
    table->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionMode(QAbstractItemView::NoSelection);
    for (int i = 0; i < actions.size(); ++i) {
        QAction *act = actions[i];
        auto *nameItem =
            new QTableWidgetItem(act->text().remove('&').remove("…"));
        nameItem->setFlags(Qt::ItemIsEnabled);
        table->setItem(i, 0, nameItem);
        auto *edit = new QKeySequenceEdit(act->shortcut());
        edit->setClearButtonEnabled(true);
        connect(edit, &QKeySequenceEdit::editingFinished, this, [act, edit] {
            act->setShortcut(edit->keySequence());
            QSettings settings("velo", "velo");
            settings.setValue("shortcuts/" + act->objectName(),
                              edit->keySequence().toString());
        });
        table->setCellWidget(i, 1, edit);
    }
    lay->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    lay->addWidget(buttons);
}
