#include "SdpFilter.h"

#include <QMap>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace openspeak::sdp {

namespace {

const QString Crlf = QStringLiteral("\r\n");

QStringList splitLines(const QString &sdp)
{
    QString normalized = sdp;
    normalized.replace(Crlf, QStringLiteral("\n"));
    QStringList lines = normalized.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

QString joinLines(const QStringList &lines) { return lines.join(Crlf) + Crlf; }

} // namespace

QString filterCodecs(const QString &sdp, const QSet<QString> &allowedVideo, const QSet<QString> &allowedAudio)
{
    static const QRegularExpression rtpmapRe(QStringLiteral("^a=rtpmap:(\\d+) ([^/]+)/"));
    static const QRegularExpression ptLineRe(QStringLiteral("^a=(rtpmap|fmtp|rtcp-fb):(\\d+)"));
    static const QRegularExpression aptRe(QStringLiteral("apt=(\\d+)"));

    const QStringList lines = splitLines(sdp);
    QStringList out;

    // Processa seção por seção (m=...).
    int i = 0;
    while (i < lines.size() && !lines[i].startsWith(QLatin1String("m=")))
        out.append(lines[i++]);

    while (i < lines.size()) {
        QStringList section{lines[i++]};
        while (i < lines.size() && !lines[i].startsWith(QLatin1String("m=")))
            section.append(lines[i++]);

        const QString mline = section.first();
        const bool video = mline.startsWith(QLatin1String("m=video"));
        const bool audio = mline.startsWith(QLatin1String("m=audio"));
        if (!video && !audio) {
            out.append(section);
            continue;
        }
        const QSet<QString> &allowed = video ? allowedVideo : allowedAudio;

        // payload types permitidos
        QSet<QString> keep;
        for (const QString &l : section) {
            const auto m = rtpmapRe.match(l);
            if (m.hasMatch() && allowed.contains(m.captured(2)))
                keep.insert(m.captured(1));
        }

        QStringList mparts = mline.split(QLatin1Char(' '));
        QStringList newM = mparts.mid(0, 3);
        for (int p = 3; p < mparts.size(); ++p)
            if (keep.contains(mparts[p]))
                newM.append(mparts[p]);
        out.append(newM.join(QLatin1Char(' ')));

        for (int s = 1; s < section.size(); ++s) {
            const QString &l = section[s];
            const auto pt = ptLineRe.match(l);
            if (pt.hasMatch() && !keep.contains(pt.captured(2)))
                continue;
            if (l.contains(QLatin1String("transport-cc")) || l.contains(QLatin1String("transport-wide-cc")))
                continue;
            if (l.startsWith(QLatin1String("a=ssrc-group:FID")))
                continue; // grupo de RTX
            out.append(l);
        }
        Q_UNUSED(aptRe);
    }
    return joinLines(out);
}

QString opusStereo(const QString &sdp, int maxAverageBitrate)
{
    static const QRegularExpression opusRe(QStringLiteral("a=rtpmap:(\\d+) opus/48000/2"), QRegularExpression::CaseInsensitiveOption);
    const auto m = opusRe.match(sdp);
    if (!m.hasMatch())
        return sdp;
    const QString pt = m.captured(1);
    QStringList lines = splitLines(sdp);
    const QString prefix = QStringLiteral("a=fmtp:%1 ").arg(pt);
    const QMap<QString, QString> extra{{"stereo", "1"}, {"sprop-stereo", "1"},
                                       {"maxaveragebitrate", QString::number(maxAverageBitrate)}, {"usedtx", "0"}};
    bool found = false;
    for (QString &l : lines) {
        if (!l.startsWith(prefix))
            continue;
        found = true;
        QMap<QString, QString> params;
        QStringList order;
        for (const QString &kv : l.mid(prefix.size()).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const int eq = kv.indexOf(QLatin1Char('='));
            const QString k = kv.left(eq).trimmed();
            params[k] = eq >= 0 ? kv.mid(eq + 1).trimmed() : QString();
            if (!order.contains(k))
                order.append(k);
        }
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            params[it.key()] = it.value();
            if (!order.contains(it.key()))
                order.append(it.key());
        }
        QStringList parts;
        for (const QString &k : order)
            parts.append(k + QLatin1Char('=') + params[k]);
        l = prefix + parts.join(QLatin1Char(';'));
    }
    if (!found) {
        const int idx = lines.indexOf(m.captured(0));
        QStringList parts;
        for (auto it = extra.begin(); it != extra.end(); ++it)
            parts.append(it.key() + QLatin1Char('=') + it.value());
        if (idx >= 0)
            lines.insert(idx + 1, prefix + parts.join(QLatin1Char(';')));
    }
    return joinLines(lines);
}

