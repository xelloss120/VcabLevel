// リバーブ（12 章）。声を受け取り、響きだけ（量をかける前）を返す。元の声に足すのは Engine。
//   入る声 → プリディレイ → ぼかし（直列の全域通過 4 段）→ 8 本の遅延線を混ぜ合わせる響き（FDN、アダマール行列）
//          → 8 本の和 → ローカット・ハイカット（2 次）→ 出力
// 長さ（T60）は、遅延線ごとに、1 周でかける倍率 10^(-3·M / (T60·fs)) で決める（どの高さの音も同じ速さで消える）。
// 響きを揺らす処理（モジュレーション）は入れない（声に揺れが付くため。12 章）。
// JUCE には依存しない。遅延はない（響きは元の声と同時に始まり、プリディレイの分だけ遅れて立ち上がる）。
#pragma once

#include "../Params.h"

#include <array>
#include <vector>

namespace vcab
{

class Reverb
{
public:
    void prepare (double fs);
    void setSettings (const ReverbSettings& s);
    void reset();

    float next (float x);

private:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    double fs = 48000.0;
    ReverbSettings settings;

    // プリディレイ（動かしたときにプツッといわないよう、遅れの長さを 50ms でならして、間を直線で補間して読む）
    std::vector<float> preBuf;
    int prePos = 0;
    float preDelay = 0.0f, preTarget = 0.0f, preCoef = 0.999f;

    // ぼかし
    std::array<std::vector<float>, kDiffusers> diffBuf;
    std::array<int, kDiffusers> diffPos {};
    std::array<float, kDiffusers> diffGain {};

    // 遅延線
    std::array<std::vector<float>, kLines> lineBuf;
    std::array<int, kLines> linePos {};
    std::array<float, kLines> lineGain {};

    Biquad lowCut, highCut;
    float lastTime = -1.0f, lastHigh = -1.0f, lastLow = -1.0f;

    void updateDecay();
    void updateFilters();
};

} // namespace vcab
