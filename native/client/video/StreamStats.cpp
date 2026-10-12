#include "StreamStats.h"

#include <QVariantList>
#include <cmath>

namespace openspeak {

QString formatBitrate(double bps, int digits)
{
    if (!std::isfinite(bps) || bps <= 0)
        return QStringLiteral("0 bps");
    static const char *units[] = {"bps", "Kbps", "Mbps", "Gbps"};
    int i = 0;
    while (bps >= 1000 && i < 3) {
        bps /= 1000;
        ++i;
    }
    return QStringLiteral("%1 %2").arg(bps, 0, 'f', i == 0 ? 0 : digits).arg(QLatin1String(units[i]));
}

QString formatBytes(double bytes, int digits)
{
    if (!std::isfinite(bytes) || bytes <= 0)
        return QStringLiteral("0 B");
    static const char *units[] = {"B", "KiB", "MiB", "GiB"};
    int i = 0;
    while (bytes >= 1024 && i < 3) {
        bytes /= 1024;
        ++i;
    }
    return QStringLiteral("%1 %2").arg(bytes, 0, 'f', i == 0 ? 0 : digits).arg(QLatin1String(units[i]));
}

static QString ms(double seconds) { return QStringLiteral("%1ms").arg(std::lround(1000 * seconds)); }

void analyzeIssues(StatsSummary &s, const TargetQuality *target, bool checkEncoder)
{
    s.issues.clear();
    s.issueSeverity.clear();
    s.issueStatus = SeverityOk;
    auto push = [&s](const char *category, const char *name, QString value, int severity, QString expected = {}) {
        s.issues.append({QLatin1String(category), QLatin1String(name), std::move(value), std::move(expected), severity});
        s.issueStatus |= severity;
        s.issueSeverity.insert(QStringLiteral("%1:%2").arg(QLatin1String(category), QLatin1String(name)), severity);
    };

    if (s.fractionLost > 0.001)
        push("network", "packetloss", QStringLiteral("%1%").arg(100 * s.fractionLost, 0, 'f', 2),
             s.fractionLost > 0.05 ? SeverityMajor : SeverityMinor);
    if (s.jitter > 0.1)
        push("network", "jitter", ms(s.jitter), SeverityMinor);
    if (s.lastRoundTripTime > 0.3)
        push("network", "rtt", ms(s.lastRoundTripTime), s.lastRoundTripTime > 0.6 ? SeverityMajor : SeverityMinor);
    if (s.sent.currentDelay > 5)
        push("network", "packetdelay", ms(s.sent.currentDelay), SeverityMinor);
    if (s.retransmit.bps > 0)
        push("network", "retransmit", formatBitrate(s.retransmit.bps), SeverityMinor);

    if (s.kind == QLatin1String("video") && checkEncoder && s.video.hasEncode) {
        if (!s.video.limitReason.isEmpty() && s.video.limitReason != QLatin1String("none")) {
            const bool major = target && target->width > 0
                && double(s.video.width) * s.video.height < double(target->width) * target->height * 0.33;
            push("video", "qualitylimit", s.video.limitReason, major ? SeverityMajor : SeverityMinor);
        }
        if (target && target->fps > 0) {
            const double frameTime = 1.0 / target->fps;
            if (s.video.avgEncodeTime > frameTime)
                push("video", "encodetime", ms(s.video.avgEncodeTime),
                     s.video.avgEncodeTime > 1.5 * frameTime ? SeverityMajor : SeverityMinor);
            if (s.video.fps < 0.8 * target->fps)
                push("video", "fps", QString::number(s.video.fps), s.video.fps < 0.6 * target->fps ? SeverityMajor : SeverityMinor,
                     QString::number(target->fps));
        }
    }
}

QVariantMap StatsSummary::toVariant() const
{
    QVariantList issueList;
    for (const StreamIssue &i : issues)
        issueList.append(QVariantMap{{"category", i.category}, {"name", i.name}, {"value", i.value},
                                     {"expected", i.expected}, {"severity", i.severity}});
    QVariantMap severity;
    for (auto it = issueSeverity.begin(); it != issueSeverity.end(); ++it)
        severity.insert(it.key(), it.value());
    return {
        {"kind", kind}, {"codec", codec}, {"packetsLost", packetsLost}, {"fractionLost", fractionLost},
        {"jitter", jitter}, {"lastRoundTripTime", lastRoundTripTime}, {"avgRoundTripTime", avgRoundTripTime},
        {"sentPackets", sent.packets}, {"sentBytes", sent.bytes}, {"sentBps", sent.bps},
        {"currentDelay", sent.currentDelay}, {"totalDelay", sent.totalDelay},
        {"receivedPackets", received.packets}, {"receivedBytes", received.bytes}, {"receivedBps", received.bps},
        {"retransmitPackets", retransmit.packets}, {"retransmitBytes", retransmit.bytes}, {"retransmitBps", retransmit.bps},
        {"targetBitrate", targetBitrate}, {"srtpCipher", srtpCipher}, {"connectionType", connectionType},
        {"width", video.width}, {"height", video.height}, {"fps", video.fps},
        {"encoder", video.encoder}, {"avgEncodeTime", video.avgEncodeTime}, {"limitReason", video.limitReason},
        {"decoder", video.decoder}, {"avgDecodeTime", video.avgDecodeTime},
        {"issueStatus", issueStatus}, {"worst", worstSeverity()}, {"issues", issueList}, {"issueSeverity", severity},
    };
}

} // namespace openspeak
