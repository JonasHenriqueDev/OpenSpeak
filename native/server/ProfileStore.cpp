#include "ProfileStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSaveFile>

namespace openspeak {

namespace {

// Baixo de propósito: o login roda na thread da sinalização e a VM de produção tem 1/8 de vCPU.
constexpr int Iterations = 50'000;
constexpr int MaxFailures = 5;
constexpr qint64 LockMs = 60'000;

// PBKDF2-HMAC-SHA256 com um bloco só (saída de 32 bytes). O QPasswordDigestor exigiria
// o Qt Network neste núcleo, que só depende do Qt Core.
QByteArray pbkdf2(const QByteArray &password, const QByteArray &salt, int iterations)
{
    QMessageAuthenticationCode hmac(QCryptographicHash::Sha256, password);
    hmac.addData(salt);
    hmac.addData(QByteArray::fromHex("00000001"));
    QByteArray u = hmac.result();
    QByteArray out = u;
    for (int i = 1; i < iterations; ++i) {
        hmac.reset();
        hmac.addData(u);
        u = hmac.result();
        for (qsizetype b = 0; b < out.size(); ++b)
            out[b] = char(out[b] ^ u[b]);
    }
    return out;
}

QByteArray randomSalt()
{
    quint32 words[4];
    QRandomGenerator::system()->fillRange(words);
    return QByteArray(reinterpret_cast<const char *>(words), sizeof words);
}

} // namespace

ProfileStore::ProfileStore(std::function<qint64()> now)
    : m_now(now ? std::move(now) : std::function<qint64()>([] { return QDateTime::currentMSecsSinceEpoch(); }))
{
}

void ProfileStore::setFile(const QString &file)
{
    m_file = file;
    QFile in(file);
    if (file.isEmpty() || !in.open(QIODevice::ReadOnly))
        return;
    const QJsonObject profiles = QJsonDocument::fromJson(in.readAll()).object().value("profiles").toObject();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        const QJsonObject p = it.value().toObject();
        m_profiles.insert(it.key(), {p.value("nickname").toString(), p.value("password").toString(), p.value("displayName").toString(),
                                     p.value("color").toString(), p.value("description").toString(), p.value("avatar").toString()});
    }
}

QString ProfileStore::hashPassword(const QString &password, const QByteArray &salt, int iterations)
{
    return QStringLiteral("pbkdf2-sha256$%1$%2$%3")
        .arg(iterations)
        .arg(QString::fromLatin1(salt.toBase64()), QString::fromLatin1(pbkdf2(password.toUtf8(), salt, iterations).toBase64()));
}

bool ProfileStore::checkPassword(const QString &password, const QString &stored)
{
    const QStringList parts = stored.split(QLatin1Char('$'));
    bool ok = false;
    const int iterations = parts.value(1).toInt(&ok);
    if (parts.size() != 4 || parts[0] != QLatin1String("pbkdf2-sha256") || !ok || iterations <= 0)
        return false;
    const QByteArray expected = QByteArray::fromBase64(parts[3].toLatin1());
    const QByteArray actual = pbkdf2(password.toUtf8(), QByteArray::fromBase64(parts[2].toLatin1()), iterations);
    if (expected.size() != actual.size())
        return false;
    char diff = 0;
    for (qsizetype i = 0; i < actual.size(); ++i)
        diff |= char(expected[i] ^ actual[i]);
    return diff == 0;
}

const ProfileStore::Profile *ProfileStore::find(const QString &nickname) const
{
    const auto it = m_profiles.constFind(key(nickname));
    return it != m_profiles.cend() ? &it.value() : nullptr;
}

QString ProfileStore::claim(const QString &nickname, const QString &password)
{
    const QString k = key(nickname);
    Profile profile;
    profile.nickname = nickname;
    profile.password = hashPassword(password, randomSalt(), Iterations);
    m_profiles.insert(k, profile);
    save();
    return k;
}

void ProfileStore::setPassword(const QString &key, const QString &password)
{
    m_profiles[key].password = hashPassword(password, randomSalt(), Iterations);
    save();
}

void ProfileStore::update(const QString &key, const Profile &profile)
{
    m_profiles[key] = profile;
    save();
}

ProfileStore::Verify ProfileStore::verify(const QString &key, const QString &password)
{
    Failure failure = m_failures.value(key);
    if (failure.lockedUntil > m_now())
        return Verify::Locked;
    if (checkPassword(password, m_profiles.value(key).password)) {
        m_failures.remove(key);
        return Verify::Ok;
    }
    failure.lockedUntil = 0;
    if (++failure.count >= MaxFailures)
        failure = {0, m_now() + LockMs};
    m_failures.insert(key, failure);
    return Verify::Wrong;
}

void ProfileStore::save() const
{
    if (m_file.isEmpty())
        return;
    QJsonObject profiles;
    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        const Profile &p = it.value();
        profiles.insert(it.key(), QJsonObject{{"nickname", p.nickname}, {"password", p.password}, {"displayName", p.displayName},
                                              {"color", p.color}, {"description", p.description}, {"avatar", p.avatar}});
    }
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile out(m_file);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"profiles", profiles}}).toJson(QJsonDocument::Compact);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        qWarning() << "não foi possível gravar os perfis:" << out.errorString();
        return;
    }
    QFile::setPermissions(m_file, QFile::ReadOwner | QFile::WriteOwner);
}

} // namespace openspeak
