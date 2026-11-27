#include "Reverb.h"
#include "MathConstants.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

namespace
{
// ぼかしの長さ（ms）と倍率。前の 2 段で細かく、後ろの 2 段で粗くぼかす
constexpr double kDiffMs[4] = { 4.77, 3.60, 12.73, 9.31 };
constexpr float kDiffGain[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
// 遅延線の長さ（ms）。平均 約 51ms。標本の数は、この長さ以上で一番近い素数にする（重なって鳴る周波数を作らないように）
constexpr double kLineMs[8] = { 31.3, 37.9, 41.9, 47.3, 53.1, 59.9, 67.7, 73.1 };
constexpr double kMaxPredelayMs = 100.0;
constexpr double kPredelaySmoothSeconds = 0.05;

bool isPrime (int n)
{
    if (n < 2)
        return false;
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0)
            return false;
    return true;
}

int nextPrime (int n)
{
    while (! isPrime (n))
        ++n;
    return n;
}

// 8 点のアダマール変換（1/√8 をかけて、エネルギーを変えない）
void hadamard8 (float* v)
{
    for (int h = 1; h < 8; h *= 2)
        for (int i = 0; i < 8; i += h * 2)
            for (int j = i; j < i + h; ++j)
            {
                const float a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
    constexpr float k = 0.35355339f; // 1/√8
    for (int i = 0; i < 8; ++i)
        v[i] *= k;
}
} // namespace

void Reverb::prepare (double sampleRate)
{
    fs = sampleRate;
    preBuf.assign (static_cast<size_t> (std::ceil (kMaxPredelayMs * 0.001 * fs)) + 4, 0.0f);
    preCoef = static_cast<float> (std::exp (-1.0 / (kPredelaySmoothSeconds * fs)));
    for (int i = 0; i < kDiffusers; ++i)
    {
        diffBuf[static_cast<size_t> (i)].assign (static_cast<size_t> (std::max (1L, std::lround (kDiffMs[i] * 0.001 * fs))), 0.0f);
        diffGain[static_cast<size_t> (i)] = kDiffGain[i];
    }
    for (int i = 0; i < kLines; ++i)
        lineBuf[static_cast<size_t> (i)].assign (static_cast<size_t> (nextPrime (static_cast<int> (std::lround (kLineMs[i] * 0.001 * fs)))), 0.0f);
    lastTime = lastHigh = lastLow = -1.0f;
    setSettings (settings);
    reset();
}

void Reverb::reset()
{
    std::fill (preBuf.begin(), preBuf.end(), 0.0f);
    prePos = 0;
    preDelay = preTarget;
    for (auto& b : diffBuf)
        std::fill (b.begin(), b.end(), 0.0f);
    diffPos.fill (0);
    for (auto& b : lineBuf)
        std::fill (b.begin(), b.end(), 0.0f);
    linePos.fill (0);
    lowCut.z1 = lowCut.z2 = highCut.z1 = highCut.z2 = 0.0f;
}

void Reverb::setSettings (const ReverbSettings& s)
{
    settings = s;
    preTarget = static_cast<float> (std::clamp (static_cast<double> (s.predelayMs), 0.0, kMaxPredelayMs) * 0.001 * fs);
    if (s.timeS != lastTime)
        updateDecay();
    if (s.highCutHz != lastHigh || s.lowCutHz != lastLow)
        updateFilters();
}

void Reverb::updateDecay()
{
    lastTime = settings.timeS;
    const double t60 = std::max (0.05, static_cast<double> (settings.timeS));
    for (int i = 0; i < kLines; ++i)
    {
        const double m = static_cast<double> (lineBuf[static_cast<size_t> (i)].size());
        lineGain[static_cast<size_t> (i)] = static_cast<float> (std::pow (10.0, -3.0 * m / (t60 * fs)));
    }
}

void Reverb::updateFilters()
{
    lastHigh = settings.highCutHz;
    lastLow = settings.lowCutHz;
    // 2 次のバターワース（RBJ の式、Q = 1/√2）。標本化周波数の 0.45 倍より上には置かない
    const double q = 0.70710678;
    auto design = [this, q] (Biquad& f, double hz, bool high)
    {
        const double w = 2.0 * kPi * std::clamp (hz, 10.0, 0.45 * fs) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        const double b0 = high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0;
        const double b1 = high ? -(1.0 + c) : (1.0 - c);
        f.b0 = static_cast<float> (b0 / a0);
        f.b1 = static_cast<float> (b1 / a0);
        f.b2 = static_cast<float> (b0 / a0);
        f.a1 = static_cast<float> (-2.0 * c / a0);
        f.a2 = static_cast<float> ((1.0 - alpha) / a0);
    };
    design (lowCut, settings.lowCutHz, true);
    design (highCut, settings.highCutHz, false);
}

float Reverb::next (float x)
{
    // プリディレイ
    const int preLen = static_cast<int> (preBuf.size());
    preBuf[static_cast<size_t> (prePos)] = x;
    preDelay = preTarget + (preDelay - preTarget) * preCoef;
    float readAt = static_cast<float> (prePos) - preDelay;
    if (readAt < 0.0f)
        readAt += static_cast<float> (preLen);
    int i0 = static_cast<int> (readAt);
    // preDelay がごく小さい（プリディレイ 0 へならしている途中）と、prePos が 0 のとき readAt が丸めで preLen ちょうどになる。
    // そのままだと輪の外を読む（中身しだいでは NaN が響きに入り、鳴り続ける）ので、輪の頭に戻す
    if (i0 >= preLen)
        i0 -= preLen;
    const float frac = readAt - static_cast<float> (i0);
    const int i1 = i0 + 1 == preLen ? 0 : i0 + 1;
    // readAt は書いた位置から preDelay だけ前。i1 は i0 の次（新しい側）
    float v = preBuf[static_cast<size_t> (i0)] + (preBuf[static_cast<size_t> (i1)] - preBuf[static_cast<size_t> (i0)]) * frac;
    prePos = prePos + 1 == preLen ? 0 : prePos + 1;

    // ぼかし（全域通過）
    for (size_t d = 0; d < static_cast<size_t> (kDiffusers); ++d)
    {
        auto& buf = diffBuf[d];
        int& pos = diffPos[d];
        const float g = diffGain[d];
        const float z = buf[static_cast<size_t> (pos)];
        const float w = v + g * z;
        v = z - g * w;
        buf[static_cast<size_t> (pos)] = w;
        pos = pos + 1 == static_cast<int> (buf.size()) ? 0 : pos + 1;
    }

    // 遅延線: 出口の音に 1 周分の倍率をかけ、和を響きとして出し、混ぜ合わせて入口に戻す
    float s[kLines];
    float out = 0.0f;
    for (size_t i = 0; i < static_cast<size_t> (kLines); ++i)
    {
        s[i] = lineBuf[i][static_cast<size_t> (linePos[i])] * lineGain[i];
        out += (i & 1) ? -s[i] : s[i];
    }
    hadamard8 (s);
    for (size_t i = 0; i < static_cast<size_t> (kLines); ++i)
    {
        auto& buf = lineBuf[i];
        int& pos = linePos[i];
        buf[static_cast<size_t> (pos)] = s[i] + v; // 声は 8 本すべてに同じ大きさで入れる
        pos = pos + 1 == static_cast<int> (buf.size()) ? 0 : pos + 1;
    }

    // 和は 1/√8 で、最初の 1 周の響きのエネルギーが入った声と同じになる
    out *= 0.35355339f;
    return highCut.process (lowCut.process (out));
}

} // namespace vcab
