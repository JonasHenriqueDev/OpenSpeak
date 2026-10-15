#pragma once
// Efeitos sonoros de canal e transmissão: entrar/sair do canal, alguém entrar/sair,
// streams iniciados/pausados no canal atual e espectadores entrando/saindo das
// minhas transmissões e das que estou assistindo (inclusive eu mesmo). Dois temas: "suave" (padrão) e "bolha".
// O estado é comparado a cada mudança do ConnectionHandler, então funciona igual
// no servidor, na tailnet e na sala web.

#include <QHash>
#include <QObject>
#include <QSet>

class QSoundEffect;

namespace openspeak {

class ConnectionHandler;
class ScreenshareManager;
class Settings;

class SoundEffects : public QObject {
    Q_OBJECT
public:
    SoundEffects(ConnectionHandler *handler, ScreenshareManager *screenshare, Settings *settings,
                 QObject *parent = nullptr);

    /// Toca um evento ("entrar_sala", "alguem_entrou"...) no tema indicado; usado pela prévia nas Preferências.
    Q_INVOKABLE void preview(const QString &theme, const QString &event);

private:
    void scheduleRefresh();
    void refresh();
    void play(const QString &event);
    QSoundEffect *effect(const QString &theme, const QString &event);

    ConnectionHandler *m_handler;
    ScreenshareManager *m_screenshare;
    Settings *m_settings;
    QHash<QString, QSoundEffect *> m_effects;
    bool m_pending = false;
    int m_channel = 0;
    QSet<int> m_others;              // clientes no meu canal, exceto eu
    QHash<QString, bool> m_streams;  // streams do meu canal -> pausado
    QHash<QString, int> m_viewers;   // minhas transmissões -> espectadores
    QHash<QString, int> m_watched;   // streams que assisto -> espectadores (eu incluso)
};

} // namespace openspeak
