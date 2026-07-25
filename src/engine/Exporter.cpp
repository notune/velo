#include "engine/Exporter.h"
#include "engine/AudioEngine.h"
#include "engine/Compositor.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QProcess>
#include <QTemporaryFile>

// Bundled builds (AppImage/portable) ship an ffmpeg binary next to velo so
// nothing needs to be installed; otherwise fall back to the system PATH.
QString Exporter::ffmpegBinary() {
    const QString env = qEnvironmentVariable("VELO_FFMPEG");
    if (!env.isEmpty() && QFile::exists(env)) return env;
#ifdef Q_OS_WIN
    const QString beside = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
    const QString beside = QCoreApplication::applicationDirPath() + "/ffmpeg";
#endif
    if (QFile::exists(beside)) return beside;
    return QStringLiteral("ffmpeg");
}

Exporter::Exporter(Project *project, QRecursiveMutex *mutex, const QString &seqId,
                   const ExportSettings &settings, QObject *parent)
    : QThread(parent), m_project(project), m_mutex(mutex), m_seqId(seqId),
      m_s(settings) {}

QStringList Exporter::availableEncoders() {
    static QStringList cached;
    static bool probed = false;
    if (probed) return cached;
    probed = true;
    QProcess p;
    p.start(ffmpegBinary(), {"-hide_banner", "-encoders"});
    p.waitForFinished(5000);
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    for (const QString &enc : {"libx264", "libx265", "h264_nvenc", "hevc_nvenc",
                               "libvpx-vp9", "prores_ks", "libsvtav1"})
        if (out.contains(" " + enc + " ")) cached << enc;
    return cached;
}

// Resolve a path far enough that two spellings of the same file compare equal:
// symlinks, "..", "./", a relative path. A destination that does not exist yet
// cannot be a source, so the weaker directory-only fallback is enough there.
static QString resolvedPath(const QString &p) {
    if (p.isEmpty()) return QString();
    const QFileInfo fi(QDir::fromNativeSeparators(p));
    const QString canonical = fi.canonicalFilePath();
    if (!canonical.isEmpty()) return canonical;
    const QDir dir(fi.absolutePath());
    const QString canonicalDir = dir.canonicalPath();
    return (canonicalDir.isEmpty() ? QDir::cleanPath(dir.absolutePath())
                                   : canonicalDir) +
           "/" + fi.fileName();
}

