#pragma once
// Escolha da camada do simulcast para um espectador (lógica pura, testável).
//
// Preferência do espectador: "auto", "high" ou "low". No automático:
//  - desce para a baixa quando a banda estimada fica abaixo de DownRatio do
//    bitrate do stream por DownAfter seguidos;
//  - volta para a alta depois de UpAfter com a estimativa acima de UpRatio,
//    respeitando uma espera que dobra a cada tentativa frustrada (subiu e caiu
//    de novo em pouco tempo), para não ficar alternando.
//
// Camada baixa sob demanda (STREAM_SFU_LAYER_PAUSE): quando ninguém precisa dela,
// o transmissor para de codificá-la (LowLayerControl). Para a troca continuar
// fluida, o espectador "aquece" a baixa antes de precisar: abaixo de WarmRatio
// ela já é religada, e a descida só acontece em DownRatio, quando o keyframe
// dela normalmente já chegou. Até lá o Forwarder segue mandando a alta.

#include <QString>

#include <algorithm>
#include <chrono>
#include <optional>

namespace openspeak::sfu {

enum class LayerPreference { Auto, High, Low };

inline LayerPreference layerPreferenceFromString(const QString &s)
{
    if (s == QLatin1String("high"))
        return LayerPreference::High;
    if (s == QLatin1String("low"))
        return LayerPreference::Low;
    return LayerPreference::Auto;
}

inline QString layerPreferenceName(LayerPreference p)
{
    switch (p) {
    case LayerPreference::High: return QStringLiteral("high");
    case LayerPreference::Low: return QStringLiteral("low");
    case LayerPreference::Auto: break;
    }
    return QStringLiteral("auto");
}

class LayerPolicy {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr double DownRatio = 0.6;
    static constexpr double UpRatio = 0.85;
    static constexpr auto DownAfter = std::chrono::seconds(2);
    static constexpr auto UpAfter = std::chrono::seconds(5);
    static constexpr auto InitialBackoff = std::chrono::seconds(10);
    static constexpr auto MaxBackoff = std::chrono::seconds(120);
    /// Caiu de novo até este tempo depois de subir: a subida foi frustrada.
    static constexpr auto FailedUpWindow = std::chrono::seconds(30);
    /// Abaixo disso a camada baixa é religada (se estiver pausada), antes de descer.
    static constexpr double WarmRatio = 0.8;

    LayerPreference preference = LayerPreference::Auto;

    /// Este espectador precisa da camada baixa agora ou pode precisar logo?
    bool wantsLow() const
    {
        if (preference == LayerPreference::High)
            return false;
        return preference == LayerPreference::Low || m_layer == 1 || m_warm;
    }

    /// Camada (0 = alta, 1 = baixa) para a estimativa atual do espectador.
    int step(int estimate, int configured, Clock::time_point now)
    {
        if (preference == LayerPreference::High)
            return 0;
        if (preference == LayerPreference::Low)
            return 1;
        if (m_layer == 0) {
            m_aboveSince.reset();
            // Aquecimento com histerese: liga abaixo de WarmRatio, desliga só acima de UpRatio.
            if (estimate < WarmRatio * configured)
                m_warm = true;
            else if (estimate >= UpRatio * configured)
                m_warm = false;
            if (estimate >= DownRatio * configured) {
                m_belowSince.reset();
                return 0;
            }
            if (!m_belowSince)
                m_belowSince = now;
            if (now - *m_belowSince < DownAfter)
                return 0;
            m_belowSince.reset();
            m_layer = 1;
            m_warm = false;
            const bool failedUp = m_lastUp && now - *m_lastUp < FailedUpWindow;
            m_backoff = failedUp ? std::min<Clock::duration>(m_backoff * 2, MaxBackoff) : Clock::duration(InitialBackoff);
            m_upAllowedAt = now + m_backoff;
            return 1;
        }
        m_belowSince.reset();
        if (estimate < UpRatio * configured || now < m_upAllowedAt) {
            m_aboveSince.reset();
            return 1;
        }
        if (!m_aboveSince)
            m_aboveSince = now;
        if (now - *m_aboveSince < UpAfter)
            return 1;
        m_aboveSince.reset();
        m_layer = 0;
        m_lastUp = now;
        return 0;
    }

