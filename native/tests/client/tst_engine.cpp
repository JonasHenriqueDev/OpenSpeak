// Testes unitários do motor: detecção de problemas (limiares do TS),
// layout e filtro de SDP. Mesmos cenários de test/stats.test.js e test/layout.test.js.

#include "AudioCapture.h"
#include "AudioCodec.h"
#include "Layout.h"
#include "SdpFilter.h"
#include "StreamStats.h"
#include "VideoDecoder.h"
#include "VideoEncoder.h"
#include "SourceThumbnails.h"

#include <QImage>
#include <QSignalSpy>
#include <QTest>
#include <QWindow>
#include <windows.h>

using namespace openspeak;

class TestEngine : public QObject {
    Q_OBJECT

    static StatsSummary video()
    {
        StatsSummary s;
        s.video.width = 1280;
        s.video.height = 720;
        s.video.fps = 30;
        s.video.hasEncode = true;
        s.video.avgEncodeTime = 0.01;
        return s;
    }

private slots:
    void picksDiscordProcessTreeRoot()
    {
        // Raiz (pai é o Update.exe, fora da lista) com filhos, como o Discord real.
        QCOMPARE(AudioCapture::rootProcess({{500, 900}, {610, 500}, {620, 500}, {630, 610}}), 500u);
        // Filhos listados antes da raiz.
        QCOMPARE(AudioCapture::rootProcess({{620, 500}, {500, 900}}), 500u);
        // Duas instâncias independentes (estável + PTB): a de menor PID.
        QCOMPARE(AudioCapture::rootProcess({{800, 1}, {810, 800}, {700, 2}}), 700u);
        QCOMPARE(AudioCapture::rootProcess({}), 0u);
    }

