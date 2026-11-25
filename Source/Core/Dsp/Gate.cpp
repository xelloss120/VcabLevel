#include "Gate.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

namespace
{
constexpr double kDetectReleaseSeconds = 0.001; // 判定のピークを下げる速さ（3 章）
float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }
} // namespace

void Gate::prepare (double sampleRate)
{
    fs = sampleRate;
    update();
    reset();
}

void Gate::reset()
{
    env = 0.0f;
    open = false;
    holdLeft = 0;
    g = 0.0f; // 閉じた状態から始める（起動した瞬間の雑音を出さない）
}

void Gate::setSettings (const GateSettings& s)
{
    settings = s;
    update();
}

void Gate::update()
{
    const auto& s = settings;
    envCoef = static_cast<float> (std::exp (-1.0 / (kDetectReleaseSeconds * fs)));
    openLin = dbToLin (s.threshold);
    closeLin = dbToLin (s.threshold - std::max (0.0f, s.hysteresis));
    holdSamples = static_cast<int> (std::lround (std::max (0.0f, s.holdMs) * 0.001 * fs));
    const double attackSamples = std::max (1.0, s.attackMs * 0.001 * fs);
    attackStep = static_cast<float> (1.0 / attackSamples);
    const double releaseSamples = std::max (1.0, s.releaseMs * 0.001 * fs);
    releaseMul = static_cast<float> (std::pow (10.0, -80.0 / 20.0 / releaseSamples)); // リリースの時間で -80dB まで
}

} // namespace vcab