    int autoLayer() const { return m_layer; }

private:
    int m_layer = 0;
    bool m_warm = false;
    std::optional<Clock::time_point> m_belowSince, m_aboveSince, m_lastUp;
    Clock::time_point m_upAllowedAt{};
    Clock::duration m_backoff = InitialBackoff;
};

/// Pausa e religa a camada baixa de um stream (lógica pura, testável). O relay chama
/// step() a cada ajuste e manda ao transmissor o que ela devolver.
class LowLayerControl {
public:
    using Clock = std::chrono::steady_clock;
    enum class Action { None, Pause, Resume };

    /// Sem ninguém precisando da baixa por este tempo, ela é pausada. Longo o
    /// bastante para uma oscilação de rede não ficar ligando e desligando.
    static constexpr auto DefaultIdleBeforePause = std::chrono::seconds(30);
    /// Pediu para religar e ela não voltou: pede de novo (mensagem perdida,
    /// transmissor reconectando).
    static constexpr auto ResumeRetry = std::chrono::seconds(3);
    /// Pediu a pausa e o transmissor não confirmou ({type:'lowlayer', paused}):
    /// é uma versão antiga, que ignora o pedido. Para de controlar este stream.
    static constexpr auto AckTimeout = std::chrono::seconds(5);

    Clock::duration idleBeforePause = DefaultIdleBeforePause;

    /// `lowAlive`: a baixa está chegando. `wanted`: algum espectador precisa dela.
    Action step(bool lowAlive, bool wanted, Clock::time_point now)
    {
        if (lowAlive && !m_seen) {
            m_seen = true; // transmissor com simulcast: só agora vale controlar
            m_neededAt = now;
        }
        if (!m_seen || m_unsupported)
            return Action::None;
        if (m_paused && !m_acked && now - m_pauseSentAt >= AckTimeout) {
            m_unsupported = true; // segue mandando as duas, como antes
            m_paused = false;
            m_managed = false;
            return Action::None;
        }
        if (wanted) {
            m_neededAt = now;
            const bool retry = m_managed && !m_paused && !lowAlive && now - m_resumeSentAt >= ResumeRetry;
            if (!m_paused && !retry)
                return Action::None;
            m_paused = false;
            m_resumeSentAt = now;
            return Action::Resume;
        }
        if (m_paused || !lowAlive || now - m_neededAt < idleBeforePause)
            return Action::None;
        m_paused = true;
        m_managed = true;
        m_acked = false;
        m_pauseSentAt = now;
        return Action::Pause;
    }

    /// O transmissor confirmou a pausa; `bonus`: passou o bitrate dela para a alta.
    void onAck(bool paused, bool bonus)
    {
        if (!paused || !m_paused)
            return;
        m_acked = true;
        m_bonus = bonus;
    }

    /// Com a baixa pausada (ou religando) a pedido, o stream continua em simulcast:
    /// os espectadores mantêm a escolha de qualidade.
    bool simulcast(bool highAlive, bool lowAlive) const { return highAlive && (lowAlive || (m_seen && m_managed)); }
    bool paused() const { return m_paused; }
    /// O bitrate da baixa está na alta agora (pausa confirmada, encoder com troca dinâmica).
    bool bonusActive() const { return m_paused && m_acked && m_bonus; }

private:
    bool m_seen = false;        // a baixa já chegou neste stream
    bool m_managed = false;     // já pedimos uma pausa (o transmissor segue este controle)
    bool m_paused = false;
    bool m_acked = false;       // o transmissor confirmou a pausa atual
    bool m_bonus = false;       // e aplicou o bônus na alta
    bool m_unsupported = false; // transmissor antigo: não controla mais
    Clock::time_point m_neededAt{}, m_resumeSentAt{}, m_pauseSentAt{};
};

} // namespace openspeak::sfu
