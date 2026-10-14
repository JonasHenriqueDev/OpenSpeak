#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace openspeak {
int compareReleaseVersions(const QString &a, const QString &b);
bool validReleaseVersion(const QString &version);
/// "lite" (padrão: cliente do servidor) ou "full" (com Tailscale, sala web e servidor dedicado).
QString normalizeEdition(const QString &edition);
QString installerPrefix(const QString &edition);
QJsonObject selectInstallerRelease(const QJsonArray &releases, bool allowPrereleases, const QString &edition = QStringLiteral("lite"));
bool verifyInstallerDigest(const QString &file, const QString &sha256);
}
