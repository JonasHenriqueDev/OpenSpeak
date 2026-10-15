#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace openspeak {
int compareReleaseVersions(const QString &a, const QString &b);
bool validReleaseVersion(const QString &version);
/// "lite" (padrão: cliente do servidor) ou "full" (com Tailscale, sala web e servidor dedicado).
QString normalizeEdition(const QString &edition);
QString installerPrefix(const QString &edition);
QJsonObject selectInstallerRelease(const QJsonArray &releases, bool allowPrereleases, const QString &edition = QStringLiteral("lite"));
bool verifyInstallerDigest(const QString &file, const QString &sha256);
/// Rota sem a API (limite de 60 consultas/h por IP): a tag para onde
/// github.com/.../releases/latest redireciona ("v1.2.3"), ou vazio.
QString releaseTagFromUrl(const QUrl &url);
/// Monta, no formato da API, uma release estável com o instalador `filename`
/// e o hash do SHA256SUMS-<versão>.txt publicado nela. Vazio se não constar.
QJsonObject releaseFromChecksums(const QString &tag, const QByteArray &sums, const QString &filename, qint64 size);
}
