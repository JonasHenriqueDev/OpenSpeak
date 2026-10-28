#pragma once
// Efeitos sonoros de canal e transmissão: entrar/sair do canal, alguém entrar/sair,
// streams iniciados/pausados no canal atual e espectadores entrando/saindo das
// minhas transmissões e das que estou assistindo (inclusive eu mesmo). Temas em
// scripts/gen_sounds.py ("suave" é o padrão).
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
    /// Prévia de um tema nas Preferências: a cada chamada um evento sorteado (nunca o mesmo
    /// da vez anterior), para dar para ouvir os vários sons do tema.
    Q_INVOKABLE void previewRandom(const QString &theme);

private:
    void preload();
    void scheduleRefresh();
    void refresh();
    void play(const QString &event);
    QSoundEffect *effect(const QString &theme, const QString &event);

    ConnectionHandler *m_handler;
    ScreenshareManager *m_screenshare;
    Settings *m_settings;
    QHash<QString, QSoundEffect *> m_effects;
    bool m_pending = false;
    QSoundEffect *m_lastPreview = nullptr; // última prévia sorteada (parada na próxima)
    QString m_lastPreviewEvent;
    int m_channel = 0;
    QSet<int> m_others;              // clientes no meu canal, exceto eu
    QHash<QString, bool> m_streams;  // streams do meu canal -> pausado
    QHash<QString, int> m_viewers;   // minhas transmissões -> espectadores
    QHash<QString, int> m_watched;   // streams que assisto -> espectadores (eu incluso)
};

} // namespace openspeak
