#include "Limiter.h"

#include "MathConstants.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

namespace
{
constexpr double kLookaheadSeconds = 0.001; // 先読み 1ms（6 章）
}

void Limiter::prepare (double sampleRate)
{
    fs = sampleRate;
    lookahead = std::max (kDetectDelay + 2, static_cast<int> (std::lround (kLookaheadSeconds * fs)));
    window = lookahead - kDetectDelay + 1;

    int size = 1;
    while (size < lookahead + kTaps + 4)
        size <<= 1;
    ring.assign (static_cast<size_t> (size), 0.0f);
    ringMask = size - 1;

    dqIndex.assign (static_cast<size_t> (window + 1), 0);
    dqValue.assign (static_cast<size_t> (window + 1), 1.0f);
    box.assign (static_cast<size_t> (window), 1.0f);

    // 点と点の間の補間: 窓をかけた sinc（Kaiser、β = 9）。区間 (k, k+1) の中の位置 f の値を、y[k−15] 〜 y[k+16] から求める
    const int half = kTaps / 2;
    auto besselI0 = [] (double v)
    {
        double sum = 1.0, term = 1.0;
        for (int m = 1; m < 40; ++m)
        {
            term *= (v / (2.0 * m)) * (v / (2.0 * m));
            sum += term;
        }
        return sum;
    };
    constexpr double kBeta = 9.0;
    for (int ph = 0; ph < kPhases; ++ph)
    {
        const double f = (ph + 1) / 4.0;
        double sum = 0.0;
        for (int j = 0; j < kTaps; ++j)
        {
            const double t = (j - (half - 1)) - f; // 標本 k + (j − 15) から見た距離
            const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double u = t / (half + 0.5);
            const double w = std::abs (u) >= 1.0 ? 0.0 : besselI0 (kBeta * std::sqrt (1.0 - u * u)) / besselI0 (kBeta);
            coeffs[ph][j] = static_cast<float> (sinc * w);
            sum += sinc * w;
        }
        for (int j = 0; j < kTaps; ++j)
            coeffs[ph][j] = static_cast<float> (coeffs[ph][j] / sum); // 直流をそのまま通す
    }

    setSettings (settings);
    reset();
}

void Limiter::setSettings (const LimiterSettings& s)
{
    settings = s;
    ceilingLin = std::pow (10.0f, s.ceiling / 20.0f);
    releaseCoef = static_cast<float> (std::exp (-1.0 / (std::max (1.0f, s.releaseMs) * 0.001 * fs)));
}

void Limiter::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    pos = 0;
    prevInterval = 0.0f;
    dqHead = 0;
    dqSize = 0;
    held = 1.0f;
    std::fill (box.begin(), box.end(), 1.0f);
    boxPos = 0;
    boxSum = static_cast<double> (window);
}

float Limiter::next (float x, float& gain)
{
    ring[static_cast<size_t> (pos & ringMask)] = x;

    // 1. k = n − D の山（標本そのものと、両側の区間の補間）
    const int64_t k = pos - kDetectDelay;
    float interval = 0.0f; // 区間 (k, k+1) の山
    for (int ph = 0; ph < kPhases; ++ph)
    {
        float v = 0.0f;
        for (int j = 0; j < kTaps; ++j)
            v += coeffs[ph][j] * sampleAt (k - (kTaps / 2 - 1) + j);
        interval = std::max (interval, std::abs (v));
    }
    const float peak = std::max ({ std::abs (sampleAt (k)), prevInterval, interval });
    prevInterval = interval;

    // 2. 必要な倍率
    const float r = peak > ceilingLin ? ceilingLin / peak : 1.0f;

    // 3. 直近 W 個の最小値
    const int cap = window + 1;
    while (dqSize > 0 && dqValue[static_cast<size_t> ((dqHead + dqSize - 1) % cap)] >= r)
        --dqSize;
    const auto tail = static_cast<size_t> ((dqHead + dqSize) % cap);
    dqIndex[tail] = k;
    dqValue[tail] = r;
    ++dqSize;
    while (dqIndex[static_cast<size_t> (dqHead)] <= k - window)
    {
        dqHead = (dqHead + 1) % cap;
        --dqSize;
    }
    const float h = dqValue[static_cast<size_t> (dqHead)];
    // 下げるのは即座、戻すのはリリースでならす（h 以下に保つので、収まる保証は崩れない）
    held = h < held ? h : h + (held - h) * releaseCoef;

    // 4. 移動平均
    boxSum += static_cast<double> (held) - box[static_cast<size_t> (boxPos)];
    box[static_cast<size_t> (boxPos)] = held;
    boxPos = boxPos + 1 == window ? 0 : boxPos + 1;
    gain = static_cast<float> (boxSum / window);
    if (gain > 1.0f)
        gain = 1.0f;

    const float out = sampleAt (pos - lookahead);
    ++pos;
    return out;
}

} // namespace vcab
