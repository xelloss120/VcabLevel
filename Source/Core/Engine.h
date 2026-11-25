// 全体の処理（2 章）:
//   入力 ─┬───────────────────────────── ゲートの判定
//         └→ コンプ → ゲイン → リミッター（先読み 1ms）→ ゲートの開け閉め → バイパスのクロスフェード → 出力
// 入力チャンネルの選択はプラグインの側（PluginProcessor::mixToMono。VcabDenoise と同じ形）で行い、ここには 1 チャンネルを渡す。
// JUCE には依存しない。遅延は 1ms（リミッターの先読み。リミッターをオフにしても変わらない）。
#pragma once

#include "Dsp/Compressor.h"
#include "Dsp/Gate.h"
#include "Dsp/Limiter.h"
#include "Params.h"

#include <array>
#include <atomic>
#include <vector>

namespace vcab
{

// 画面用の 10ms ごとの値（8 章）
struct MeterFrame
{
    float inDb = -120.0f;      // 入力の山（dBFS）
    float outDb = -120.0f;     // 出力の山（dBFS）
    float rmsDb = -120.0f;     // コンプが見ている大きさ（RMS、dBFS。区切りの終わりの値）
    float compDb = 0.0f;       // コンプの削り量（dB、0 以上。区切りの中の最大）
    float limiterDb = 0.0f;    // リミッターの削り量（dB、0 以上。区切りの中の最大）
    float gateDb = 0.0f;       // ゲートの倍率（dB、0 以下。区切りの中の最小）
};

// 書き手 1（音の処理）、読み手 1（画面）。ロックしない。あふれた分は捨てる
class MeterRing
{
public:
    static constexpr int kSize = 1024;

    void push (const MeterFrame& f)
    {
        const int w = writeIndex.load (std::memory_order_relaxed);
        const int next = (w + 1) % kSize;
        if (next == readIndex.load (std::memory_order_acquire))
            return;
        buffer[static_cast<size_t> (w)] = f;
        writeIndex.store (next, std::memory_order_release);
    }

    bool pop (MeterFrame& f)
    {
        const int r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return false;
        f = buffer[static_cast<size_t> (r)];
        readIndex.store ((r + 1) % kSize, std::memory_order_release);
        return true;
    }

private:
    std::array<MeterFrame, kSize> buffer {};
    std::atomic<int> writeIndex { 0 }, readIndex { 0 };
};

class Engine
{
public:
    void prepare (double fs, int maxBlock);
    void setSettings (const Settings& s);

    // data: 入力（1 チャンネル）を受け取り、出力に書き換える
    void process (float* data, int n);

    int latency() const { return limiter.latency(); }
    double sampleRate() const { return fs; }

    MeterRing meters;
    // 処理ツール用: 画面用の値を、捨てずにここにも溜める（nullptr なら溜めない）
    void setFrameLog (std::vector<MeterFrame>* log) { frameLog = log; }

private:
    double fs = 48000.0;
    int maxBlock = 0;
    Settings settings;

    Gate gate;
    Compressor comp;
    Limiter limiter;

    // バイパス用に、元の音を遅延の分だけ遅らせる輪
    std::vector<float> dryRing;
    int dryPos = 0;

    // 段のオン/オフとバイパスのつなぎ（10ms の直線。0 で素通し / 1 で効く）
    float gateMix = 1.0f, compMix = 1.0f, limiterMix = 1.0f, bypassMix = 0.0f, mixStep = 0.002f;
    // ゲイン（20ms でならす）
    float gainLin = 1.0f, gainCoef = 0.999f;

    // 画面用の区切り
    int frameLength = 480, frameFill = 0;
    MeterFrame frame;
    float frameInPeak = 0.0f, frameOutPeak = 0.0f, frameGateMin = 1.0f, frameLimiterMin = 1.0f, frameComp = 0.0f;
    std::vector<MeterFrame>* frameLog = nullptr;

    void processChunk (float* data, int n);
    void finishFrame (float rmsDb);
};

} // namespace vcab
