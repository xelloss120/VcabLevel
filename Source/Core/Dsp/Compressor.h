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

    float levelDb() const { return level; } // いまの大きさ（RMS、dBFS）

private:
    double fs = 48000.0;
    CompSettings settings;
    float rmsCoef = 0.99f, attackCoef = 0.9f, releaseCoef = 0.99f;
    double meanSquare = 0.0;
    float level = -120.0f;
    float reduction = 0.0f;

    void update();
};

} // namespace vcab
