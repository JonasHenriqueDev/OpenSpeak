#include "UpdatePolicy.h"
#include <QRegularExpression>
#include <QUrl>
#include <QVersionNumber>
#include <QFile>
#include <QCryptographicHash>

namespace openspeak {
bool verifyInstallerDigest(const QString &file, const QString &digest) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&input) && QString::fromLatin1(hash.result().toHex()) == digest;
}
namespace {
QString cleanVersion(QString version) {
    if (version.startsWith('v')) version.remove(0, 1);
    return version.section('+', 0, 0);
}
}
QString normalizeEdition(const QString &edition) {
    return edition.trimmed().compare(QLatin1String("full"), Qt::CaseInsensitive) == 0 ? QStringLiteral("full") : QStringLiteral("lite");
}
QString installerPrefix(const QString &edition) {
    return normalizeEdition(edition) == QLatin1String("full") ? QStringLiteral("OpenSpeak-Full-Setup") : QStringLiteral("OpenSpeak-Setup");
}
bool validReleaseVersion(const QString &version) {
    static const QRegularExpression pattern(QStringLiteral("^v?[0-9]+\\.[0-9]+\\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?(?:\\+[0-9A-Za-z.-]+)?$"));
    return pattern.match(version).hasMatch();
}
int compareReleaseVersions(const QString &a, const QString &b) {
    const auto av = cleanVersion(a), bv = cleanVersion(b);
    const int base = QVersionNumber::compare(QVersionNumber::fromString(av), QVersionNumber::fromString(bv));
    if (base) return base;
    const auto ap = av.section('-', 1), bp = bv.section('-', 1);
    if (ap.isEmpty() || bp.isEmpty()) return ap.isEmpty() == bp.isEmpty() ? 0 : ap.isEmpty() ? 1 : -1;
    const auto aa = ap.split('.'), ba = bp.split('.');
    for (int i = 0; i < qMin(aa.size(), ba.size()); ++i) {
        if (aa[i] == ba[i]) continue;
        bool an = false, bn = false;
        const auto ai = aa[i].toULongLong(&an), bi = ba[i].toULongLong(&bn);
        if (an && bn) return ai < bi ? -1 : 1;
        if (an != bn) return an ? -1 : 1;
        return QString::compare(aa[i], ba[i], Qt::CaseSensitive);
    }
    return aa.size() == ba.size() ? 0 : aa.size() < ba.size() ? -1 : 1;
}
QJsonObject selectInstallerRelease(const QJsonArray &releases, bool allowPrereleases, const QString &edition) {
    const QString prefix = installerPrefix(edition);
    QJsonObject best;
    static const QRegularExpression digestPattern(QStringLiteral("^sha256:[0-9a-fA-F]{64}$"));
    for (const auto &value : releases) {
        const auto release = value.toObject();
        const auto tag = release.value("tag_name").toString();
        const bool pre = release.value("prerelease").toBool() || cleanVersion(tag).contains('-');
        if (release.value("draft").toBool() || (!allowPrereleases && pre) || !validReleaseVersion(tag)) continue;
        if (!best.isEmpty() && compareReleaseVersions(tag, best.value("version").toString()) <= 0) continue;
        QJsonObject chosen;
        const QString filename = QStringLiteral("%1-%2.exe").arg(prefix, tag.startsWith('v') ? tag.mid(1) : tag);
        for (const auto &assetValue : release.value("assets").toArray()) {
            const auto asset = assetValue.toObject();
            const auto name = asset.value("name").toString();
            const QUrl url(asset.value("browser_download_url").toString());
            if (name != filename && name != prefix + QLatin1String(".exe")) continue;
            if (asset.value("state").toString() != QLatin1String("uploaded") || !digestPattern.match(asset.value("digest").toString()).hasMatch()) continue;
            if (url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com") || !url.userInfo().isEmpty()
                || !url.path().startsWith(QLatin1String("/JonasHenriqueDev/OpenSpeak/releases/download/"))) continue;
            if (asset.value("size").toDouble() <= 0 || asset.value("size").toDouble() > 512 * 1024 * 1024) continue;
            if (chosen.isEmpty() || name == filename) chosen = asset;
        }
        if (!chosen.isEmpty()) best = {{"version", tag.startsWith('v') ? tag.mid(1) : tag}, {"prerelease", pre},
            {"url", chosen.value("browser_download_url")}, {"sha256", chosen.value("digest").toString().mid(7).toLower()},
            {"size", chosen.value("size")}, {"filename", filename}};
    }
    return best;
}
QString releaseTagFromUrl(const QUrl &url) {
    static const QRegularExpression tagPath(QStringLiteral("^/JonasHenriqueDev/OpenSpeak/releases/tag/(v[^/]+)$"));
    if (url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com")) return {};
    const auto match = tagPath.match(url.path());
    return match.hasMatch() && validReleaseVersion(match.captured(1)) ? match.captured(1) : QString();
}
QJsonObject releaseFromChecksums(const QString &tag, const QByteArray &sums, const QString &filename, qint64 size) {
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64}) [ *](\\S+)$"));
    for (const QByteArray &raw : sums.split('\n')) {
        const auto match = line.match(QString::fromUtf8(raw.trimmed()));
        if (!match.hasMatch() || match.captured(2) != filename) continue;
        // Mesma forma da API, para passar pelas mesmas regras de selectInstallerRelease.
        return {{"tag_name", tag}, {"prerelease", false}, {"draft", false}, {"assets", QJsonArray{QJsonObject{
            {"name", filename}, {"size", double(size)}, {"state", "uploaded"},
            {"digest", QStringLiteral("sha256:") + match.captured(1).toLower()},
            {"browser_download_url", QStringLiteral("https://github.com/JonasHenriqueDev/OpenSpeak/releases/download/%1/%2").arg(tag, filename)}}}}};
    }
    return {};
}
}
