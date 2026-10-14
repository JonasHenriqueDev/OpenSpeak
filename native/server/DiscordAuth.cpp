#include "DiscordAuth.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcDiscord, "openspeak.discord")

namespace openspeak {

DiscordAuth::DiscordAuth(QString clientId, QString clientSecret, QObject *parent)
    : QObject(parent), m_clientId(std::move(clientId)), m_clientSecret(std::move(clientSecret))
{
    m_network.setTransferTimeout(10'000);
}

void DiscordAuth::exchange(const QString &code, Callback done)
{
    if (!configured())
        return done(503, {{"error", "discord_not_configured"}});
    if (code.isEmpty() || code.size() > 256)
        return done(400, {{"error", "invalid_code"}});

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    QNetworkRequest request(QUrl(QStringLiteral("https://discord.com/api/oauth2/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_network.post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = body.value("access_token").toString();
        if (reply->error() != QNetworkReply::NoError || token.isEmpty()) {
            qCWarning(lcDiscord) << "troca do código OAuth falhou:" << reply->errorString() << body.value("error").toString();
            return done(502, {{"error", "token_exchange_failed"}});
        }
        // Só o token: o resto (refresh token, escopos) não interessa à atividade.
        done(200, {{"access_token", token}});
    });
}

} // namespace openspeak