QString keepH264ConstrainedBaseline(const QString &sdp)
{
    static const QRegularExpression h264Re(QStringLiteral("^a=rtpmap:(\\d+) H264/90000"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fmtpRe(QStringLiteral("^a=fmtp:(\\d+) (.*)$"));
    const QStringList lines = splitLines(sdp);
    QSet<QString> h264, good;
    QMap<QString, QString> fmtp;
    for (const QString &l : lines) {
        if (const auto m = h264Re.match(l); m.hasMatch())
            h264.insert(m.captured(1));
        else if (const auto f = fmtpRe.match(l); f.hasMatch())
            fmtp.insert(f.captured(1), f.captured(2).toLower());
    }
    for (const QString &pt : h264) {
        const QString params = fmtp.value(pt);
        // profile-level-id 42e0xx = Constrained Baseline (42 + constraint_set1).
        if (params.contains(QLatin1String("packetization-mode=1")) && params.contains(QLatin1String("profile-level-id=42e0")))
            good.insert(pt);
    }
    if (good.isEmpty() || good == h264)
        return sdp;
    QSet<QString> drop = h264 - good;
    // RTX associado (apt=) a um H.264 descartado também sai.
    static const QRegularExpression aptRe(QStringLiteral("apt=(\\d+)"));
    for (auto it = fmtp.begin(); it != fmtp.end(); ++it)
        if (const auto m = aptRe.match(it.value()); m.hasMatch() && drop.contains(m.captured(1)))
            drop.insert(it.key());

    static const QRegularExpression ptLineRe(QStringLiteral("^a=(rtpmap|fmtp|rtcp-fb):(\\d+)"));
    QStringList out;
    for (const QString &l : lines) {
        if (l.startsWith(QLatin1String("m=video"))) {
            QStringList parts = l.split(QLatin1Char(' '));
            QStringList kept = parts.mid(0, 3);
            for (int p = 3; p < parts.size(); ++p)
                if (!drop.contains(parts[p]))
                    kept.append(parts[p]);
            out.append(kept.join(QLatin1Char(' ')));
            continue;
        }
        if (const auto m = ptLineRe.match(l); m.hasMatch() && drop.contains(m.captured(2)))
            continue;
        out.append(l);
    }
    return joinLines(out);
}

namespace {

/// Reescreve uma linha de candidato (com ou sem o prefixo "a=").
QString rewriteCandidateLine(const QString &line, const QString &ip)
{
    const int at = line.indexOf(QLatin1String("candidate:"));
    if (at < 0)
        return line;
    QStringList f = line.mid(at).split(QLatin1Char(' '));
    // candidate:<fundação> <componente> <transporte> <prioridade> <ip> <porta> typ <tipo> ...
    if (f.size() < 8 || f[6] != QLatin1String("typ") || f[7] != QLatin1String("host"))
        return line;
    static const QRegularExpression ipv4Re(QStringLiteral("^\\d{1,3}(\\.\\d{1,3}){3}$"));
    if (!ipv4Re.match(f[4]).hasMatch())
        return line; // só IPv4 (mDNS/IPv6 ficam como estão)
    f[4] = ip;
    return line.left(at) + f.join(QLatin1Char(' '));
}

} // namespace

QString advertiseHostIp(const QString &text, const QString &ip)
{
    if (ip.isEmpty() || !text.contains(QLatin1String("candidate:")))
        return text;
    if (!text.contains(QLatin1Char('\n')))
        return rewriteCandidateLine(text, ip);
    QStringList lines = splitLines(text);
    for (QString &l : lines)
        if (l.startsWith(QLatin1String("a=candidate:")))
            l = rewriteCandidateLine(l, ip);
    return joinLines(lines);
}

QString candidateKey(const QString &candidate)
{
    const int at = candidate.indexOf(QLatin1String("candidate:"));
    const QStringList f = candidate.mid(std::max(0, at)).split(QLatin1Char(' '));
    if (f.size() < 8)
        return candidate;
    return QStringList{f[1], f[2].toLower(), f[4], f[5], f[7]}.join(QLatin1Char(' '));
}

} // namespace openspeak::sdp
