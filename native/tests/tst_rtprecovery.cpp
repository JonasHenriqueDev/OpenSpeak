// Recuperação de perda RTP (NACK + descarte de quadros incompletos).

#include "RtpRecovery.h"

#include <QTest>

using namespace openspeak;
using namespace std::chrono_literals;
using Buffer = RtpRecoveryBuffer;
using Clock = Buffer::Clock;

namespace {

constexpr uint32_t Ssrc = 0x1234;

/// Pacote RTP H.264 com um NAL simples do tipo dado.
rtc::message_ptr rtp(uint16_t seq, uint32_t ts, bool marker, uint8_t nalType)
{
    rtc::binary b(12 + 2);
    b[0] = std::byte(0x80);
    b[1] = std::byte((marker ? 0x80 : 0) | 96);
    b[2] = std::byte(seq >> 8);
    b[3] = std::byte(seq & 0xFF);
    for (int i = 0; i < 4; ++i) {
        b[4 + i] = std::byte(ts >> (24 - 8 * i));
        b[8 + i] = std::byte(Ssrc >> (24 - 8 * i));
    }
    b[12] = std::byte(0x60 | nalType);
    b[13] = std::byte(0xAB);
    return rtc::make_message(std::move(b));
}

uint16_t seqOf(const rtc::message_ptr &m) { return uint16_t(std::to_integer<uint8_t>((*m)[2]) << 8 | std::to_integer<uint8_t>((*m)[3])); }

/// Keyframe = SPS, PPS, IDR (marker). P-frame = 2 slices (marker no último).
struct Stream {
    uint16_t seq;
    uint32_t ts = 1000;
    explicit Stream(uint16_t first) : seq(first) {}
    std::vector<rtc::message_ptr> key()
    {
        ts += 3000;
        return {rtp(seq++, ts, false, 7), rtp(seq++, ts, false, 8), rtp(seq++, ts, true, 5)};
    }
    std::vector<rtc::message_ptr> delta()
    {
        ts += 3000;
        return {rtp(seq++, ts, false, 1), rtp(seq++, ts, true, 1)};
    }
};

struct Harness {
    Buffer buffer;
    Clock::time_point now = Clock::now();
    std::vector<uint16_t> delivered, nacks;
    int plis = 0;

    explicit Harness(Buffer::Mode mode = Buffer::Mode::Frames) : buffer(mode) {}

    void collect(rtc::message_vector &out, Buffer::Events &ev)
    {
        for (const auto &m : out)
            delivered.push_back(seqOf(m));
        nacks.insert(nacks.end(), ev.nack.begin(), ev.nack.end());
        plis += ev.keyFrame ? 1 : 0;
    }
    void push(const rtc::message_ptr &m)
    {
        rtc::message_vector out;
        Buffer::Events ev;
        buffer.push(m, now, out, ev);
        buffer.poll(now, out, ev);
        collect(out, ev);
    }
    void push(const std::vector<rtc::message_ptr> &ms)
    {
        for (const auto &m : ms)
            push(m);
    }
    void advance(Clock::duration d)
    {
        now += d;
        rtc::message_vector out;
        Buffer::Events ev;
        buffer.poll(now, out, ev);
        collect(out, ev);
    }
};

std::vector<uint16_t> seqs(const std::vector<rtc::message_ptr> &ms)
{
    std::vector<uint16_t> r;
    for (const auto &m : ms)
        r.push_back(seqOf(m));
    return r;
}

std::vector<uint16_t> operator+(std::vector<uint16_t> a, const std::vector<uint16_t> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

} // namespace

class TestRtpRecovery : public QObject {
    Q_OBJECT

private slots:
    void inOrderPassesCompleteFrames()
    {
        Harness h;
        Stream s(100);
        const auto k = s.key(), d1 = s.delta(), d2 = s.delta();
        h.push(k);
        h.push(d1);
        h.push(d2);
        QCOMPARE(h.delivered, seqs(k) + seqs(d1) + seqs(d2));
        QVERIFY(h.nacks.empty());
        QCOMPARE(h.buffer.stats().droppedFrames, uint64_t(0));
    }

    void reorderedPacketIsNackedAndRecovered()
    {
        Harness h;
        Stream s(100);
        const auto k = s.key(), d = s.delta();
        h.push(k);
        h.push(d[1]); // chega antes do primeiro slice
        QCOMPARE(h.delivered, seqs(k));
        QCOMPARE(h.nacks, std::vector<uint16_t>{seqOf(d[0])});
        h.now += 30ms;
        h.push(d[0]);
        QCOMPARE(h.delivered, seqs(k) + seqs(d));
        QCOMPARE(h.buffer.stats().recoveredPackets, uint64_t(1));
        QVERIFY(h.buffer.rtt() < 50ms); // aprendeu com a amostra de 30 ms
    }

