#pragma once
// Perfis: um apelido vira perfil ao ganhar uma senha. Os dados ficam num JSON no
// servidor, no mesmo formato de server/profileStore.js.

#include <QDateTime>
#include <QHash>
#include <QString>

#include <functional>

namespace openspeak {

class ProfileStore {
public:
    struct Profile {
        QString nickname; // como foi reivindicado
        QString password; // pbkdf2-sha256$iterações$sal$hash
        QString displayName;
        QString color;
        QString description;
        QString avatar; // data URL
        QString banner; // data URL; vai só por profilemedia
        QString avatarAnimated; // data URL de um GIF; avatar guarda a capa estática
        QString mediaTag; // muda quando a mídia pesada muda
    };
    enum class Verify { Ok, Wrong, Locked };

    explicit ProfileStore(std::function<qint64()> now = {});

    /// Arquivo JSON dos perfis; vazio = só na memória. Carrega o que já existir.
    void setFile(const QString &file);
    /// Relê o arquivo se ele mudou por fora (o monitor gera senhas novas nele).
    void refresh();

    static QString key(const QString &nickname) { return nickname.trimmed().toLower(); }
    static QString hashPassword(const QString &password, const QByteArray &salt, int iterations);
    static bool checkPassword(const QString &password, const QString &stored);

    const Profile *find(const QString &nickname) const;
    /// Devolve a chave do novo perfil.
    QString claim(const QString &nickname, const QString &password);
    void setPassword(const QString &key, const QString &password);
    void update(const QString &key, const Profile &profile);
    /// Tentativas erradas demais bloqueiam o perfil por um tempo (segura quem tenta adivinhar).
    Verify verify(const QString &key, const QString &password);

private:
    struct Failure {
        int count = 0;
        qint64 lockedUntil = 0;
    };
    void load();
    void save();

    std::function<qint64()> m_now;
    QString m_file;
    QDateTime m_fileTime;
    qint64 m_fileSize = -1;
    QHash<QString, Profile> m_profiles;
    QHash<QString, Failure> m_failures;
};

} // namespace openspeak