    void excludingDiscordReportsWhatIsExcluded()
    {
        AudioCapture capture;
        QSignalSpy excluded(&capture, &AudioCapture::excludedChanged);
        QSignalSpy errors(&capture, &AudioCapture::errorOccurred);
        capture.start(AudioCapture::Mode::SystemExcludingDiscord);
        QTRY_VERIFY_WITH_TIMEOUT(!excluded.isEmpty() || !errors.isEmpty(), 5000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        const QString expected = AudioCapture::discordRootPid() ? QStringLiteral("discord") : QStringLiteral("self");
        QCOMPARE(excluded.first().first().toString(), expected);
        capture.stop();
    }

    void filtersShellAndBackgroundWindows()
    {
        QWindow normal;
        normal.setTitle("OpenSpeak filter test");
        normal.resize(320, 180);
        normal.show();
        ShowWindow(reinterpret_cast<HWND>(normal.winId()), SW_SHOWNORMAL);
        const QCapturableWindow descriptor(&normal);
        QTRY_VERIFY(VideoSource::isShareableWindow(descriptor));
        normal.setTitle("");
        SetWindowTextW(reinterpret_cast<HWND>(normal.winId()), L"");
        QVERIFY(!VideoSource::isShareableWindow(descriptor));
        normal.setTitle("Settings"); // real Settings windows remain shareable
        QVERIFY(VideoSource::isShareableWindow(descriptor));
        normal.hide();
        QVERIFY(!VideoSource::isShareableWindow(descriptor));
        normal.show();
        const HWND hwnd = reinterpret_cast<HWND>(normal.winId());
        const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_TOOLWINDOW);
        QVERIFY(!VideoSource::isShareableWindow(descriptor));
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_NOACTIVATE);
        QVERIFY(!VideoSource::isShareableWindow(descriptor));
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
        QVERIFY(VideoSource::isShareableWindow(descriptor));
    }
    void vanishedSourceDoesNotSelectAnotherDevice()
    {
        VideoSource capture;
        SourceDescriptor window;
        window.type = StreamType::Window;
        window.id = "0";
        window.windowSpecified = true;
        QVERIFY(!capture.open(window, 5));
        QCOMPARE(capture.errorString(), QStringLiteral("no_available_source"));
        SourceDescriptor camera;
        camera.type = StreamType::Camera;
        camera.id = "openspeak-nonexistent-camera";
        QVERIFY(!capture.open(camera, 5));
        QVERIFY(!capture.isActive());
    }

    void sourceThumbnails()
    {
        SourceThumbnails thumbnails;
        QSignalSpy ready(&thumbnails, &SourceThumbnails::ready);
        const QVariantList sources{QVariantMap{{"type", 2}, {"id", "test"}, {"test", true}}};
        thumbnails.start(sources);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 3000);
        QCOMPARE(ready.first()[0].toString(), QStringLiteral("test:2"));
        const QString image = ready.first()[1].toString();
        QVERIFY(image.startsWith("data:image/jpeg;base64,"));
        const auto bytes = QByteArray::fromBase64(image.section(',', 1).toLatin1());
        QCOMPARE(QImage::fromData(bytes).size(), QSize(320, 180));
        thumbnails.start(sources);
        thumbnails.stop();
        QTest::qWait(300);
        QCOMPARE(ready.count(), 1); // closing the picker cancels queued captures
        thumbnails.start({});
        QTest::qWait(100);
        QCOMPARE(ready.count(), 1);
    }

    void noIssues()
    {
        StatsSummary s = video();
        TargetQuality t{1280, 720, 30};
        analyzeIssues(s, &t, true);
        QCOMPARE(s.issueStatus, int(SeverityOk));
        QVERIFY(s.issues.isEmpty());
    }

    void networkThresholds()
    {
        TargetQuality t{1280, 720, 30};
        StatsSummary minor = video();
        minor.fractionLost = 0.01;
        minor.lastRoundTripTime = 0.4;
        minor.jitter = 0.2;
        analyzeIssues(minor, &t, true);
        QCOMPARE(minor.issueSeverity.value("network:packetloss"), int(SeverityMinor));
        QCOMPARE(minor.issueSeverity.value("network:rtt"), int(SeverityMinor));
        QCOMPARE(minor.issueSeverity.value("network:jitter"), int(SeverityMinor));

        StatsSummary major = video();
        major.fractionLost = 0.06;
        major.lastRoundTripTime = 0.7;
        analyzeIssues(major, &t, true);
        QCOMPARE(major.issueSeverity.value("network:packetloss"), int(SeverityMajor));
        QCOMPARE(major.issueSeverity.value("network:rtt"), int(SeverityMajor));

        StatsSummary s = video();
        s.sent.currentDelay = 6;
        s.retransmit.bps = 1000;
        analyzeIssues(s, &t, true);
        QCOMPARE(s.issueSeverity.value("network:packetdelay"), int(SeverityMinor));
        QCOMPARE(s.issueSeverity.value("network:retransmit"), int(SeverityMinor));
    }

    void encoderThresholds()
    {
        TargetQuality t{1280, 720, 30};
        StatsSummary s = video();
        s.video.width = 640;
        s.video.height = 360;
        s.video.fps = 15;
        s.video.avgEncodeTime = 0.06;
        s.video.limitReason = QStringLiteral("cpu");
        analyzeIssues(s, &t, true);
        QCOMPARE(s.issueSeverity.value("video:qualitylimit"), int(SeverityMajor));
        QCOMPARE(s.issueSeverity.value("video:encodetime"), int(SeverityMajor));
        QCOMPARE(s.issueSeverity.value("video:fps"), int(SeverityMajor));

        StatsSummary inbound = video();
        inbound.video.limitReason = QStringLiteral("cpu");
        analyzeIssues(inbound, &t, false);
        QVERIFY(inbound.issues.isEmpty());
    }

    void gridLayouts()
    {
        auto one = gridLayout(1, 1600, 900);
        QCOMPARE(one.tiles.size(), 1);
        QCOMPARE(one.tiles[0].rect.width(), 1600.0);
        auto four = gridLayout(4, 1608, 908, 8);
        QCOMPARE(four.perPage, 4);
        for (int n : {2, 3, 5, 7, 12}) {
            for (const auto &t : gridLayout(n, 1200, 700).tiles) {
                QVERIFY(t.rect.right() <= 1200 && t.rect.bottom() <= 700);
                QVERIFY(t.rect.height() >= MinTileHeight);
            }
        }
        auto paged = gridLayout(50, 600, 300);
        QVERIFY(paged.perPage < 50);
        auto focus = focusLayout(4, 2, 1280, 800);
        QCOMPARE(focus.tiles[0].index, 2);
        QCOMPARE(focus.tiles.size(), 4);
    }

    void sdpFilterKeepsOnlyH264AndOpus()
    {
        const QString offer = QStringLiteral(
            "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111 63\r\na=mid:0\r\na=rtpmap:111 opus/48000/2\r\na=fmtp:111 minptime=10\r\n"
            "a=rtpmap:63 red/48000/2\r\na=fmtp:63 111/111\r\n"
            "m=video 9 UDP/TLS/RTP/SAVPF 45 96 97 103\r\na=mid:1\r\na=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
            "a=rtpmap:45 AV1/90000\r\na=rtcp-fb:45 transport-cc\r\n"
            "a=rtpmap:96 H264/90000\r\na=rtcp-fb:96 nack\r\na=rtcp-fb:96 transport-cc\r\na=fmtp:96 packetization-mode=1\r\n"
            "a=rtpmap:97 rtx/90000\r\na=fmtp:97 apt=96\r\na=rtpmap:103 ulpfec/90000\r\n"
            "a=ssrc-group:FID 1 2\r\n");
        const QString out = sdp::filterCodecs(offer, {"H264"}, {"opus"});
        QVERIFY(out.contains("m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"));
        QVERIFY(out.contains("m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"));
        QVERIFY(!out.contains("AV1"));
        QVERIFY(!out.contains("rtx"));
        QVERIFY(!out.contains("ulpfec"));
        QVERIFY(!out.contains("transport-cc"));
        QVERIFY(!out.contains("FID"));
        QVERIFY(out.contains("a=rtcp-fb:96 nack"));

        const QString stereo = sdp::opusStereo(out, 256000);
        QVERIFY(stereo.contains("a=fmtp:111 minptime=10;"));
        QVERIFY(stereo.contains("stereo=1"));
        QVERIFY(stereo.contains("maxaveragebitrate=256000"));
    }

    void sdpSfuHelpers()
    {
        // Modo SFU: só H.264 Constrained Baseline com packetization-mode=1.
        const QString offer = QStringLiteral(
            "v=0\r\ns=-\r\nm=video 9 UDP/TLS/RTP/SAVPF 102 104 106\r\n"
            "a=rtpmap:102 H264/90000\r\na=rtcp-fb:102 nack\r\na=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f\r\n"
            "a=rtpmap:104 H264/90000\r\na=fmtp:104 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640c1f\r\n"
            "a=rtpmap:106 H264/90000\r\na=rtcp-fb:106 nack\r\na=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n");
        const QString out = sdp::keepH264ConstrainedBaseline(offer);
        QVERIFY(out.contains("m=video 9 UDP/TLS/RTP/SAVPF 106\r\n"));
        QVERIFY(!out.contains("a=rtpmap:102"));
        QVERIFY(!out.contains("640c1f"));
        QVERIFY(out.contains("a=rtcp-fb:106 nack"));
        // Sem nenhuma variante compatível, não mexe.
        QCOMPARE(sdp::keepH264ConstrainedBaseline(QStringLiteral("v=0\r\nm=video 9 X 96\r\na=rtpmap:96 H264/90000\r\n")),
                 QStringLiteral("v=0\r\nm=video 9 X 96\r\na=rtpmap:96 H264/90000\r\n"));

        // NAT 1:1: candidato host IPv4 passa a anunciar o IP público.
        const QString host = QStringLiteral("candidate:1 1 UDP 2122317823 10.0.0.5 10000 typ host");
        QCOMPARE(sdp::advertiseHostIp(host, "203.0.113.10"), QStringLiteral("candidate:1 1 UDP 2122317823 203.0.113.10 10000 typ host"));
        const QString srflx = QStringLiteral("a=candidate:2 1 UDP 1686052607 203.0.113.10 10000 typ srflx raddr 10.0.0.5 rport 10000");
        QCOMPARE(sdp::advertiseHostIp(srflx, "203.0.113.10"), srflx);
        const QString v6 = QStringLiteral("candidate:3 1 UDP 2122317823 fe80::1 10000 typ host");
        QCOMPARE(sdp::advertiseHostIp(v6, "203.0.113.10"), v6);
        const QString sdpText = QStringLiteral("v=0\r\nm=video 9 X 96\r\na=candidate:1 1 UDP 2122317823 172.17.0.1 10000 typ host\r\n");
        QVERIFY(sdp::advertiseHostIp(sdpText, "203.0.113.10").contains("a=candidate:1 1 UDP 2122317823 203.0.113.10 10000 typ host\r\n"));
        // Duas interfaces viram o mesmo candidato: a chave ignora fundação/prioridade.
        QCOMPARE(sdp::candidateKey(QStringLiteral("candidate:1 1 UDP 2122317823 203.0.113.10 10000 typ host")),
                 sdp::candidateKey(QStringLiteral("candidate:9 1 udp 2122252543 203.0.113.10 10000 typ host")));
    }

    void encodeDecodeRoundTrip()
    {
        // Alimenta o encoder durante sua inicialização e decodifica a saída.
        const QStringList encoders = VideoEncoder::availableEncoders();
        QVERIFY2(!encoders.isEmpty(), "nenhum encoder H.264");
        qInfo() << "encoders disponíveis:" << encoders;

        QList<EncodedVideoFrame> packets;
        std::mutex m;
        VideoEncoder enc;
        VideoEncoderConfig cfg;
        cfg.maxWidth = 640;
        cfg.maxHeight = 360;
        cfg.fps = 60; // taxa suportada pelo hardware; entradas espaçadas abaixo
        cfg.bitrate = 1'000'000;
        enc.start(cfg, [&](const EncodedVideoFrame &f) {
            std::lock_guard lock(m);
            packets.append(f);
        });
        QImage img(1280, 720, QImage::Format_RGBX8888);
        for (int i = 0; i < 150; ++i) {
            img.fill(QColor::fromHsv((i * 30) % 360, 200, 200));
            enc.pushFrame(QVideoFrame(img));
            QTest::qWait(30);
            std::lock_guard lock(m);
            if (packets.size() >= 5)
                break;
        }
        QTRY_VERIFY_WITH_TIMEOUT([&] { std::lock_guard lock(m); return packets.size() >= 5; }(), 5000);
        enc.stop();
        QVERIFY(packets.first().keyFrame);
        QCOMPARE(packets.first().width, 640);

        std::atomic<int> decoded{0};
        QSize decodedSize;
        VideoDecoder dec;
        dec.start(QStringLiteral("H264"), [&](const QVideoFrame &f) {
            decodedSize = f.size();
            decoded++;
        }, [] {});
        for (const auto &p : packets)
            dec.push(p.data);
        QTRY_VERIFY_WITH_TIMEOUT(decoded >= 3, 5000);
        dec.stop();
        QCOMPARE(decodedSize, QSize(640, 360));
    }

    void opusRoundTrip()
    {
        QList<QByteArray> packets;
        OpusEncoder enc;
        QVERIFY(enc.open(128000, [&](const QByteArray &p, quint32) { packets.append(p); }));
        QByteArray pcm(48000 / 10 * 2 * 4, 0); // 100 ms de silêncio estéreo float
        enc.push(pcm, 48000, 2);
        QCOMPARE(packets.size(), 5); // 5 quadros de 20 ms
        OpusDecoder dec;
        QByteArray out;
        for (const auto &p : packets)
            out += dec.decode(p);
        QCOMPARE(out.size(), 5 * 960 * 2 * 4);
    }
};

QTEST_MAIN(TestEngine)
#include "tst_engine.moc"
