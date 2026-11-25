#include "Compressor.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

namespace
{
constexpr double kRmsSeconds = 0.010; // RMS の時定数（4 章）
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
    level = -120.0f;
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
}

float Compressor::next (float x)
{
    // RMS: 2 乗を 10ms の 1 次でならす
    meanSquare = rmsCoef * meanSquare + (1.0 - rmsCoef) * static_cast<double> (x) * x;
    level = static_cast<float> (10.0 * std::log10 (meanSquare + 1.0e-20));
    const float target = compReductionDb (settings, level);
    // 下げる量を、増えるときはアタック、減るときはリリースでならす
    const float c = target > reduction ? attackCoef : releaseCoef;
    reduction = target + (reduction - target) * c;
    return reduction;
}

} // namespace vcab
