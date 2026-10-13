#pragma once
// Relay de mídia do modo SFU visto pelo VirtualServer.
//
// O servidor participa da sinalização como um par com o clid reservado 0
// (RelayClid, em Protocol.h): o transmissor oferta ao relay, e o relay oferta a cada
// espectador. O VirtualServer só valida permissões e repassa; a mídia fica
// toda dentro da implementação (SfuRelay), que os testes trocam por um falso.

#include "Protocol.h"

#include <QJsonObject>
#include <QString>

#include <functional>

namespace openspeak {

class StreamRelay {
public:
    /// Sinalização do relay para um cliente (dono ou espectador do stream).
    using SignalFn = std::function<void(const QString &streamId, int clid, const QJsonObject &payload)>;
    /// A sessão com `clid` falhou de vez. Se `clid` for o dono, o stream acabou.
    using FailFn = std::function<void(const QString &streamId, int clid)>;

    virtual ~StreamRelay() = default;

    /// Chamado pelo VirtualServer ao receber o relay. Os callbacks são sempre
    /// invocados na thread do Qt e nunca de dentro das chamadas abaixo.
    virtual void setCallbacks(SignalFn signal, FailFn failed) = 0;

    virtual void openStream(const QString &streamId, int ownerClid, int bitrate) = 0;
    virtual void updateStream(const QString &streamId, int bitrate) = 0;
    virtual void closeStream(const QString &streamId) = 0;
    virtual void addViewer(const QString &streamId, int clid) = 0;
    virtual void removeViewer(const QString &streamId, int clid) = 0;
    /// `streamsignaling` com clid 0 vindo de `fromClid` (já validado).
    virtual void signaling(const QString &streamId, int fromClid, const QJsonObject &payload) = 0;
};

} // namespace openspeak
