#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace openspeak {
int compareReleaseVersions(const QString &a, const QString &b);
bool validReleaseVersion(const QString &version);
QJsonObject selectInstallerRelease(const QJsonArray &releases, bool allowPrereleases);
bool verifyInstallerDigest(const QString &file, const QString &sha256);
}
