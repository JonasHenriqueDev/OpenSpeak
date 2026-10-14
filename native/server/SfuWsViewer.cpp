#include "SfuWsViewer.h"

#include <QMetaObject>

#include <atomic>
#include <mutex>

namespace openspeak {

namespace {

uint8_t u8(const std::byte *p) { return std::to_integer<uint8_t>(*p); }

constexpr uint8_t NalIdr = 5, NalSps = 7, NalStapA = 24, NalFuA = 28;
bool startsKeyFrame(uint8_t nalType) { return nalType == NalIdr || nalType == NalSps; }

} // namespace

// Roda nas threads da libdatachannel: só acumula e agenda o flush na thread do Qt.
class SfuWsViewer::Sink : public sfu::EgressSink {
public:
    explicit Sink(SfuWsViewer *owner) : m_owner(owner) {}

    void send(sfu::Kind kind, const rtc::binary &packet) override
    {
        if (packet.size() < 12 || packet.size() > 0xFFFF)
            return;
        std::lock_guard lock(m_mutex);
        if (!m_owner)
            return;
        if (kind == sfu::Kind::Video && m_waitKeyFrame) {
            if (!isKeyFrameStart(packet.data(), packet.size())) {
                ++m_dropped;
                return;
            }
            m_waitKeyFrame = false;
        }
        const char header[3] = {char(kind), char(packet.size() >> 8), char(packet.size() & 0xFF)};
        m_buffer.append(header, 3);
        m_buffer.append(reinterpret_cast<const char *>(packet.data()), qsizetype(packet.size()));
        if (!m_flushQueued) {
            m_flushQueued = true;
            // Eventos pendentes de um QObject destruído são descartados pelo Qt;
            // stop() zera m_owner sob o mutex antes da destruição.
            SfuWsViewer *owner = m_owner;
            QMetaObject::invokeMethod(owner, [owner] { owner->flush(); }, Qt::QueuedConnection);
        }
    }

    QByteArray take()
    {
        std::lock_guard lock(m_mutex);
        m_flushQueued = false;
        QByteArray batch;
        batch.swap(m_buffer);
        return batch;
    }

    void waitKeyFrame()
    {
        std::lock_guard lock(m_mutex);
        m_waitKeyFrame = true;
    }

    void detach()
    {
        std::lock_guard lock(m_mutex);
        m_owner = nullptr;
        m_buffer.clear();
    }

    uint64_t dropped() const
    {
        std::lock_guard lock(m_mutex);
        return m_dropped;
    }

    std::atomic<uint64_t> sentBytes{0};
    std::atomic<uint64_t> congestions{0};

private:
    mutable std::mutex m_mutex;
    SfuWsViewer *m_owner;
    QByteArray m_buffer;
    bool m_flushQueued = false;
    bool m_waitKeyFrame = true; // começa num keyframe
    uint64_t m_dropped = 0;
};

SfuWsViewer::SfuWsViewer(std::shared_ptr<sfu::Forwarder> forwarder, SendFn send, QObject *parent)
    : QObject(parent), m_forwarder(std::move(forwarder)), m_sink(std::make_shared<Sink>(this)), m_send(std::move(send))
{
    m_forwarder->addSink(m_sink);
    m_forwarder->requestKeyFrame();
}

SfuWsViewer::~SfuWsViewer() { stop(); }

void SfuWsViewer::stop()
{
    m_forwarder->removeSink(m_sink);
    m_sink->detach();
}

void SfuWsViewer::requestKeyFrame()
{
    m_sink->waitKeyFrame();
    m_forwarder->requestKeyFrame();
}

void SfuWsViewer::flush()
{
    const QByteArray batch = m_sink->take();
    if (batch.isEmpty())
        return;
    const qint64 pending = m_send(batch);
    if (pending < 0)
        return;
    m_sink->sentBytes += uint64_t(batch.size());
    if (pending > MaxPendingBytes) {
        ++m_sink->congestions;
        requestKeyFrame();
    }
}

QJsonObject SfuWsViewer::mediaSnapshot() const
{
    // Mesmos campos das sessões WebRTC (SfuEgress): o monitor lê mediaRxBytes/mediaTxBytes.
    return {
        {"state", "connected"},
        {"connectionType", "websocket"},
        {"transport", "websocket"},
        {"mediaRxBytes", 0},
        {"mediaTxBytes", double(m_sink->sentBytes.load())},
        {"droppedVideoPackets", double(m_sink->dropped())},
        {"congestions", double(m_sink->congestions.load())},
    };
}

bool SfuWsViewer::isKeyFrameStart(const std::byte *data, size_t size)
{
    if (size < 13)
        return false;
    const uint8_t b0 = u8(data);
    size_t at = 12 + 4 * size_t(b0 & 0x0F);
    if (b0 & 0x10) {
        if (size < at + 4)
            return false;
        at += 4 + 4 * size_t(u8(data + at + 2) << 8 | u8(data + at + 3));
    }
    if (size <= at)
        return false;
    const std::byte *p = data + at;
    const size_t n = size - at;
    const uint8_t type = u8(p) & 0x1F;
    if (type == NalStapA) {
        for (size_t i = 1; i + 2 < n;) {
            const size_t len = size_t(u8(p + i) << 8 | u8(p + i + 1));
            if (len == 0 || i + 2 + len > n)
                break;
            if (startsKeyFrame(u8(p + i + 2) & 0x1F))
                return true;
            i += 2 + len;
        }
        return false;
    }
    if (type == NalFuA)
        return n >= 2 && (u8(p + 1) & 0x80) && startsKeyFrame(u8(p + 1) & 0x1F);
    return startsKeyFrame(type);
}

} // namespace openspeak
