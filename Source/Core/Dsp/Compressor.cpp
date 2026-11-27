#include "Compressor.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

namespace
{
constexpr double kRmsSeconds = 0.010; // RMS の時定数（4 章）
// 効き始めの線に取る余裕（パワーで 1e-4 = 約 0.0004dB。dB に直すときの丸めより十分大きい）
constexpr double kQuietMargin = 1.0 - 1.0e-4;
double meanSquareToDb (double ms) { return 10.0 * std::log10 (ms + 1.0e-20); }
float coef (double seconds, double fs) { return static_cast<float> (std::exp (-1.0 / (std::max (seconds, 1.0e-5) * fs))); }
} // namespace

void Compressor::prepare (double sampleRate)
{
    fs = sampleRate;
    update();
    reset();
}

void Compressor::reset()
{
    meanSquare = 0.0;
    reduction = 0.0f;
}

void Compressor::setSettings (const CompSettings& s)
{
    settings = s;
    update();
}

void Compressor::update()
{
    rmsCoef = coef (kRmsSeconds, fs);
    attackCoef = coef (settings.attackMs * 0.001, fs);
    releaseCoef = coef (settings.releaseMs * 0.001, fs);
    const float kneeStart = settings.threshold - 0.5f * std::max (0.0f, settings.knee); // ここ以下では下げない（4 章の式）
    quietMeanSquare = std::pow (10.0, kneeStart / 10.0) * kQuietMargin;
}

float Compressor::levelDb() const
{
    return static_cast<float> (meanSquareToDb (meanSquare));
}

float Compressor::next (float x)
{
    // RMS: 2 乗を 10ms の 1 次でならす
    meanSquare = rmsCoef * meanSquare + (1.0 - rmsCoef) * static_cast<double> (x) * x;
    const float target = meanSquare + 1.0e-20 < quietMeanSquare ? 0.0f : compReductionDb (settings, static_cast<float> (meanSquareToDb (meanSquare)));
    // 下げる量を、増えるときはアタック、減るときはリリースでならす
    const float c = target > reduction ? attackCoef : releaseCoef;
    reduction = target + (reduction - target) * c;
    return reduction;
}

} // namespace vcab
