#include "core/Document.h"
#include "effects/Effects.h"
#include "media/MediaCache.h"
#include "engine/AudioEngine.h"
#include "engine/Compositor.h"
#include "engine/Exporter.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

// Headless engine smoke test: generates media with ffmpeg, builds a project,
// renders a frame and exports a short file. Run with `velo --selftest`.
static int selftest() {
    const QString dir = QDir::tempPath() + "/velo_selftest";
    QDir().mkpath(dir);
    const QString vid = dir + "/test.mp4";
    QProcess gen;
    gen.start(Exporter::ffmpegBinary(),
              {"-y", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30",
               "-f", "lavfi", "-i", "sine=frequency=440", "-t", "4", "-c:v",
               "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", vid});
    gen.waitForFinished(30000);
    if (gen.exitCode() != 0) {
        fprintf(stderr, "selftest: failed to generate test media\n");
        return 1;
    }

    Document doc;
    const QStringList ids = doc.importMedia({vid});
    if (ids.size() != 1) {
        fprintf(stderr, "selftest: import failed\n");
        return 1;
    }
    const QString seqId = doc.sequenceFromMedia(ids.first());
    Sequence *seq = doc.project().sequenceById(seqId);
    if (!seq || seq->duration() < 3.0) {
        fprintf(stderr, "selftest: sequence creation failed\n");
        return 1;
    }
    // exercise editing ops
    doc.splitAt(seqId, 2.0, false);
    doc.addTextClip(seqId, 2, 0.5);
    {
        // close-gap regression: move the 2nd A/V pair right, close the gap
        Sequence *sq = doc.project().sequenceById(seqId);
        Clip *v1 = &sq->videoTracks[0].clips[1];
        const double oldStart = v1->start;
        for (Clip *c : {&sq->videoTracks[0].clips[1], &sq->audioTracks[0].clips[1]})
            c->start += 5.0;
        doc.closeGap(seqId, TrackType::Video, 0, oldStart + 2.5);
        sq = doc.project().sequenceById(seqId);
        const double vs = sq->videoTracks[0].clips[1].start;
        const double as = sq->audioTracks[0].clips[1].start;
        if (std::abs(vs - oldStart) > 1e-6 || std::abs(as - oldStart) > 1e-6) {
            fprintf(stderr, "selftest: closeGap failed (v=%.3f a=%.3f want %.3f)\n",
                    vs, as, oldStart);
            return 1;
        }
    }

    Compositor comp(&doc.project(), doc.mutex());
    QImage frame = comp.renderFrame(seqId, 1.0, 0.5);
    if (frame.isNull() || frame.width() != 320) {
        fprintf(stderr, "selftest: render failed (%dx%d)\n", frame.width(),
                frame.height());
        return 1;
    }
    {
        // vignette must visibly darken the frame corner
        Sequence *sq = doc.project().sequenceById(seqId);
        Clip &vclip = sq->videoTracks[0].clips[0];
        EffectInstance fx = EffectRegistry::instance()->createInstance("vignette");
        fx.params["amount"] = AnimatedParam(100);
        vclip.effects.append(fx);
        QImage after = comp.renderFrame(seqId, 1.0, 0.5);
        auto luma = [](QRgb p) { return qRed(p) + qGreen(p) + qBlue(p); };
        const int cornerBefore = luma(frame.pixel(4, 4));
        const int cornerAfter = luma(after.pixel(4, 4));
        if (cornerAfter > cornerBefore - 60) {
            fprintf(stderr, "selftest: vignette had no effect (%d -> %d)\n",
                    cornerBefore, cornerAfter);
            return 1;
        }
        vclip.effects.clear();
        // color grade: a warm temperature must raise red vs blue
        fx = EffectRegistry::instance()->createInstance("color_grade");
        fx.params["temperature"] = AnimatedParam(80);
        vclip.effects.append(fx);
        QImage warm = comp.renderFrame(seqId, 1.0, 0.5);
        qint64 rb0 = 0, rb1 = 0;
        for (int y = 0; y < frame.height(); y += 7)
            for (int x = 0; x < frame.width(); x += 7) {
                rb0 += qRed(frame.pixel(x, y)) - qBlue(frame.pixel(x, y));
                rb1 += qRed(warm.pixel(x, y)) - qBlue(warm.pixel(x, y));
            }
        if (rb1 < rb0 + frame.width() * frame.height() / 4) {
            fprintf(stderr, "selftest: color grade temperature had no effect "
                            "(%lld -> %lld)\n", rb0, rb1);
            return 1;
        }
        vclip.effects.clear();
    }
    {
        // ripple delete after a nested sequence must keep linked A/V in sync
        Document d2;
        const QString s2 = d2.sequenceFromMedia(d2.importMedia({vid}).first());
        d2.splitAt(s2, 1.0, false);
        d2.splitAt(s2, 2.0, false);
        Sequence *sq = d2.project().sequenceById(s2);
        // nest the first piece so the audio track has no clip before t=1
        d2.setSelectedClips({sq->videoTracks[0].clips[0].id,
                             sq->audioTracks[0].clips[0].id});
        d2.nestClips(s2, d2.selectedClips());
        sq = d2.project().sequenceById(s2);
        // ripple delete the middle piece (video sandwiched, audio is not)
        QSet<quint64> mid{sq->videoTracks[0].clips[1].id,
                          sq->audioTracks[0].clips[0].id};
        d2.deleteClips(s2, mid, true);
        sq = d2.project().sequenceById(s2);
        const double vs = sq->videoTracks[0].clips[1].start;
        const double as = sq->audioTracks[0].clips[0].start;
        if (std::abs(vs - as) > 1e-6 || std::abs(vs - 1.0) > 1e-6) {
            fprintf(stderr,
                    "selftest: ripple desynced A/V (v=%.3f a=%.3f want 1.0)\n",
                    vs, as);
            return 1;
        }
    }
    AudioMixer mixer(&doc.project(), doc.mutex());
    QVector<float> buf(4800 * 2);
    mixer.mix(seqId, 1.0, 4800, buf.data());
    float peak = 0;
    for (float v : buf) peak = qMax(peak, std::abs(v));
    if (peak < 0.01f) {
        fprintf(stderr, "selftest: audio mix silent\n");
        return 1;
    }
    {
        // preserve-pitch: at 200% speed the 440 Hz test tone must stay
        // ~440 Hz; the plain resample path must shift it to ~880 Hz
        Document d4;
        const QString s4 = d4.sequenceFromMedia(d4.importMedia({vid}).first());
        Sequence *sq = d4.project().sequenceById(s4);
        const quint64 aid = sq->audioTracks[0].clips[0].id;
        d4.setClipSpeed(s4, aid, 2.0);
        AudioMixer mx(&d4.project(), d4.mutex());
        const int n = AudioMixer::kRate;  // 1 s
        QVector<float> b(n * 2);
        auto toneHz = [&]() {
            mx.mix(s4, 0.5, n, b.data());
            int crossings = 0;
            for (int i = 1; i < n; ++i)
                if ((b[i * 2] >= 0) != (b[(i - 1) * 2] >= 0)) ++crossings;
            return crossings / 2.0;
        };
        d4.setClipPreservePitch(s4, aid, true);
        const double stretched = toneHz();
        d4.setClipPreservePitch(s4, aid, false);
        const double resampled = toneHz();
        if (std::abs(stretched - 440) > 60 || std::abs(resampled - 880) > 90) {
            fprintf(stderr,
                    "selftest: preserve-pitch tones %.0f/%.0f Hz "
                    "(want ~440/~880)\n",
                    stretched, resampled);
            return 1;
        }
    }
    {
        // nesting twice must keep replacing clips in the timeline (the
        // sequence list reallocates under the hood)
        Document d3;
        const QString s3 = d3.sequenceFromMedia(d3.importMedia({vid}).first());
        for (int round = 0; round < 2; ++round) {
            Sequence *sq = d3.project().sequenceById(s3);
            QSet<quint64> all;
            for (const auto *list : {&sq->videoTracks, &sq->audioTracks})
                for (const auto &t : *list)
                    for (const auto &c : t.clips) all.insert(c.id);
            const QString nid = d3.nestClips(s3, all);
            sq = d3.project().sequenceById(s3);
            int found = 0;
            for (const auto &t : sq->videoTracks)
                for (const auto &c : t.clips)
                    if (c.type == ClipType::Nested && c.mediaId == nid) ++found;
            if (found != 1) {
                fprintf(stderr,
                        "selftest: nest round %d lost the timeline clip\n",
                        round + 1);
                return 1;
            }
        }
        // extreme speed must not blow up the audio mixer
        Sequence *sq = d3.project().sequenceById(s3);
        Clip *nestClip = &sq->videoTracks[0].clips[0];
        d3.setClipSpeed(s3, nestClip->id, 20000.0);
        AudioMixer m3(&d3.project(), d3.mutex());
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i)
            m3.mix(s3, i * 0.02, 1024, buf.data());
        if (timer.elapsed() > 2000) {
            fprintf(stderr, "selftest: 20000%% speed mix too slow (%lld ms)\n",
                    timer.elapsed());
            return 1;
        }
        // video at extreme speed renders via nearest-keyframe decoding
        Compositor c3(&d3.project(), d3.mutex());
        QImage fast = c3.renderFrame(s3, 0.0001, 0.25);
        if (fast.isNull()) {
            fprintf(stderr, "selftest: 20000%% speed render failed\n");
            return 1;
        }
    }
    {
        // decoder lanes: alternating reads at two distant positions of one
        // file (cross dissolves, reused footage in nests) must not trigger
        // a seek + GOP redecode on every request
        MediaCache cache;
        QElapsedTimer timer;
        timer.start();
        bool frames = true;
        for (int i = 0; i < 15; ++i) {
            frames &= !cache.videoFrame(vid, 0.2 + i * 0.033, 640).isNull();
            frames &= !cache.videoFrame(vid, 3.0 + i * 0.033, 640).isNull();
        }
        // one shared decoder ping-pongs at ~260 ms here; lanes need ~35 ms
        if (!frames || timer.elapsed() > 150) {
            fprintf(stderr,
                    "selftest: alternating decode too slow (%lld ms, ok=%d)\n",
                    timer.elapsed(), int(frames));
            return 1;
        }
    }
    {
        // cross dissolve across a cut must blend in the outgoing clip
        // (a black result means the previous clip wasn't drawn)
        Document d5;
        const QString s5 = d5.sequenceFromMedia(d5.importMedia({vid}).first());
        d5.splitAt(s5, 2.0, false);
        Sequence *sq = d5.project().sequenceById(s5);
        sq->videoTracks[0].clips[1].transIn = {TransitionType::CrossDissolve,
                                               1.0};
        Compositor c5(&d5.project(), d5.mutex());
        // 20 ms into the dissolve the incoming clip is ~invisible, so the
        // frame must still show the colorful outgoing testsrc2
        QImage img = c5.renderFrame(s5, 2.02, 0.5);
        int peak = 0;
        for (int y = 0; y < img.height(); y += 8)
            for (int x = 0; x < img.width(); x += 8) {
                QRgb px = img.pixel(x, y);
                peak = qMax(peak, qRed(px) + qGreen(px) + qBlue(px));
            }
        if (peak < 120) {
            fprintf(stderr,
                    "selftest: cross dissolve fades from black (peak=%d)\n",
                    peak);
            return 1;
        }
        // same with a *nested* outgoing clip: a nested sequence has no
        // media past its end, so the dissolve must hold its last frame
        sq = d5.project().sequenceById(s5);
        d5.setSelectedClips({sq->videoTracks[0].clips[0].id,
                             sq->audioTracks[0].clips[0].id});
        d5.nestClips(s5, d5.selectedClips());
        sq = d5.project().sequenceById(s5);
        sq->videoTracks[0].clips[1].transIn = {TransitionType::CrossDissolve,
                                               1.0};
        img = c5.renderFrame(s5, 2.02, 0.5);
        peak = 0;
        for (int y = 0; y < img.height(); y += 8)
            for (int x = 0; x < img.width(); x += 8) {
                QRgb px = img.pixel(x, y);
                peak = qMax(peak, qRed(px) + qGreen(px) + qBlue(px));
            }
        if (peak < 120) {
            fprintf(stderr,
                    "selftest: nested cross dissolve fades from black "
                    "(peak=%d)\n",
                    peak);
            return 1;
        }
    }
    {
        // media in/out points trim clips dropped into a sequence
        Document d4;
        const QString mid = d4.importMedia({vid}).first();
        d4.setMediaInOut(mid, 1.0, 3.0);
        const QString s4 = d4.createSequence("inout", 640, 360, 30)->id;
        const QList<quint64> ids =
            d4.addMediaClip(s4, mid, TrackType::Video, 0, 0.0);
        const Sequence *sq = d4.project().sequenceByIdConst(s4);
        const Clip &c = sq->videoTracks[0].clips[0];
        if (ids.size() != 2 || std::abs(c.in - 1.0) > 1e-6 ||
            std::abs(c.duration - 2.0) > 1e-6) {
            fprintf(stderr, "selftest: in/out trim failed (in=%.3f dur=%.3f)\n",
                    c.in, c.duration);
            return 1;
        }
        // relocating media keeps the id and re-probes the new file
        const QString vid2 = dir + "/test2.mp4";
        QFile::remove(vid2);
        QFile::copy(vid, vid2);
        if (!d4.relocateMedia(mid, vid2)) {
            fprintf(stderr, "selftest: relocateMedia failed\n");
            return 1;
        }
        const MediaItem *m = d4.project().mediaByIdConst(mid);
        if (!m || m->offline || m->path != vid2 || m->duration < 3.0) {
            fprintf(stderr, "selftest: relocated media is wrong\n");
            return 1;
        }
    }

    // save & reload round-trip
    const QString proj = dir + "/test.velo";
    if (!doc.saveProject(proj) || !doc.loadProject(proj)) {
        fprintf(stderr, "selftest: project save/load failed\n");
        return 1;
    }

    ExportSettings es;
    es.outputPath = dir + "/out.mp4";
    es.width = 640;
    es.height = 360;
    es.fps = 30;
    es.crf = 28;
    QString msg;
    auto runExport = [&](const QString &out) {
        ExportSettings s = es;
        s.outputPath = out;
        Exporter exp(&doc.project(), doc.mutex(), seqId, s);
        bool ok = false;
        QEventLoop loop;
        QObject::connect(&exp, &Exporter::finished, &loop,
                         [&](bool o, const QString &m) {
                             ok = o;
                             msg = m;
                             loop.quit();
                         });
        exp.start();
        loop.exec();
        exp.wait(10000);
        return ok;
    };

    // exporting on top of a file the project reads from must be refused,
    // not truncated half-way through the render
    // (the last spelling is the same file by a non-canonical path)
    for (const QString &spelling :
         {vid, proj, dir + "/./" + QFileInfo(vid).fileName()}) {
        const QString real = QFileInfo(spelling).canonicalFilePath();
        const qint64 before = QFileInfo(real).size();
        if (runExport(spelling)) {
            fprintf(stderr, "selftest: export overwrote source %s\n",
                    qPrintable(spelling));
            return 1;
        }
        if (QFileInfo(real).size() != before) {
            fprintf(stderr, "selftest: source %s was modified\n",
                    qPrintable(real));
            return 1;
        }
    }

    if (!runExport(es.outputPath)) {
        fprintf(stderr, "selftest: export failed: %s\n", qPrintable(msg));
        return 1;
    }
    printf("selftest: OK (render, mix, save/load, export -> %s)\n",
           qPrintable(es.outputPath));
    return 0;
}

