#include "Settings.h"
#include "Presets.h"
#include "Protocol.h"

#include <QRandomGenerator>

namespace openspeak {

Settings::Settings(const QString &profile, bool debug, QObject *parent)
    : QObject(parent), m_settings(QStringLiteral("OpenSpeak"), QStringLiteral("OpenSpeak")), m_debug(debug)
{
    m_group = profile.isEmpty() ? QStringLiteral("default") : profile;
    m_uid = value("uid").toString();
    if (m_uid.isEmpty()) {
        QByteArray bytes(15, Qt::Uninitialized);
        QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), 3);
        for (int i = 12; i < 15; ++i)
            bytes[i] = char(QRandomGenerator::global()->bounded(256));
        m_uid = QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        set("uid", m_uid);
    }
}

QVariant Settings::value(const QString &key, const QVariant &def) const
{
    return m_settings.value(m_group + QLatin1Char('/') + key, def);
}

void Settings::set(const QString &key, const QVariant &v)
{
    m_settings.setValue(m_group + QLatin1Char('/') + key, v);
    emit changed();
}

QVariantMap Settings::lastStreamingConfig() const
{
    QVariantMap cfg = value("lastStreamingConfig").toMap();
    const QualityPreset *p = findPreset(QLatin1String(DefaultPresetId));
    auto def = [&cfg](const char *k, const QVariant &v) {
        if (!cfg.contains(QLatin1String(k)))
            cfg.insert(QLatin1String(k), v);
    };
    def("type", int(StreamType::Screen));
    def("access", int(StreamAccess::Public));
    def("mode", int(StreamMode::P2P));
    def("bitrate", p->bitrate);
    def("viewerLimit", 0);
    def("audio", true);
    def("width", p->width);
    def("height", p->height);
    def("fps", p->fps);
    def("audioBitrate", p->audioBitrate);
    return cfg;
}

void Settings::toggleContact(const QString &uid)
{
    QStringList list = contacts();
    if (list.contains(uid))
        list.removeAll(uid);
    else
        list.append(uid);
    set("contacts", list);
}

void Settings::hideStream(const QString &id)
{
    QStringList list = hiddenStreams();
    if (!list.contains(id)) {
        list.append(id);
        setHiddenStreams(list);
    }
}

void Settings::showStream(const QString &id)
{
    QStringList list = hiddenStreams();
    if (list.removeAll(id))
        setHiddenStreams(list);
}

} // namespace openspeak
