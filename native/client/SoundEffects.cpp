#include "SoundEffects.h"

#include "ConnectionHandler.h"
#include "ScreenshareManager.h"
#include "Settings.h"

#include <QAudio>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>

namespace openspeak {

namespace {
// Temas gerados por scripts/gen_sounds.py; o primeiro é o padrão.
const QStringList &themes()
{
    static const QStringList list{QStringLiteral("suave"), QStringLiteral("bolha"), QStringLiteral("cristal"),
                                  QStringLiteral("retro"), QStringLiteral("kalimba"), QStringLiteral("espacial"),
                                  QStringLiteral("discreto"), QStringLiteral("nostalgia")};
    return list;
}
const QStringList &events()
{
    static const QStringList list{QStringLiteral("entrar_sala"), QStringLiteral("sair_sala"),
                                  QStringLiteral("alguem_entrou"), QStringLiteral("alguem_saiu"),
                                  QStringLiteral("iniciar_transmissao"), QStringLiteral("pausar_transmissao"),
                                  QStringLiteral("espectador_entrou"), QStringLiteral("espectador_saiu")};
    return list;
}
} // namespace

SoundEffects::SoundEffects(ConnectionHandler *handler, ScreenshareManager *screenshare, Settings *settings,
                           QObject *parent)
    : QObject(parent), m_handler(handler), m_screenshare(screenshare), m_settings(settings)
{
    connect(handler, &ConnectionHandler::connectionChanged, this, &SoundEffects::scheduleRefresh);
    connect(handler, &ConnectionHandler::changed, this, &SoundEffects::scheduleRefresh);
    connect(handler, &ConnectionHandler::streamAdded, this, &SoundEffects::scheduleRefresh);
    connect(handler, &ConnectionHandler::streamUpdated, this, &SoundEffects::scheduleRefresh);
    connect(handler, &ConnectionHandler::streamRemoved, this, &SoundEffects::scheduleRefresh);
    if (screenshare)
        connect(screenshare, &ScreenshareManager::itemsChanged, this, &SoundEffects::scheduleRefresh);
    // Carrega o tema escolhido antes: o primeiro play() não espera a leitura do arquivo.
    preload();
    connect(settings, &Settings::changed, this, &SoundEffects::preload);
}

void SoundEffects::preload()
{
    for (const QString &event : events())
        effect(m_settings->soundTheme(), event);
}

void SoundEffects::preview(const QString &theme, const QString &event)
{
    if (auto *fx = effect(theme, event)) {
        fx->setVolume(QAudio::convertVolume(m_settings->soundVolume() / 100.0, QAudio::LogarithmicVolumeScale,
                                            QAudio::LinearVolumeScale));
        fx->play();
    }
}

// Vários sinais chegam juntos para a mesma notificação; compara uma vez só.
void SoundEffects::scheduleRefresh()
{
    if (m_pending)
        return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] { m_pending = false; refresh(); });
}

void SoundEffects::refresh()
{
    const bool connected = m_handler->isConnected();
    const int channel = connected ? m_handler->ownChannel() : 0;
    QSet<int> others;
    QHash<QString, bool> streams;
    if (channel != 0) {
        for (const ClientInfo &c : m_handler->clients())
            if (c.cid == channel && c.clid != m_handler->ownClid())
                others.insert(c.clid);
        for (const StreamInfo &s : m_handler->streams())
            if (s.cid == channel)
                streams.insert(s.id, s.paused);
    }
    // Espectadores das minhas transmissões (a contagem vem do servidor).
    QHash<QString, int> viewers;
    if (connected)
        for (const StreamInfo &s : m_handler->streams())
            if (s.ownerClid == m_handler->ownClid())
                viewers.insert(s.id, s.viewerCount);
    bool viewerJoined = false, viewerLeft = false;
    for (auto it = viewers.cbegin(); it != viewers.cend(); ++it) {
        const int before = m_viewers.value(it.key(), 0);
        viewerJoined |= it.value() > before;
        viewerLeft |= it.value() < before;
    }
    // Streams que estou assistindo: a contagem pública já me inclui, então entrar
    // toca "espectador_entrou" quando o servidor me conta; sair toca na hora.
    QHash<QString, int> watched;
    if (connected && m_screenshare)
        for (const QString &id : m_screenshare->watchedStreams()) {
            const auto s = m_handler->streams().constFind(id);
            if (s != m_handler->streams().cend())
                watched.insert(id, s->viewerCount);
        }
    for (auto it = watched.cbegin(); it != watched.cend(); ++it) {
        const auto before = m_watched.constFind(it.key());
        if (before == m_watched.cend())
            continue; // acabei de pedir para assistir: essa é a base
        viewerJoined |= it.value() > before.value();
        viewerLeft |= it.value() < before.value();
    }
    for (auto it = m_watched.cbegin(); it != m_watched.cend(); ++it)
        if (!watched.contains(it.key()) && m_handler->streams().contains(it.key()))
            viewerLeft = true; // saí do stream (que continua no ar)

    QString event;
    if (channel != m_channel) {
        // Ao trocar de canal, o que já estava lá não toca nada.
        if (channel != 0)
            event = QStringLiteral("entrar_sala");
        else if (m_channel != 0)
            event = QStringLiteral("sair_sala");
    } else if (channel != 0) {
        bool started = false, paused = false;
        for (auto it = streams.cbegin(); it != streams.cend(); ++it) {
            const auto old = m_streams.constFind(it.key());
            if (!it.value() && (old == m_streams.cend() || old.value()))
                started = true;
            else if (it.value() && old != m_streams.cend() && !old.value())
                paused = true;
        }
        for (auto it = m_streams.cbegin(); it != m_streams.cend(); ++it)
            if (!it.value() && !streams.contains(it.key()))
                paused = true; // encerrado

        // Pessoas antes de streams: quem entra já transmitindo toca só "entrou".
        if (!(others - m_others).isEmpty())
            event = QStringLiteral("alguem_entrou");
        else if (!(m_others - others).isEmpty())
            event = QStringLiteral("alguem_saiu");
        else if (viewerJoined)
            event = QStringLiteral("espectador_entrou");
        else if (viewerLeft)
            event = QStringLiteral("espectador_saiu");
        else if (started)
            event = QStringLiteral("iniciar_transmissao");
        else if (paused)
            event = QStringLiteral("pausar_transmissao");
    }

    m_channel = channel;
    m_others = others;
    m_streams = streams;
    m_viewers = viewers;
    m_watched = watched;
    if (!event.isEmpty())
        play(event);
}

void SoundEffects::play(const QString &event)
{
    if (m_settings->soundEffects())
        preview(m_settings->soundTheme(), event);
}

QSoundEffect *SoundEffects::effect(const QString &theme, const QString &event)
{
    const QString t = themes().contains(theme) ? theme : themes().first();
    if (!events().contains(event))
        return nullptr;
    const QString key = t + QLatin1Char('/') + event;
    QSoundEffect *&fx = m_effects[key];
    if (!fx) {
        fx = new QSoundEffect(this);
        fx->setSource(QUrl(QStringLiteral("qrc:/sounds/%1.wav").arg(key)));
    }
    return fx;
}

} // namespace openspeak
