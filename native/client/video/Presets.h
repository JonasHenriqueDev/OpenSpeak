#pragma once
// Presets de qualidade extraídos do TeamSpeak 6 (docs/ANALISE_TEAMSPEAK_P2P.md §3).

#include <QString>
#include <array>

namespace openspeak {

struct QualityPreset {
    const char *id;
    int bitrate;      // bps
    int width;        // -1 = resolução da fonte
    int height;
    int fps;
    int audioBitrate; // bps
};

inline constexpr std::array<QualityPreset, 7> QualityPresets{{
    {"360", 500'000, 640, 360, 30, 64'000},
    {"480", 1'000'000, 852, 480, 30, 96'000},
    {"720", 3'000'000, 1280, 720, 30, 128'000},
    {"1080", 5'000'000, 1920, 1080, 30, 192'000},
    {"1440", 6'500'000, 2560, 1440, 30, 256'000},
    {"source", 8'000'000, -1, -1, 30, 320'000},
    {"presentation", 2'000'000, -1, -1, 5, 320'000},
}};

inline constexpr const char *DefaultPresetId = "720";

inline const QualityPreset *findPreset(const QString &id)
{
    for (const auto &p : QualityPresets)
        if (id == QLatin1String(p.id))
            return &p;
    return nullptr;
}

} // namespace openspeak
