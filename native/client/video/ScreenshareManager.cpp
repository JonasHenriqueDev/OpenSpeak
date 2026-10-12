#include "ScreenshareManager.h"
#include "Presets.h"

#include <QLoggingCategory>
#include <QUrl>

#include <algorithm>

Q_LOGGING_CATEGORY(lcShare, "openspeak.screenshare")

namespace openspeak {

namespace {
constexpr int ThumbnailIntervalMs = 10'000;
constexpr int HistoryLength = 60;

QVariantMap historyEntry(const StatsSummary &s, bool inbound)
{
    return {
        {"fractionLost", s.fractionLost},
        {"jitter", s.jitter},
        {"rtt", s.lastRoundTripTime / 2},
        {"retransmitBps", s.retransmit.bps},
        {"bps", inbound ? s.received.bps : s.sent.bps},
        {"codingTime", inbound ? s.video.avgDecodeTime : s.video.avgEncodeTime},
        {"fps", s.video.fps},
    };
}

void pushHistory(QList<QVariantMap> &list, QVariantMap entry)
{
    list.append(std::move(entry));
    while (list.size() > HistoryLength)
        list.removeFirst();
}

QVariantList toVariantList(const QList<QVariantMap> &list)
{
    QVariantList out;
    out.reserve(list.size());
    for (const QVariantMap &m : list)
        out.append(m);
    return out;
}
} // namespace

ScreenshareManager::ScreenshareManager(ConnectionHandler *handler, Settings *settings, QObject *parent)
    : QObject(parent), m_handler(handler), m_conn(handler->connection()), m_settings(settings)
{
    connect(m_conn, &ServerConnection::notification, this, &ScreenshareManager::onNotification);
    connect(m_handler, &ConnectionHandler::changed, this, &ScreenshareManager::notifyChanged);
    connect(m_handler, &ConnectionHandler::streamUpdated, this, &ScreenshareManager::notifyChanged);
    connect(m_settings, &Settings::changed, this, &ScreenshareManager::notifyChanged);
    connect(m_handler, &ConnectionHandler::streamRemoved, this, [this](const QString &id, int) {
        disposeOwn(id);
        auto it = m_inbound.find(id);
        if (it != m_inbound.end()) {
            disposeInbound(*it);
            m_inbound.erase(it);
        }
        notifyChanged();
    });
    connect(m_handler, &ConnectionHandler::disconnected, this, [this] {
        QStringList ids;
        for (const auto &[id, own] : m_own)
            ids.append(id);
        for (const QString &id : ids)
            disposeOwn(id);
        for (InboundView &v : m_inbound)
            disposeInbound(v);
        m_inbound.clear();
        notifyChanged();
    });
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &ScreenshareManager::collectStats);
    m_statsTimer.start();
    m_changeDebounce.setSingleShot(true);
    m_changeDebounce.setInterval(16);
    connect(&m_changeDebounce, &QTimer::timeout, this, &ScreenshareManager::itemsChanged);
}

ScreenshareManager::~ScreenshareManager()
{
    QStringList ids;
    for (const auto &[id, own] : m_own)
        ids.append(id);
    for (const QString &id : ids)
        disposeOwn(id);
    for (InboundView &v : m_inbound)
        disposeInbound(v);
}

void ScreenshareManager::notifyChanged()
{
    if (!m_changeDebounce.isActive())
        m_changeDebounce.start();
}

QString ScreenshareManager::errorMessage(const CommandResult &r) const
{
    return QStringLiteral("error.") + r.returnCode;
}

// ======================================================= configuração

