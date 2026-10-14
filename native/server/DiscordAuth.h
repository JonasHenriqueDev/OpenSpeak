#pragma once
// Login da atividade do Discord: troca o código OAuth2 (AUTHORIZE do Embedded
// App SDK) por um access token. O client secret fica só no servidor
// (DISCORD_CLIENT_ID / DISCORD_CLIENT_SECRET); a atividade usa o token no
// AUTHENTICATE do SDK para saber o nome do usuário.

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <functional>

namespace openspeak {

class DiscordAuth : public QObject {
    Q_OBJECT
public:
    /// status HTTP para a atividade + corpo JSON ({access_token} ou {error}).
    using Callback = std::function<void(int status, const QJsonObject &body)>;

    DiscordAuth(QString clientId, QString clientSecret, QObject *parent = nullptr);

    bool configured() const { return !m_clientId.isEmpty() && !m_clientSecret.isEmpty(); }
    void exchange(const QString &code, Callback done);

private:
    QString m_clientId, m_clientSecret;
    QNetworkAccessManager m_network;
};

} // namespace openspeak