static bool samePath(const QString &a, const QString &b) {
    if (a.isEmpty() || b.isEmpty()) return false;
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

OutputConflict Exporter::checkOutputPath(const Project &project,
                                         const QString &outputPath) {
    OutputConflict c;
    const QString out = resolvedPath(outputPath);
    if (out.isEmpty()) return c;

    if (samePath(out, resolvedPath(project.filePath))) {
        c.kind = OutputConflict::ProjectFile;
        c.path = project.filePath;
        return c;
    }
    for (const MediaItem &m : project.media) {
        if (!samePath(out, resolvedPath(m.path))) continue;
        c.kind = OutputConflict::MediaUnused;
        c.path = m.path;
        c.name = m.name.isEmpty() ? QFileInfo(m.path).fileName() : m.name;
        for (const Sequence &seq : project.sequences)
            for (const QList<Track> *tracks :
                 {&seq.videoTracks, &seq.audioTracks})
                for (const Track &t : *tracks)
                    for (const Clip &clip : t.clips)
                        if (clip.mediaId == m.id) {
                            c.kind = OutputConflict::MediaInUse;
                            return c;
                        }
        return c;
    }
    return c;
}

static bool writeWavHeader(QFile &f, qint64 dataBytes) {
    QByteArray h;
    QDataStream ds(&h, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.writeRawData("RIFF", 4);
    ds << quint32(36 + dataBytes);
    ds.writeRawData("WAVEfmt ", 8);
    ds << quint32(16) << quint16(1) << quint16(2) << quint32(AudioMixer::kRate)
       << quint32(AudioMixer::kRate * 4) << quint16(4) << quint16(16);
    ds.writeRawData("data", 4);
    ds << quint32(dataBytes);
    return f.write(h) == h.size();
}

void Exporter::run() {
    double duration;
    {
        QMutexLocker lock(m_mutex);
        const Sequence *seq = m_project->sequenceByIdConst(m_seqId);
        if (!seq) {
            emit finished(false, tr("Sequence not found"));
            return;
        }
        duration = seq->duration();
        // Last line of defence: ffmpeg would truncate the file before the
        // compositor is done reading it (black frames + a lost source).
        const OutputConflict clash = checkOutputPath(*m_project, m_s.outputPath);
        if (clash.blocking()) {
            emit finished(false,
                          clash.kind == OutputConflict::ProjectFile
                              ? tr("The output file is this project file (%1). "
                                   "Choose a different name.")
                                    .arg(clash.path)
                              : tr("The output file is a source used in the "
                                   "timeline (%1). Choose a different name.")
                                    .arg(clash.path));
            return;
        }
    }
    if (duration <= 0) {
        emit finished(false, tr("Sequence is empty"));
        return;
    }

    // ---- 1) render the audio mix to a temporary WAV -------------------------
    QTemporaryFile wav(QDir::tempPath() + "/velo_export_XXXXXX.wav");
    wav.setAutoRemove(true);
    if (!wav.open()) {
        emit finished(false, tr("Cannot create temporary audio file"));
        return;
    }
    {
        AudioMixer mixer(m_project, m_mutex);
        const qint64 totalFrames = qint64(duration * AudioMixer::kRate) + 1;
        writeWavHeader(wav, totalFrames * 4);
        const int block = AudioMixer::kRate;  // 1 s blocks
        QVector<float> buf(block * 2);
        QVector<qint16> pcm(block * 2);
        for (qint64 done = 0; done < totalFrames; done += block) {
            if (m_cancel.loadAcquire()) {
                emit finished(false, tr("Export cancelled"));
                return;
            }
            const int n = int(qMin<qint64>(block, totalFrames - done));
            mixer.mix(m_seqId, double(done) / AudioMixer::kRate, n, buf.data());
            for (int i = 0; i < n * 2; ++i)
                pcm[i] = qint16(qBound(-1.0f, buf[i], 1.0f) * 32767.0f);
            wav.write(reinterpret_cast<const char *>(pcm.constData()), n * 4);
            emit progress(int(done * 15 / totalFrames), tr("Mixing audio"));
        }
        wav.flush();
    }

    // ---- 2) pipe video frames into ffmpeg -----------------------------------
    const int w = m_s.width & ~1, h = m_s.height & ~1;
    QStringList args{"-y", "-hide_banner", "-loglevel", "error"};
    if (!m_s.audioOnly) {
        args << "-f" << "rawvideo" << "-pix_fmt" << "bgra" << "-video_size"
             << QString("%1x%2").arg(w).arg(h) << "-framerate"
             << QString::number(m_s.fps) << "-i" << "pipe:0";
    }
    args << "-i" << wav.fileName();
    if (!m_s.audioOnly) {
        args << "-map" << "0:v" << "-map" << "1:a" << "-c:v" << m_s.videoCodec;
        if (m_s.videoBitrateKbps > 0) {
            args << "-b:v" << QString("%1k").arg(m_s.videoBitrateKbps);
        } else if (m_s.videoCodec.contains("nvenc")) {
            args << "-rc" << "vbr" << "-cq" << QString::number(m_s.crf);
        } else if (m_s.videoCodec == "prores_ks") {
            args << "-profile:v" << "3";
        } else {
            args << "-crf" << QString::number(m_s.crf);
        }
        if (m_s.videoCodec != "prores_ks") args << "-pix_fmt" << "yuv420p";
        if (m_s.videoCodec == "libx264" || m_s.videoCodec == "libx265")
            args << "-preset" << "medium";
    }
    const bool vorbisContainer = m_s.outputPath.endsWith(".webm") ||
                                 m_s.outputPath.endsWith(".ogg");
    args << "-c:a" << (vorbisContainer ? "libopus" : "aac") << "-b:a"
         << QString("%1k").arg(m_s.audioBitrateKbps);
    args << "-shortest" << m_s.outputPath;

    QProcess ff;
    ff.setProcessChannelMode(QProcess::MergedChannels);
    ff.start(ffmpegBinary(), args);
    if (!ff.waitForStarted(5000)) {
        emit finished(false, tr("Could not start ffmpeg"));
        return;
    }

    if (!m_s.audioOnly) {
        Compositor comp(m_project, m_mutex);
        const qint64 totalFrames = qint64(duration * m_s.fps) + 1;
        const double scale = 1.0;  // render at sequence res, scale to target
        for (qint64 f = 0; f < totalFrames; ++f) {
            if (m_cancel.loadAcquire()) {
                ff.kill();
                ff.waitForFinished(3000);
                QFile::remove(m_s.outputPath);
                emit finished(false, tr("Export cancelled"));
                return;
            }
            QImage img = comp.renderFrame(m_seqId, double(f) / m_s.fps, scale);
            if (img.width() != w || img.height() != h)
                img = img.scaled(w, h, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
            img = img.convertToFormat(QImage::Format_ARGB32);
            // QImage rows may be padded; write row by row
            const qint64 rowBytes = qint64(w) * 4;
            for (int y = 0; y < h; ++y) {
                ff.write(reinterpret_cast<const char *>(img.constScanLine(y)),
                         rowBytes);
            }
            while (ff.bytesToWrite() > 32 * 1024 * 1024) {
                if (!ff.waitForBytesWritten(10000)) break;
            }
            if (ff.state() != QProcess::Running) break;
            emit progress(15 + int(f * 84 / totalFrames), tr("Encoding video"));
        }
    } else {
        emit progress(60, tr("Encoding audio"));
    }

    ff.closeWriteChannel();
    ff.waitForFinished(-1);
    const QString log = QString::fromUtf8(ff.readAll());
    if (ff.exitStatus() != QProcess::NormalExit || ff.exitCode() != 0) {
        emit finished(false, tr("ffmpeg failed:\n") + log.right(2000));
        return;
    }
    emit progress(100, tr("Done"));
    emit finished(true, m_s.outputPath);
}
