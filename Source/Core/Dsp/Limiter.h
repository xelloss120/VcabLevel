// リミッター（6 章）。先読み 1ms。点と点の間の山（4 倍に補間して探す）も含めて、上限を超えないように下げる。
//
// 作り（標本の番号 n は、いま入った音）:
//   1. 点と点の間の山: k = n − D の標本のまわりを 4 倍に補間して山 p[k] を求める（補間に k + 16 まで使うので D = 16）。
//   2. 必要な倍率 r[k] = min(1, 上限 / p[k])。
//   3. 直近 W 個の r の最小値 h を取り、戻りだけをリリースでならす（下げるのは即座）。
//   4. それを W 個の移動平均でならした値 g を、L = D + W − 1 だけ遅らせた音にかける。
//      g[n] は r[n − L] 以下になる（移動平均の W 個のどれもが、r[n − L] を含む窓の最小値なので）。
//      なので、山は必ず上限に収まり、その手前の W 個（約 1ms）で直線的に下がる。
#pragma once

#include "../Params.h"

#include <cstdint>
#include <vector>

namespace vcab
{

class Limiter
{
public:
    static constexpr int kDetectDelay = 16;  // D: 補間に使う先の標本の数（= kTaps / 2）
    static constexpr int kTaps = 32;         // 補間のタップ数（1 つの位相あたり。48kHz のとき、20kHz まで誤差 0.01dB 以下）
    static constexpr int kPhases = 3;        // 点と点の間の 3 か所（1/4、2/4、3/4）

    // 準備（音の処理のスレッドの外で呼ぶ）。遅延は 1ms
    void prepare (double fs);
    void setSettings (const LimiterSettings& s);
    void reset();
    int latency() const { return lookahead; }

    // ゲイン後の音を 1 標本入れ、L だけ遅れた音を返す。gain に、その音にかける倍率（0〜1）を書く
    float next (float x, float& gain);

private:
    double fs = 48000.0;
    LimiterSettings settings;
    float ceilingLin = 0.7f;
    float releaseCoef = 0.99f;

    int lookahead = 48; // L
    int window = 41;    // W

    // 音の輪（遅延と補間の両方に使う）
    std::vector<float> ring;
    int ringMask = 0;
    int64_t pos = 0;

    float coeffs[kPhases][kTaps] {};
    float prevInterval = 0.0f; // 1 つ前の区間（k − 1 と k の間）の山

    // 直近 W 個の最小値（単調な列。番号と値）
    std::vector<int64_t> dqIndex;
    std::vector<float> dqValue;
    int dqHead = 0, dqSize = 0;

    float held = 1.0f; // 3 の値（戻りをならしたもの）

    // 移動平均
    std::vector<float> box;
    int boxPos = 0;
    double boxSum = 0.0;

    float sampleAt (int64_t index) const { return ring[static_cast<size_t> (index & ringMask)]; }
};

} // namespace vcab
