// コンプ（4 章）。RMS（10ms）で大きさを見て、下げる量（dB）を返す。音そのものにはかけない（かけるのは Engine）。
#pragma once

#include "../Params.h"

namespace vcab
{

class Compressor
{
public:
    void prepare (double fs);
    void setSettings (const CompSettings& s);
    void reset();

    // 1 標本を見て、いま下げる量（dB、0 以上）を返す
    float next (float x);

    float levelDb() const; // いまの大きさ（RMS、dBFS）

private:
    double fs = 48000.0;
    CompSettings settings;
    float rmsCoef = 0.99f, attackCoef = 0.9f, releaseCoef = 0.99f;
    // 2 乗の平均がこれより小さい間は、効き始め（閾値 − ニー/2）より確かに下なので、dB に直さずに目標を 0 とする（負荷を減らす）
    double quietMeanSquare = 0.0;
    double meanSquare = 0.0;
    float reduction = 0.0f;

    void update();
};

} // namespace vcab
