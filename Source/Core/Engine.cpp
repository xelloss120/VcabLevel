#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

namespace
{
constexpr double kMixSeconds = 0.010;   // オン/オフとバイパスの切り替え（VcabDenoise と同じ 10ms）
constexpr double kGainSeconds = 0.020;  // ゲインのつまみのならし（5 章）
constexpr double kFrameSeconds = 0.010; // 画面用の区切り
float toDb (float lin, float floorDb = -120.0f) { return lin > 0.0f ? std::max (floorDb, 20.0f * std::log10 (lin)) : floorDb; }
float target (bool on) { return on ? 1.0f : 0.0f; }
float ramp (float v, float t, float step) { return v + std::clamp (t - v, -step, step); }
} // namespace

void Engine::prepare (double sampleRate, int block)
{
    fs = sampleRate;
    maxBlock = std::max (block, 16);
    gate.prepare (fs);
    comp.prepare (fs);
    limiter.prepare (fs);
    dryRing.assign (static_cast<size_t> (limiter.latency() + 1), 0.0f);
    dryPos = 0;
    mixStep = static_cast<float> (1.0 / (kMixSeconds * fs));
    gainCoef = static_cast<float> (std::exp (-1.0 / (kGainSeconds * fs)));
    frameLength = std::max (1, static_cast<int> (std::lround (kFrameSeconds * fs)));
    frameFill = 0;
    frameInPeak = frameOutPeak = 0.0f;
    frameGateMin = frameLimiterMin = 1.0f;
    frameComp = 0.0f;
    setSettings (settings);
    gateMix = target (settings.gate.on);
    compMix = target (settings.comp.on);
    limiterMix = target (settings.limiter.on);
    bypassMix = target (settings.bypass);
    gainLin = settings.gain.on ? std::pow (10.0f, settings.gain.db / 20.0f) : 1.0f;
}

void Engine::setSettings (const Settings& s)
{
    settings = s;
    gate.setSettings (s.gate);
    comp.setSettings (s.comp);
    limiter.setSettings (s.limiter);
}

void Engine::process (float* data, int n)
{
    // 準備した大きさより大きな塊は区切って処理する（ほかの 3 つと同じ形）
    for (int done = 0; done < n;)
    {
        const int len = std::min (n - done, maxBlock);
        processChunk (data + done, len);
        done += len;
    }
}

void Engine::processChunk (float* data, int n)
{
    const float gateTarget = target (settings.gate.on), compTarget = target (settings.comp.on);
    const float limiterTarget = target (settings.limiter.on), bypassTarget = target (settings.bypass);
    const float gainTarget = settings.gain.on ? std::pow (10.0f, settings.gain.db / 20.0f) : 1.0f;
    const float ceilingLin = std::pow (10.0f, settings.limiter.ceiling / 20.0f);
    const int dryLen = static_cast<int> (dryRing.size());

    for (int i = 0; i < n; ++i)
    {
        const float x = data[i];
        gateMix = ramp (gateMix, gateTarget, mixStep);
        compMix = ramp (compMix, compTarget, mixStep);
        limiterMix = ramp (limiterMix, limiterTarget, mixStep);
        bypassMix = ramp (bypassMix, bypassTarget, mixStep);
        gainLin = gainTarget + (gainLin - gainTarget) * gainCoef;

        // 元の音（バイパス用）を遅延の分だけ遅らせる
        dryRing[static_cast<size_t> (dryPos)] = x;
        dryPos = dryPos + 1 == dryLen ? 0 : dryPos + 1;
        const float dry = dryRing[static_cast<size_t> (dryPos)]; // いちばん古い = L 標本前

        // ゲートの判定（入口の音。かけるのは最後。判定が 1ms 先の音なので、ゲートも先読みになる）
        const float gateGain = 1.0f + (gate.next (x) - 1.0f) * gateMix;

        // コンプ → ゲイン
        const float compDb = comp.next (x) * compMix;
        float y = x * std::pow (10.0f, -compDb / 20.0f) * gainLin;

        // リミッター（遅らせた音と、それにかける倍率）
        float limiterGain = 1.0f;
        y = limiter.next (y, limiterGain);
        limiterGain = 1.0f + (limiterGain - 1.0f) * limiterMix;
        y *= limiterGain;
        if (settings.limiter.on) // 念のための頭打ち（ふつうは効かない）
            y = std::clamp (y, -ceilingLin, ceilingLin);

        // ゲートの開け閉め（最後）
        y *= gateGain;

        // バイパス
        const float out = y + (dry - y) * bypassMix;
        data[i] = out;

        // 画面用
        frameInPeak = std::max (frameInPeak, std::abs (x));
        frameOutPeak = std::max (frameOutPeak, std::abs (out));
        frameComp = std::max (frameComp, compDb);
        frameLimiterMin = std::min (frameLimiterMin, limiterGain);
        frameGateMin = std::min (frameGateMin, gateGain);
        if (++frameFill >= frameLength)
            finishFrame (comp.levelDb());
    }
}

void Engine::finishFrame (float rmsDb)
{
    MeterFrame f;
    f.inDb = toDb (frameInPeak);
    f.outDb = toDb (frameOutPeak);
    f.rmsDb = std::max (-120.0f, rmsDb);
    f.compDb = frameComp;
    f.limiterDb = -toDb (frameLimiterMin);
    f.gateDb = toDb (frameGateMin);
    meters.push (f);
    if (frameLog != nullptr)
        frameLog->push_back (f);
    frameFill = 0;
    frameInPeak = frameOutPeak = 0.0f;
    frameGateMin = frameLimiterMin = 1.0f;
    frameComp = 0.0f;
}

} // namespace vcab
