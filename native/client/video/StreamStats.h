#pragma once
// Resumo de estatísticas por sessão + detecção de problemas.
// Os limiares de analyzeIssues reproduzem o cliente TS 6 (análise §4) e
// public/js/video/StatsCollector.js.

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace openspeak {

enum Severity : int { SeverityOk = 0, SeverityMinor = 1, SeverityMajor = 2 };

struct StreamIssue {
    QString category; // network | video
    QString name;     // packetloss, jitter, rtt, packetdelay, retransmit, qualitylimit, encodetime, fps
    QString value;
    QString expected;
    int severity = SeverityOk;
};

struct TargetQuality {
    int width = 0;
    int height = 0;
    int fps = 0;
};

struct StatsSummary {
    QString kind = QStringLiteral("video");
    QString codec;
    qint64 packetsLost = 0;
    double fractionLost = 0;      // 0..1
    double jitter = 0;            // s
    double lastRoundTripTime = 0; // s
    double avgRoundTripTime = 0;  // s
    struct { qint64 packets = 0, bytes = 0; double bps = 0, currentDelay = 0, totalDelay = 0; } sent;
    struct { qint64 packets = 0, bytes = 0; double bps = 0; } received;
    struct { qint64 packets = 0, bytes = 0; double bps = 0; } retransmit;
    double targetBitrate = 0;
    QString srtpCipher;
    QString connectionType;
    struct {
        int width = 0, height = 0, fps = 0;
        bool hasEncode = false;
        QString encoder;
        double avgEncodeTime = 0;
        QString limitReason = QStringLiteral("none");
        bool hasDecode = false;
        QString decoder;
        double avgDecodeTime = 0;
    } video;
    int issueStatus = SeverityOk; // OR de severidades
    QList<StreamIssue> issues;
    QHash<QString, int> issueSeverity; // "categoria:nome" -> severidade

    int worstSeverity() const { return issueStatus & SeverityMajor ? SeverityMajor : issueStatus ? SeverityMinor : SeverityOk; }
    QVariantMap toVariant() const;
};

QString formatBitrate(double bps, int digits = 1);
QString formatBytes(double bytes, int digits = 1);

/// Mesmos limiares do TS. `checkEncoder` liga as verificações do lado de saída.
void analyzeIssues(StatsSummary &s, const TargetQuality *target, bool checkEncoder);

} // namespace openspeak
