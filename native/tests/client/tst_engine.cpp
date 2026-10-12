// Testes unitários do motor: detecção de problemas (limiares do TS),
// layout e filtro de SDP. Mesmos cenários de test/stats.test.js e test/layout.test.js.

#include "AudioCodec.h"
#include "Layout.h"
#include "SdpFilter.h"
#include "StreamStats.h"
#include "VideoDecoder.h"
#include "VideoEncoder.h"

#include <QImage>
#include <QTest>

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

    void encodeDecodeRoundTrip()
    {
        // Codifica 10 quadros com o melhor encoder disponível e decodifica de volta.
        const QStringList encoders = VideoEncoder::availableEncoders();
        QVERIFY2(!encoders.isEmpty(), "nenhum encoder H.264");
        qInfo() << "encoders disponíveis:" << encoders;

        QList<EncodedVideoFrame> packets;
        std::mutex m;
        VideoEncoder enc;
        VideoEncoderConfig cfg;
        cfg.maxWidth = 640;
        cfg.maxHeight = 360;
        cfg.fps = 1000; // não descartar quadros no teste
        cfg.bitrate = 1'000'000;
        enc.start(cfg, [&](const EncodedVideoFrame &f) {
            std::lock_guard lock(m);
            packets.append(f);
        });
        QImage img(1280, 720, QImage::Format_RGBX8888);
        for (int i = 0; i < 10; ++i) {
            img.fill(QColor::fromHsv(i * 30, 200, 200));
            enc.pushFrame(QVideoFrame(img));
            QTest::qWait(30);
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