    void nackIsRetriedUntilGiveUp()
    {
        Harness h;
        Stream s(100);
        const auto k = s.key(), d = s.delta();
        h.push(k);
        h.push(d[1]);
        QCOMPARE(h.nacks.size(), size_t(1));
        h.advance(h.buffer.retryInterval());
        QCOMPARE(h.nacks.size(), size_t(2));
        h.advance(h.buffer.retryInterval());
        QCOMPARE(h.nacks.size(), size_t(3));
    }

    void lostPacketDropsUntilKeyFrame()
    {
        Harness h;
        Stream s(100);
        const auto k1 = s.key(), d1 = s.delta(), d2 = s.delta(), k2 = s.key(), d3 = s.delta();
        h.push(k1);
        h.advance(1s); // longe do PLI inicial
        const int plisBefore = h.plis;
        h.push(d1[1]); // d1[0] perdido de vez
        h.push(d2);
        h.advance(h.buffer.giveUpAfter());
        QCOMPARE(h.delivered, seqs(k1)); // nada do quadro quebrado nem do seguinte
        QCOMPARE(h.plis, plisBefore + 1);
        h.push(k2);
        h.push(d3);
        QCOMPARE(h.delivered, seqs(k1) + seqs(k2) + seqs(d3));
        QCOMPARE(h.buffer.stats().lostPackets, uint64_t(1));
        QVERIFY(h.buffer.stats().droppedFrames >= 2);
    }

    void lostMarkerDoesNotLeakPartialFrame()
    {
        Harness h;
        Stream s(100);
        const auto k1 = s.key(), d1 = s.delta(), k2 = s.key();
        h.push(k1);
        h.push(d1[0]); // d1[1] (marker) perdido
        h.push(k2);
        h.advance(h.buffer.giveUpAfter());
        // O keyframe seguinte começa com SPS: é entregue mesmo sem saber o início.
        QCOMPARE(h.delivered, seqs(k1) + seqs(k2));
    }

    void wrapsAroundSequence()
    {
        Harness h;
        Stream s(65533);
        const auto k = s.key(), d = s.delta(), d2 = s.delta();
        h.push(k);
        h.push(d);
        h.push(d2[1]);
        h.push(d2[0]);
        QCOMPARE(h.delivered, seqs(k) + seqs(d) + seqs(d2));
        QCOMPARE(h.nacks, std::vector<uint16_t>{seqOf(d2[0])});
    }

    void startingMidStreamWaitsForKeyFrame()
    {
        Harness h;
        Stream s(100);
        const auto d1 = s.delta(), d2 = s.delta(), k = s.key();
        h.push(d1[1]); // entrou no meio de um quadro
        h.push(d2);
        h.push(k);
        QCOMPARE(h.delivered, seqs(k));
        QVERIFY(h.plis >= 1);
    }

    void duplicatesAreDropped()
    {
        Harness h;
        Stream s(100);
        const auto k = s.key();
        h.push(k);
        h.push(k[1]);
        QCOMPARE(h.delivered, seqs(k));
    }

    void passThroughForwardsImmediately()
    {
        Harness h(Buffer::Mode::PassThrough);
        Stream s(100);
        const auto k = s.key(), d = s.delta();
        h.push(k);
        h.push(d[1]);
        QCOMPARE(h.delivered, seqs(k) + std::vector<uint16_t>{seqOf(d[1])});
        QCOMPARE(h.nacks, std::vector<uint16_t>{seqOf(d[0])});
        h.push(d[0]); // reenvio passa adiante
        h.push(d[0]); // duplicado não
        QCOMPARE(h.delivered.back(), seqOf(d[0]));
        QCOMPARE(h.delivered.size(), size_t(5));
        QCOMPARE(h.plis, 0);
    }

    void nackEncoding()
    {
        const auto b = buildGenericNack(1, Ssrc, {100, 101, 105, 116, 117, 200});
        auto at = [&](size_t i) { return std::to_integer<uint8_t>(b[i]); };
        QCOMPARE(b.size(), size_t(12 + 3 * 4));
        QCOMPARE(at(0), uint8_t(0x81));
        QCOMPARE(at(1), uint8_t(205));
        QCOMPARE(int(at(2) << 8 | at(3)), 2 + 3);
        // FCI 1: PID 100, BLP com 101 (bit 0), 105 (bit 4) e 116 (bit 15)
        QCOMPARE(int(at(12) << 8 | at(13)), 100);
        QCOMPARE(int(at(14) << 8 | at(15)), (1 << 0) | (1 << 4) | (1 << 15));
        QCOMPARE(int(at(16) << 8 | at(17)), 117);
        QCOMPARE(int(at(18) << 8 | at(19)), 0);
        QCOMPARE(int(at(20) << 8 | at(21)), 200);
    }
};

QTEST_GUILESS_MAIN(TestRtpRecovery)
#include "tst_rtprecovery.moc"