// Running as root (no own audio session): borrow the desktop user's
// PipeWire/PulseAudio socket so playback isn't silent.
static void adoptUserAudioSession() {
#ifdef Q_OS_UNIX
    if (geteuid() != 0 || !qEnvironmentVariableIsEmpty("PULSE_SERVER")) return;
    const QDir run("/run/user");
    for (const QFileInfo &fi :
         run.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString sock = fi.filePath() + "/pulse/native";
        if (!QFile::exists(sock)) continue;
        qputenv("PULSE_SERVER", ("unix:" + sock).toUtf8());
        if (const passwd *pw = getpwuid(uid_t(fi.fileName().toUInt()))) {
            const QString cookie =
                QString(pw->pw_dir) + "/.config/pulse/cookie";
            if (QFile::exists(cookie)) qputenv("PULSE_COOKIE", cookie.toUtf8());
        }
        break;
    }
#endif
}

int main(int argc, char *argv[]) {
    adoptUserAudioSession();
    QApplication app(argc, argv);
    app.setApplicationName("Velo");
    app.setOrganizationName("velo");
    app.setApplicationVersion(QStringLiteral(VELO_VERSION));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"selftest", "Run a headless engine self test and exit."});
    parser.addPositionalArgument("project", "Project file (.velo) to open.");
    parser.process(app);

    if (parser.isSet("selftest")) return selftest();

    Theme::apply(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/velo.svg")));
    MainWindow win;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) win.document()->loadProject(args.first());
    win.show();
    return app.exec();
}
