// ゲート（3 章）。入口の音で判定し、かける倍率（0〜1）を返す。音そのものにはかけない（かけるのは Engine。リバーブとリミッターの前）。
#pragma once

#include "../Params.h"

namespace vcab
{

class Gate
{
public:
    void prepare (double fs);
    void setSettings (const GateSettings& s);
    void reset();

    // 入口の 1 標本を見て、いまの倍率を返す
    float next (float x)
    {
        // 判定: 1ms ほどならしたピーク（山は即座に取り、1ms で下げる）
        const float a = x < 0.0f ? -x : x;
        env = a > env ? a : env * envCoef;
        if (env < kEnvFloor)
            env = 0.0f; // 無音が続いても非正規化数に入らないように（呼び出し側の設定に頼らない）。閉じる線（-92dB 以上）よりずっと下

        if (env >= openLin)
        {
            open = true;
            holdLeft = holdSamples;
        }
        else if (open)
        {
            if (env >= closeLin)
                holdLeft = holdSamples; // 閉じる線より上にいる間は、ホールドを数え始めない
            else if (holdLeft > 0)
                --holdLeft;
            else
                open = false;
        }

        if (open)
        {
            // 開く: 振幅で直線
            g = g + attackStep < 1.0f ? g + attackStep : 1.0f;
        }
        else if (g > 0.0f)
        {
            // 閉じる: dB で直線に -80dB まで下げてから 0（無音）にする
            g *= releaseMul;
            if (g < kClosedLin)
                g = 0.0f;
        }
        return g;
    }

    bool isOpen() const { return open; }

private:
    static constexpr float kClosedLin = 1.0e-4f; // -80dB
    static constexpr float kEnvFloor = 1.0e-20f;  // -400dB

    double fs = 48000.0;
    GateSettings settings;
    float envCoef = 0.98f;
    float openLin = 0.0f, closeLin = 0.0f;
    int holdSamples = 0;
    float attackStep = 1.0f, releaseMul = 0.99f;

    float env = 0.0f;
    float g = 0.0f;
    bool open = false;
    int holdLeft = 0;

    void update();
};

} // namespace vcab
