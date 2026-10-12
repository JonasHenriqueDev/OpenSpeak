#include "SdpFilter.h"

#include <QMap>
#include <QRegularExpression>
#include <QStringList>

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

} // namespace openspeak::sdp
