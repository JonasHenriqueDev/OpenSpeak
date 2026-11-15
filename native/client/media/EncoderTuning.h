#pragma once
// Ajustes de qualidade de cada encoder H.264 (lógica pura, testável).
//
// Nível "Quality": mais capricho no chip de vídeo (NVENC/AMF/QSV são unidades
// próprias da GPU, separadas da que roda o jogo), sem B-frames nem lookahead,
// então a latência fica a mesma. Nível "Fast": os ajustes antigos, mais leves.
// O EncoderLoadGuard cai para o "Fast" sozinho se o encoder não der conta.
// O OpenH264 (software) é igual nos dois: caprichar nele custaria CPU.

#include <QString>

#include <algorithm>
#include <utility>
#include <vector>

namespace openspeak {

enum class EncoderTier { Quality, Fast };

struct EncoderOption {
    const char *key;
    const char *value;
};

struct EncoderTuning {
    std::vector<EncoderOption> options;
    /// Extras que um driver/GPU antigo pode recusar: o encoder tenta com todos,
    /// depois tirando do fim um por vez, antes de desistir do nível.
    std::vector<EncoderOption> optional;
};

/// Software (OpenH264): sem nível de qualidade e sem recuo.
inline bool encoderHasTiers(const QString &name) { return name != QLatin1String("libopenh264"); }

inline EncoderTuning encoderTuning(const QString &name, EncoderTier tier)
{
    const bool quality = tier == EncoderTier::Quality;
    if (name == QLatin1String("h264_nvenc")) {
        EncoderTuning t;
        t.options = {{"preset", quality ? "p5" : "p3"},
                     {"tune", quality ? "ll" : "ull"},
                     {"rc", "cbr"},
                     {"zerolatency", "1"},
                     {"delay", "0"},
                     {"forced-idr", "1"},
                     {"profile", "baseline"}};
        if (quality) // menos bloco em céu, fumaça e cena escura (GTX 10xx+ / RTX)
            t.optional = {{"spatial-aq", "1"}, {"temporal-aq", "1"}};
        return t;
    }
    if (name == QLatin1String("h264_amf")) {
        EncoderTuning t;
        t.options = {{"usage", "ultralowlatency"},
                     {"quality", quality ? "balanced" : "speed"},
                     {"rc", "cbr"},
                     {"profile", "constrained_baseline"}};
        if (quality)
            t.optional = {{"vbaq", "1"}};
        return t;
    }
    if (name == QLatin1String("h264_qsv")) {
        return {{{"preset", quality ? "medium" : "veryfast"},
                 {"async_depth", "1"},
                 {"low_delay_brc", "1"},
                 {"profile", "baseline"}},
                {}};
    }
    if (name == QLatin1String("libopenh264")) {
        return {{{"profile", "constrained_baseline"}, {"allow_skip_frames", "1"}, {"rc_mode", "bitrate"}}, {}};
    }
    return {};
}

/// Cai do nível "Quality" para o "Fast" quando o encoder passa a usar mais de
/// Limit do tempo de um quadro por WindowsBeforeFallback janelas de 1 s seguidas
/// (GPU antiga, ou o QSV dividindo a GPU integrada com o jogo).
class EncoderLoadGuard {
public:
    static constexpr double Limit = 0.7;
    static constexpr int WindowsBeforeFallback = 3;

    /// Média de uma janela de 1 s (segundos por quadro). true = cair agora.
    bool feed(double avgEncodeTime, int fps)
    {
        if (avgEncodeTime > Limit / std::max(1, fps))
            ++m_over;
        else
            m_over = 0;
        return m_over >= WindowsBeforeFallback;
    }
    void reset() { m_over = 0; }

private:
    int m_over = 0;
};

} // namespace openspeak