ScreenshareManager::StreamConfig ScreenshareManager::configFromMap(const QVariantMap &m, const StreamConfig &b)
{
    StreamConfig c = b;
    if (m.contains("name") && !m.value("name").toString().trimmed().isEmpty())
        c.name = m.value("name").toString().trimmed().left(64);
    if (m.contains("access"))
        c.access = StreamAccess(m.value("access").toInt());
    if (m.contains("bitrate"))
        c.bitrate = std::clamp(m.value("bitrate").toInt(), 250'000, 20'000'000);
    if (m.contains("viewerLimit"))
        c.viewerLimit = std::clamp(m.value("viewerLimit").toInt(), 0, 1000);
    if (m.contains("audio"))
        c.audio = m.value("audio").toBool();
    if (m.contains("width"))
        c.width = m.value("width").toInt();
    if (m.contains("height"))
        c.height = m.value("height").toInt();
    if (m.contains("fps"))
        c.fps = std::clamp(m.value("fps").toInt(), 1, 60);
    if (m.contains("audioBitrate"))
        c.audioBitrate = std::clamp(m.value("audioBitrate").toInt(), 32'000, 510'000);
    return c;
}

QJsonObject ScreenshareManager::wireConfig(const StreamConfig &c)
{
    return {
        {"name", c.name}, {"type", int(c.type)}, {"access", int(c.access)}, {"mode", int(StreamMode::P2P)},
        {"bitrate", c.bitrate}, {"viewerLimit", c.viewerLimit}, {"audio", c.audio},
        {"quality", QJsonObject{{"width", c.width}, {"height", c.height}, {"fps", c.fps}}},
    };
}

// ===================================================== fontes / preview

QVariantList ScreenshareManager::sources(int type) const
{
    QVariantList out;
    for (const SourceDescriptor &d : VideoSource::availableSources(StreamType(type)))
        out.append(QVariantMap{{"type", int(d.type)}, {"id", d.id}, {"label", d.label},
                               {"window", QVariant::fromValue(d.window)}});
    return out;
}

bool ScreenshareManager::openPreview(const QVariantMap &source, int fps)
{
    closePreview();
    SourceDescriptor d;
    d.type = StreamType(source.value("type", int(StreamType::Screen)).toInt());
    d.id = source.value("id").toString();
    d.label = source.value("label").toString();
    d.testPattern = source.value("test").toBool();
    d.window = source.value("window").value<QCapturableWindow>();
    d.windowSpecified = d.type == StreamType::Window && source.contains("window");
    auto src = std::make_unique<VideoSource>();
    auto *candidate = src.get();
    connect(candidate, &VideoSource::errorOccurred, this, [this, candidate](const QString &error) {
        if (m_preview.get() != candidate) return;
        emit errorOccurred(error);
        QTimer::singleShot(0, this, [this, candidate] {
            if (m_preview.get() == candidate) closePreview();
        });
    });
    if (!src->open(d, fps)) {
        emit errorOccurred(QStringLiteral("video.error.") + src->errorString());
        return false;
    }
    m_preview = std::move(src);
    emit previewChanged();
    return true;
}

void ScreenshareManager::closePreview()
{
    if (!m_preview)
        return;
    m_preview->close();
    m_preview.reset();
    emit previewChanged();
}

void ScreenshareManager::attachPreview(QVideoSink *sink)
{
    if (m_preview)
        m_preview->addPreviewSink(sink);
}

void ScreenshareManager::detachPreview(QVideoSink *sink)
{
    if (m_preview)
        m_preview->removePreviewSink(sink);
}

// ===================================================== streams próprios

void ScreenshareManager::startStream(const QVariantMap &config)
{
    if (!m_handler->hasStreamingP2P()) {
        emit errorOccurred(QStringLiteral("error.streaming_not_supported_by_server"));
        return;
    }
    if (!m_preview) {
        emit errorOccurred(QStringLiteral("video.error.no_available_source"));
        return;
    }
    StreamConfig cfg = configFromMap(config, StreamConfig{});
    cfg.type = m_preview->type();
    if (cfg.name.isEmpty())
        cfg.name = QStringLiteral("Stream de %1").arg(m_handler->clientName(m_handler->ownClid()));
    // A captura passa a pertencer ao stream (guardada até o servidor responder).
    const int token = m_nextToken++;
    m_preview->clearPreviewSinks();
    m_pendingSources[token] = std::move(m_preview);
    emit previewChanged();

    QVariantMap persisted = config;
    persisted.remove("name");
    m_settings->setLastStreamingConfig(persisted);

    m_conn->sendCommand(QStringLiteral("setupstream"), wireConfig(cfg), [this, cfg, token](const CommandResult &r) {
        auto node = m_pendingSources.extract(token);
        if (node.empty())
            return;
        std::unique_ptr<VideoSource> source = std::move(node.mapped());
        if (!r.ok()) {
            source->close();
            emit errorOccurred(errorMessage(r));
            return;
        }
        const QString id = r.data.value("streamIdentifier").toString();
        auto own = std::make_unique<OwnStream>();
        own->id = id;
        own->config = cfg;
        own->source = std::move(source);
        OwnStream &ref = *own;
        m_own[id] = std::move(own);
        startMedia(ref);
        scheduleThumbnails(ref);
        qCInfo(lcShare) << "stream" << id << "ao vivo";
        notifyChanged();
    });
}

void ScreenshareManager::configureEncoder(OwnStream &own)
{
    VideoEncoderConfig ec;
    ec.maxWidth = own.config.width;
    ec.maxHeight = own.config.height;
    ec.fps = own.config.fps;
    ec.bitrate = own.config.bitrate;
    ec.screenContent = own.config.type != StreamType::Camera;
    ec.preferredEncoder = m_settings->preferredEncoder();
    own.encoder->setConfig(ec);
}

void ScreenshareManager::startMedia(OwnStream &own)
{
    OwnStream *p = &own;
    // Vídeo: UMA codificação, distribuída para todos os espectadores.
    own.encoder = std::make_unique<VideoEncoder>();
    VideoEncoderConfig ec;
    ec.maxWidth = own.config.width;
    ec.maxHeight = own.config.height;
    ec.fps = own.config.fps;
    ec.bitrate = own.config.bitrate;
    ec.screenContent = own.config.type != StreamType::Camera;
    ec.preferredEncoder = m_settings->preferredEncoder();
    own.encoder->start(ec, [p](const EncodedVideoFrame &frame) {
        std::lock_guard lock(p->sinkMutex);
        for (SessionOutbound *s : std::as_const(p->sinks))
            s->sendVideo(frame);
    });
    connect(own.source.get(), &VideoSource::frameAvailable, this, [p](const QVideoFrame &f) { p->encoder->pushFrame(f); });
    const QString id = own.id;
    connect(own.source.get(), &VideoSource::ended, this, [this, id] { stopStream(id); });

    // Áudio do stream (sistema / processo da janela / microfone).
    if (!own.config.audio)
        return;
    own.opus = std::make_unique<OpusEncoder>();
    const bool opened = own.opus->open(own.config.audioBitrate, [p](const QByteArray &packet, quint32 ts) {
        if (p->paused)
            return;
        std::lock_guard lock(p->sinkMutex);
        for (SessionOutbound *s : std::as_const(p->sinks))
            s->sendAudio(packet, ts);
    });
    if (!opened) {
        qCWarning(lcShare) << "Opus indisponível";
        own.opus.reset();
        return;
    }
    own.audio = std::make_unique<AudioCapture>();
    connect(own.audio.get(), &AudioCapture::samplesAvailable, this,
            [p](const QByteArray &pcm, int rate, int channels) {
                if (p->opus)
                    p->opus->push(pcm, rate, channels);
            },
            Qt::DirectConnection);
    connect(own.audio.get(), &AudioCapture::errorOccurred, this,
            [](const QString &e) { qCWarning(lcShare) << "captura de áudio:" << e; });
    startAudioFor(own);
}

void ScreenshareManager::startAudioFor(OwnStream &own)
{
    if (!own.audio)
        return;
    own.audio->stop();
    switch (own.config.type) {
    case StreamType::Camera:
        own.audio->start(AudioCapture::Mode::Microphone);
        break;
    case StreamType::Window: {
        const quint32 pid = AudioCapture::pidForWindowTitle(own.source->label());
        own.audio->start(pid ? AudioCapture::Mode::Process : AudioCapture::Mode::SystemExcludingSelf, pid);
        break;
    }
    default:
        own.audio->start(AudioCapture::Mode::SystemExcludingSelf);
    }
}

void ScreenshareManager::scheduleThumbnails(OwnStream &own)
{
    own.thumbTimer.stop();
    disconnect(&own.thumbTimer, nullptr, this, nullptr);
    if (own.config.access != StreamAccess::Public)
        return;
    const QString id = own.id;
    auto publish = [this, id] {
        auto it = m_own.find(id);
        if (it == m_own.end() || it->second->paused)
            return;
        const QString thumb = it->second->source->captureThumbnail();
        if (!thumb.isEmpty())
            m_conn->sendCommand(QStringLiteral("streamthumbnail"), {{"streamIdentifier", id}, {"thumbnail", thumb}});
    };
    connect(&own.thumbTimer, &QTimer::timeout, this, publish);
    own.thumbTimer.start(ThumbnailIntervalMs);
    QTimer::singleShot(1500, this, publish);
}

void ScreenshareManager::updateStream(const QString &id, const QVariantMap &config)
{
    auto it = m_own.find(id);
    if (it == m_own.end())
        return;
    OwnStream &own = *it->second;
    const StreamType type = own.config.type;
    own.config = configFromMap(config, own.config);
    own.config.type = type;
    configureEncoder(own);
    if (own.opus)
        own.opus->setBitrate(own.config.audioBitrate);
    QJsonObject wire = wireConfig(own.config);
    wire["streamIdentifier"] = id;
    m_conn->sendCommand(QStringLiteral("updatestream"), wire);
    scheduleThumbnails(own);
    // Uma mudança para "público" libera pedidos pendentes.
    const auto pending = own.joinRequests.values();
    for (const JoinReq &req : pending)
        maybeAutoAccept(own, req);
    QVariantMap persisted = config;
    persisted.remove("name");
    m_settings->setLastStreamingConfig(persisted);
    notifyChanged();
}

void ScreenshareManager::changeSource(const QString &id)
{
    auto it = m_own.find(id);
    if (it == m_own.end() || !m_preview)
        return;
    OwnStream &own = *it->second;
    OwnStream *p = &own;
    m_preview->clearPreviewSinks(); // picker sinks should not survive the dialog
    own.source->transferPreviewSinksTo(*m_preview); // local tile and fullscreen
    own.source->disconnect(this);
    own.source->close();
    own.source = std::move(m_preview);
    emit previewChanged();
    own.config.type = own.source->type();
    configureEncoder(own);
    own.encoder->requestKeyFrame();
    connect(own.source.get(), &VideoSource::frameAvailable, this, [p](const QVideoFrame &f) { p->encoder->pushFrame(f); });
    connect(own.source.get(), &VideoSource::ended, this, [this, id] { stopStream(id); });
    startAudioFor(own); // o áudio acompanha a nova fonte
    m_conn->sendCommand(QStringLiteral("updatestream"), {{"streamIdentifier", id}, {"type", int(own.config.type)}});
    notifyChanged();
}

void ScreenshareManager::stopStream(const QString &id)
{
    if (!m_own.count(id))
        return;
    disposeOwn(id);
    m_conn->sendCommand(QStringLiteral("stopstream"), {{"streamIdentifier", id}});
}

void ScreenshareManager::setPaused(const QString &id, bool paused)
{
    auto it = m_own.find(id);
    if (it == m_own.end())
        return;
    it->second->paused = paused;
    it->second->encoder->setPaused(paused);
    if (!paused)
        it->second->encoder->requestKeyFrame();
    m_conn->sendCommand(QStringLiteral("updatestream"), {{"streamIdentifier", id}, {"paused", paused}});
    notifyChanged();
}

QVariantMap ScreenshareManager::ownConfig(const QString &id) const
{
    auto it = m_own.find(id);
    if (it == m_own.end())
        return {};
    const StreamConfig &c = it->second->config;
    return {{"name", c.name}, {"type", int(c.type)}, {"access", int(c.access)}, {"mode", int(StreamMode::P2P)},
            {"bitrate", c.bitrate}, {"viewerLimit", c.viewerLimit}, {"audio", c.audio}, {"width", c.width},
            {"height", c.height}, {"fps", c.fps}, {"audioBitrate", c.audioBitrate},
            {"sourceLabel", it->second->source->label()}};
}

void ScreenshareManager::attachOwnPreview(const QString &id, QVideoSink *sink)
{
    auto it = m_own.find(id);
    if (it != m_own.end())
        it->second->source->addPreviewSink(sink);
}

void ScreenshareManager::detachOwnPreview(const QString &id, QVideoSink *sink)
{
    auto it = m_own.find(id);
    if (it != m_own.end())
        it->second->source->removePreviewSink(sink);
}

bool ScreenshareManager::viewerLimitReached(const OwnStream &own) const
{
    return own.config.viewerLimit > 0 && own.viewers.size() >= own.config.viewerLimit;
}

void ScreenshareManager::respondJoinRequest(const QString &id, int clid, bool accept, bool ban)
{
    auto it = m_own.find(id);
    if (it == m_own.end())
        return;
    OwnStream &own = *it->second;
    if (accept && viewerLimitReached(own)) {
        emit errorOccurred(QStringLiteral("video.join.request.viewer_limit_reached"));
        return;
    }
    own.joinRequests.remove(clid);
    emit joinRequestResolved(id, clid);
    notifyChanged();
    m_conn->sendCommand(QStringLiteral("respondjoinstreamrequest"),
                        {{"streamIdentifier", id}, {"clid", clid}, {"accept", accept}, {"ban", ban}},
                        [this](const CommandResult &r) {
                            if (!r.ok())
                                emit errorOccurred(errorMessage(r));
                        });
}

void ScreenshareManager::removeViewer(const QString &id, int clid, bool ban)
{
    m_conn->sendCommand(QStringLiteral("removeclientfromstream"),
                        {{"streamIdentifier", id}, {"clid", clid}, {"reason", int(ban ? LeaveReason::Banned : LeaveReason::Kicked)}});
}

void ScreenshareManager::unban(const QString &id, const QString &uid)
{
    m_conn->sendCommand(QStringLiteral("streamunban"), {{"streamIdentifier", id}, {"uid", uid}});
}

QVariantMap ScreenshareManager::viewers(const QString &id) const
{
    auto it = m_own.find(id);
    if (it == m_own.end())
        return {};
    const OwnStream &own = *it->second;
    QVariantList requests, viewers, banned;
    for (const JoinReq &r : own.joinRequests)
        requests.append(QVariantMap{{"clid", r.clid}, {"nickname", r.nickname}, {"identity", m_handler->clientIdentity(r.clid)}});
    for (int clid : own.viewers) {
        const SessionOutbound *s = own.sessions.value(clid);
        viewers.append(QVariantMap{{"clid", clid}, {"nickname", m_handler->clientName(clid)}, {"identity", m_handler->clientIdentity(clid)},
                                   {"state", s ? s->stateName() : QStringLiteral("connecting")},
                                   {"codec", QStringLiteral("H264")}});
    }
    for (const QString &uid : own.banned) {
        QString name = uid.left(24);
        QString identity;
        for (const ClientInfo &c : m_handler->clients())
            if (c.uid == uid) {
                name = c.nickname;
                identity = c.identity;
            }
        banned.append(QVariantMap{{"uid", uid}, {"nickname", name}, {"identity", identity}});
    }
    return {{"requests", requests}, {"viewers", viewers}, {"banned", banned},
            {"limit", own.config.viewerLimit}, {"limitReached", viewerLimitReached(own)}};
}

// ===================================================== streams assistidos

void ScreenshareManager::joinStream(const QString &id)
{
    const StreamInfo *info = m_handler->stream(id);
    if (!info)
        return;
    auto existing = m_inbound.find(id);
    if (existing != m_inbound.end()) {
        if (existing->status != InStatus::Error)
            return;
        disposeInbound(*existing);
        m_inbound.erase(existing);
    }
    m_settings->showStream(id);
    InboundView view;
    view.id = id;
    view.ownerClid = info->ownerClid;
    m_inbound.insert(id, view);
    notifyChanged();
    QJsonArray codecs;
    for (const QString &c : SessionInbound::localDecodeCodecs())
        codecs.append(c);
    m_conn->sendCommand(QStringLiteral("joinstreamrequest"), {{"streamIdentifier", id}, {"codecs", codecs}},
                        [this, id](const CommandResult &r) {
                            if (r.ok())
                                return;
                            auto it = m_inbound.find(id);
                            if (it == m_inbound.end())
                                return;
                            static const QHash<QString, QString> keys{
                                {"banned", "video.join.error.banned"},
                                {"viewer_limit", "video.join.error.viewer_limit"},
                                {"not_in_channel", "video.join.error.not_in_channel"},
                                {"stream_not_found", "video.join.error.stream_gone"}};
                            setInboundError(*it, keys.value(r.returnCode, QStringLiteral("video.join.error.generic")));
                        });
}

void ScreenshareManager::leaveStream(const QString &id)
{
    auto it = m_inbound.find(id);
    if (it == m_inbound.end())
        return;
    const bool wasActive = it->status != InStatus::Error;
    disposeInbound(*it);
    m_inbound.erase(it);
    notifyChanged();
    if (wasActive && m_handler->stream(id))
        m_conn->sendCommand(QStringLiteral("removeclientfromstream"), {{"streamIdentifier", id}});
}

void ScreenshareManager::dismiss(const QString &id)
{
    auto it = m_inbound.find(id);
    if (it != m_inbound.end() && it->status == InStatus::Error) {
        m_inbound.erase(it);
        notifyChanged();
    }
}

void ScreenshareManager::attachVideo(const QString &id, QVideoSink *sink)
{
    auto it = m_inbound.find(id);
    if (it != m_inbound.end() && it->session)
        it->session->addVideoSink(sink);
}

void ScreenshareManager::detachVideo(const QString &id, QVideoSink *sink)
{
    auto it = m_inbound.find(id);
    if (it != m_inbound.end() && it->session)
        it->session->removeVideoSink(sink);
}

void ScreenshareManager::setVolume(const QString &id, double volume)
{
    auto it = m_inbound.find(id);
    if (it == m_inbound.end())
        return;
    it->volume = volume;
    if (it->session)
        it->session->setVolume(volume);
}

// ============================================================ notificações

void ScreenshareManager::onNotification(const QString &cmd, const QJsonObject &a)
{
    if (cmd == QLatin1String("notifyjoinstreamrequest"))
        onJoinRequest(a);
    else if (cmd == QLatin1String("notifystreamclientjoined"))
        onClientJoined(a);
    else if (cmd == QLatin1String("notifystreamclientleft"))
        onClientLeft(a);
    else if (cmd == QLatin1String("notifyrespondjoinstreamrequest"))
        onJoinResponse(a);
    else if (cmd == QLatin1String("notifystreamsignaling"))
        onSignaling(a);
    else if (cmd == QLatin1String("notifystreamattendees")) {
        auto it = m_own.find(a.value("streamIdentifier").toString());
        if (it != m_own.end()) {
            it->second->viewers.clear();
            for (const QJsonValue &v : a.value("viewers").toArray())
                it->second->viewers.insert(v.toInt());
            notifyChanged();
        }
    } else if (cmd == QLatin1String("notifystreambanadded") || cmd == QLatin1String("notifystreambanremoved")) {
        auto it = m_own.find(a.value("streamIdentifier").toString());
        if (it != m_own.end()) {
            if (cmd.endsWith(QLatin1String("added")))
                it->second->banned.insert(a.value("uid").toString());
            else
                it->second->banned.remove(a.value("uid").toString());
            notifyChanged();
        }
    }
}

void ScreenshareManager::onJoinRequest(const QJsonObject &a)
{
    auto it = m_own.find(a.value("streamIdentifier").toString());
    if (it == m_own.end())
        return;
    JoinReq req;
    req.clid = a.value("clid").toInt();
    req.nickname = a.value("nickname").toString();
    req.uid = a.value("uid").toString();
    for (const QJsonValue &c : a.value("codecs").toArray())
        req.codecs.append(c.toString());
    it->second->joinRequests.insert(req.clid, req);
    if (!maybeAutoAccept(*it->second, req))
        emit joinRequest(it->first, req.clid, req.nickname);
    notifyChanged();
}

bool ScreenshareManager::maybeAutoAccept(OwnStream &own, const JoinReq &req)
{
    // Público aceita todos; Contatos aceita contatos; Privado pergunta.
    const bool autoAccept = own.config.access == StreamAccess::Public
        || (own.config.access == StreamAccess::ContactsOnly && m_settings->isContact(req.uid));
    if (!autoAccept || viewerLimitReached(own))
        return false;
    respondJoinRequest(own.id, req.clid, true);
    return true;
}

void ScreenshareManager::onClientJoined(const QJsonObject &a)
{
    const QString id = a.value("streamIdentifier").toString();
    auto it = m_own.find(id);
    if (it == m_own.end())
        return;
    OwnStream &own = *it->second;
    const int clid = a.value("clid").toInt();
    removeSession(own, clid);
    own.viewers.insert(clid);
    QStringList codecs;
    for (const QJsonValue &c : a.value("codecs").toArray())
        codecs.append(c.toString());
    VideoEncoder *encoder = own.encoder.get();
    auto *session = new SessionOutbound(
        id, clid, m_handler->iceServers(), codecs,
        [this, id, clid](const QJsonObject &payload) {
            m_conn->sendCommand(QStringLiteral("streamsignaling"), {{"streamIdentifier", id}, {"clid", clid}, {"payload", payload}});
        },
        [encoder] { encoder->requestKeyFrame(); }, this);
    if (!m_handler->meshMode() && QUrl(m_settings->serverUrl()).host().endsWith(QLatin1String(".ts.net")) && m_handler->directory())
        session->setBindAddress(m_handler->directory()->self().address.toString());
    if (!session->remoteSupportsH264()) {
        qCWarning(lcShare) << "espectador" << clid << "não decodifica H.264";
        delete session;
        m_conn->sendCommand(QStringLiteral("removeclientfromstream"),
                            {{"streamIdentifier", id}, {"clid", clid}, {"reason", int(LeaveReason::Failed)}});
        return;
    }
    own.sessions.insert(clid, session);
    {
        std::lock_guard lock(own.sinkMutex);
        own.sinks.append(session);
    }
    connect(session, &RtcSession::stateChanged, this, &ScreenshareManager::notifyChanged);
    connect(session, &RtcSession::failed, this, [this, id, clid] {
        m_conn->sendCommand(QStringLiteral("removeclientfromstream"),
                            {{"streamIdentifier", id}, {"clid", clid}, {"reason", int(LeaveReason::Failed)}});
    });
    qCInfo(lcShare) << "ResolveJoinRequest - adicionando espectador" << clid << "ao stream" << id;
    session->start();
    notifyChanged();
}

void ScreenshareManager::removeSession(OwnStream &own, int clid)
{
    SessionOutbound *session = own.sessions.take(clid);
    if (!session)
        return;
    {
        std::lock_guard lock(own.sinkMutex);
        own.sinks.removeAll(session);
    }
    session->close();
    session->deleteLater();
    own.history.remove(clid);
}

void ScreenshareManager::onClientLeft(const QJsonObject &a)
{
    const QString id = a.value("streamIdentifier").toString();
    const int clid = a.value("clid").toInt();
    auto it = m_own.find(id);
    if (it != m_own.end()) {
        OwnStream &own = *it->second;
        if (own.joinRequests.remove(clid))
            emit joinRequestResolved(id, clid);
        own.viewers.remove(clid);
        removeSession(own, clid);
        notifyChanged();
        return;
    }
    if (clid != m_handler->ownClid())
        return;
    auto view = m_inbound.find(id);
    if (view == m_inbound.end() || view->status == InStatus::Error)
        return;
    const auto reason = LeaveReason(a.value("reason").toInt());
    if (reason == LeaveReason::Kicked)
        setInboundError(*view, QStringLiteral("video.join.error.kicked"));
    else if (reason == LeaveReason::Banned)
        setInboundError(*view, QStringLiteral("video.join.error.banned"));
    else if (reason == LeaveReason::Failed)
        setInboundError(*view, QStringLiteral("video.join.error.connection_lost"));
    else if (!a.value("wasPending").toBool()) {
        disposeInbound(*view);
        m_inbound.erase(view);
        notifyChanged();
    }
}

void ScreenshareManager::onJoinResponse(const QJsonObject &a)
{
    const QString id = a.value("streamIdentifier").toString();
    auto it = m_inbound.find(id);
    if (it == m_inbound.end())
        return;
    if (!a.value("accepted").toBool()) {
        setInboundError(*it, LeaveReason(a.value("reason").toInt()) == LeaveReason::Banned
                                 ? QStringLiteral("video.join.error.banned")
                                 : QStringLiteral("video.join.error.denied"));
        return;
    }
    it->status = InStatus::Connecting;
    const int owner = it->ownerClid;
    auto *session = new SessionInbound(id, owner, m_handler->iceServers(), [this, id, owner](const QJsonObject &payload) {
        m_conn->sendCommand(QStringLiteral("streamsignaling"), {{"streamIdentifier", id}, {"clid", owner}, {"payload", payload}});
    }, this);
    if (!m_handler->meshMode() && QUrl(m_settings->serverUrl()).host().endsWith(QLatin1String(".ts.net")) && m_handler->directory())
        session->setBindAddress(m_handler->directory()->self().address.toString());
    session->setVolume(it->volume);
    it->session = session;
    connect(session, &RtcSession::stateChanged, this, [this, id](RtcSession::State s) {
        auto v = m_inbound.find(id);
        if (v == m_inbound.end())
            return;
        if (s == RtcSession::State::Connected)
            v->status = InStatus::Playing;
        else if (s == RtcSession::State::Reconnecting)
            v->status = InStatus::Reconnecting;
        notifyChanged();
    });
    connect(session, &SessionInbound::firstFrame, this, &ScreenshareManager::notifyChanged);
    connect(session, &RtcSession::failed, this, [this, id] {
        auto v = m_inbound.find(id);
        if (v == m_inbound.end())
            return;
        setInboundError(*v, QStringLiteral("video.join.error.connection_lost"));
        m_conn->sendCommand(QStringLiteral("removeclientfromstream"), {{"streamIdentifier", id}});
    });
    session->start();
    for (const QJsonObject &payload : m_earlySignals.take(id))
        session->onSignalingMessage(payload);
    notifyChanged();
}

void ScreenshareManager::onSignaling(const QJsonObject &a)
{
    const QString id = a.value("streamIdentifier").toString();
    const int clid = a.value("clid").toInt();
    const QJsonObject payload = a.value("payload").toObject();
    auto own = m_own.find(id);
    if (own != m_own.end()) {
        if (SessionOutbound *s = own->second->sessions.value(clid))
            s->onSignalingMessage(payload);
        return;
    }
    auto view = m_inbound.find(id);
    if (view != m_inbound.end() && view->session) {
        if (clid == view->ownerClid)
            view->session->onSignalingMessage(payload);
        return;
    }
    // A oferta chegou antes da resposta de entrada: guarda.
    m_earlySignals[id].append(payload);
}

void ScreenshareManager::setInboundError(InboundView &view, const QString &key)
{
    disposeInbound(view);
    view.status = InStatus::Error;
    view.errorKey = key;
    notifyChanged();
}

void ScreenshareManager::disposeOwn(const QString &id)
{
    auto it = m_own.find(id);
    if (it == m_own.end())
        return;
    OwnStream &own = *it->second;
    own.thumbTimer.stop();
    // Para as threads de mídia antes de destruir as sessões.
    if (own.audio)
        own.audio->stop();
    own.encoder->stop();
    own.source->disconnect(this);
    own.source->close();
    for (int clid : own.sessions.keys())
        removeSession(own, clid);
    for (int clid : own.joinRequests.keys())
        emit joinRequestResolved(id, clid);
    m_own.erase(it);
    notifyChanged();
}

void ScreenshareManager::disposeInbound(InboundView &view)
{
    if (view.session) {
        view.session->close();
        view.session->deleteLater();
        view.session = nullptr;
    }
    view.history = {};
}

// ============================================================ estatísticas

void ScreenshareManager::collectStats()
{
    for (auto &[id, ownPtr] : m_own) {
        OwnStream &own = *ownPtr;
        const VideoEncoderStats enc = own.encoder->stats();
        const TargetQuality target{own.config.width > 0 ? own.config.width : enc.width,
                                   own.config.height > 0 ? own.config.height : enc.height, own.config.fps};
        int bitrate = own.config.bitrate;
        bool any = false;
        for (auto sIt = own.sessions.begin(); sIt != own.sessions.end(); ++sIt) {
            SessionOutbound *s = sIt.value();
            s->collectStats(enc, target, own.config.bitrate);
            if (s->state() == RtcSession::State::Connected) {
                // Single-encoding: o encoder acompanha o espectador mais limitado.
                bitrate = std::min(bitrate, s->estimatedBitrate());
                any = true;
            }
            History &h = own.history[sIt.key()];
            pushHistory(h.video, historyEntry(s->videoStats(), false));
            pushHistory(h.audio, historyEntry(s->audioStats(), false));
        }
        own.encoder->setTargetBitrate(any ? bitrate : own.config.bitrate);
        if (!own.sessions.isEmpty())
            emit statsChanged(id);
    }
    for (InboundView &view : m_inbound) {
        if (!view.session)
            continue;
        const StreamInfo *info = m_handler->stream(view.id);
        TargetQuality target;
        if (info)
            target = {info->quality.width, info->quality.height, info->quality.fps};
        view.session->collectStats(info ? &target : nullptr);
        pushHistory(view.history.video, historyEntry(view.session->videoStats(), true));
        pushHistory(view.history.audio, historyEntry(view.session->audioStats(), true));
        emit statsChanged(view.id);
    }
    notifyChanged(); // severidade dos tiles
}

QVariantMap ScreenshareManager::stats(const QString &id, int viewerClid) const
{
    auto own = m_own.find(id);
    if (own != m_own.end()) {
        const OwnStream &o = *own->second;
        QVariantList viewers;
        for (auto it = o.sessions.begin(); it != o.sessions.end(); ++it)
            viewers.append(QVariantMap{{"clid", it.key()}, {"nickname", m_handler->clientName(it.key())}});
        if (!o.sessions.contains(viewerClid))
            viewerClid = o.sessions.isEmpty() ? 0 : o.sessions.firstKey();
        QVariantMap out{{"own", true}, {"viewers", viewers}, {"viewerClid", viewerClid},
                        {"target", QVariantMap{{"width", o.config.width}, {"height", o.config.height}, {"fps", o.config.fps}}}};
        if (const SessionOutbound *s = o.sessions.value(viewerClid)) {
            out["video"] = s->videoStats().toVariant();
            out["audio"] = s->audioStats().toVariant();
            const History h = o.history.value(viewerClid);
            out["history"] = QVariantMap{{"video", toVariantList(h.video)}, {"audio", toVariantList(h.audio)}};
        }
        return out;
    }
    auto view = m_inbound.find(id);
    if (view == m_inbound.end() || !view->session)
        return {{"own", false}};
    const StreamInfo *info = m_handler->stream(id);
    QVariantMap out{{"own", false}};
    if (info)
        out["target"] = QVariantMap{{"width", info->quality.width}, {"height", info->quality.height}, {"fps", info->quality.fps}};
    out["video"] = view->session->videoStats().toVariant();
    out["audio"] = view->session->audioStats().toVariant();
    out["history"] = QVariantMap{{"video", toVariantList(view->history.video)}, {"audio", toVariantList(view->history.audio)}};
    return out;
}

// ================================================================ itens

int ScreenshareManager::pendingRequests() const
{
    int n = 0;
    for (const auto &[id, own] : m_own)
        n += int(own->joinRequests.size());
    return n;
}

QVariantList ScreenshareManager::items() const
{
    QVariantList out;
    const int cid = m_handler->ownChannel();
    if (!cid)
        return out;
    const QStringList hidden = m_settings->hiddenStreams();
    const bool showOwn = m_settings->showOwnStreams();
    const bool showInactive = m_settings->showInactiveStreams();
    QVariantList own, remote;
    for (const StreamInfo &info : m_handler->streamsInChannel(cid)) {
        QVariantMap item = streamToVariant(info);
        item["ownerName"] = m_handler->clientName(info.ownerClid);
        auto o = m_own.find(info.id);
        if (o != m_own.end()) {
            if (!showOwn)
                continue;
            const OwnStream &s = *o->second;
            int severity = SeverityOk;
            for (const SessionOutbound *sess : s.sessions)
                severity = std::max({severity, sess->videoStats().worstSeverity(), sess->audioStats().worstSeverity()});
            item["kind"] = QStringLiteral("own");
            item["status"] = QStringLiteral("live");
            item["paused"] = s.paused;
            item["viewerCount"] = int(s.viewers.size());
            item["pendingRequests"] = int(s.joinRequests.size());
            item["severity"] = severity;
            own.append(item);
            continue;
        }
        auto v = m_inbound.find(info.id);
        const bool active = v != m_inbound.end();
        if (!active && (hidden.contains(info.id) || !showInactive))
            continue;
        item["kind"] = QStringLiteral("remote");
        QString status;
        int severity = SeverityOk;
        if (active) {
            switch (v->status) {
            case InStatus::Waiting: status = QStringLiteral("waiting"); break;
            case InStatus::Connecting: status = QStringLiteral("connecting"); break;
            case InStatus::Playing: status = QStringLiteral("playing"); break;
            case InStatus::Reconnecting: status = QStringLiteral("reconnecting"); break;
            case InStatus::Error: status = QStringLiteral("error"); break;
            }
            item["errorKey"] = v->errorKey;
            item["volume"] = v->volume;
            item["hasVideo"] = v->session && v->session->hasVideo();
            if (v->session)
                severity = std::max(v->session->videoStats().worstSeverity(), v->session->audioStats().worstSeverity());
        }
        item["status"] = status;
        item["severity"] = severity;
        remote.append(item);
    }
    out.append(own);
    out.append(remote);
    return out;
}

} // namespace openspeak
