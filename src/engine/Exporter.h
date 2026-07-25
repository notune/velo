#pragma once
#include "core/Model.h"
#include <QObject>
#include <QRecursiveMutex>
#include <QThread>

struct ExportSettings {
    QString outputPath;
    QString videoCodec = QStringLiteral("libx264");  // ffmpeg encoder name
    int width = 1920, height = 1080;
    double fps = 30.0;
    int crf = 20;            // quality for CRF-based encoders
    int videoBitrateKbps = 0;  // used instead of CRF when > 0
    int audioBitrateKbps = 192;
    bool audioOnly = false;
    QString container;  // derived from outputPath extension
};

// An export must never write over a file the project reads from: ffmpeg
// truncates the destination when it opens it, so a source that is still being
// decoded goes black part-way through the render and is destroyed either way.
struct OutputConflict {
    enum Kind {
        None,
        ProjectFile,   // the .velo project itself
        MediaInUse,    // bin media referenced by a clip in some sequence
        MediaUnused,   // bin media not referenced anywhere
    };
    Kind kind = None;
    QString path;  // the file that would be overwritten
    QString name;  // media display name (empty for the project file)
    // In-use sources and the project file are refused outright; an unused bin
    // item only costs the user that import, so it is merely confirmed.
    bool blocking() const { return kind == ProjectFile || kind == MediaInUse; }
    explicit operator bool() const { return kind != None; }
};

// Encodes a sequence by piping raw BGRA frames into an ffmpeg process,
// with the mixed audio pre-rendered to a temporary WAV.
class Exporter : public QThread {
    Q_OBJECT
public:
    Exporter(Project *project, QRecursiveMutex *mutex, const QString &seqId,
             const ExportSettings &settings, QObject *parent = nullptr);
    ~Exporter() override {
        cancel();
        wait(15000);
    }
    void cancel() { m_cancel.storeRelease(1); }

    static QStringList availableEncoders();  // probed once from ffmpeg
    // Does writing to outputPath clobber a file the project reads from?
    // Caller must hold the document mutex.
    static OutputConflict checkOutputPath(const Project &project,
                                          const QString &outputPath);
    // The ffmpeg binary to use: $VELO_FFMPEG, one bundled next to the
    // executable (AppImage/portable builds), or "ffmpeg" from PATH.
    static QString ffmpegBinary();

signals:
    void progress(int percent, const QString &stage);
    void finished(bool ok, const QString &message);

protected:
    void run() override;

private:
    Project *m_project;
    QRecursiveMutex *m_mutex;
    QString m_seqId;
    ExportSettings m_s;
    QAtomicInt m_cancel{0};
};
