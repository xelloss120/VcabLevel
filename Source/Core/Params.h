// パラメータの唯一の定義。プラグインの設定、処理ツール、画面は、どれもこの表から作る。
// JUCE には依存しない。
#pragma once

#include <array>
#include <string>
#include <vector>

namespace vcab
{

// パラメータの番号（ホストのオートメーションの一覧もこの順番）。Params.cpp の表もこの順番に並べる。
enum class P : int
{
    // 全体
    bypass,

    // ゲート（3 章）
    gateOn, gateThreshold, gateHysteresis, gateAttack, gateHold, gateRelease,

    // コンプ（4 章）
    compOn, compThreshold, compRatio, compKnee, compAttack, compRelease,

    // ゲイン（5 章）
    gainOn, gain,

    // リバーブ（12 章）
    reverbOn, reverbLevel, reverbTime, reverbPredelay, reverbHighCut, reverbLowCut,

    // リミッター（6 章）
    limiterOn, limiterCeiling, limiterRelease,

    // 入力（音の処理の設定ではないもの）
    inputChannel,

    count
};

constexpr int kNumParams = static_cast<int> (P::count);

enum class ParamType { real, toggle, choice };

struct ParamInfo
{
    P id;
    const char* key;      // 保存とホストの識別に使う名前（変えない）
    const char* name;     // 画面に出す名前
    ParamType type;
    float minValue, maxValue, defaultValue;
    float step;           // 0 なら連続
    float skewCentre;     // 0 なら直線。そうでなければ、つまみの真ん中に来る値
    const char* unit;
    std::vector<const char*> choices;
    const char* help;     // ツールチップの説明
};

// 入力チャンネル
enum class InputChannel : int { automatic, ch1, ch2, average };

const std::vector<ParamInfo>& paramTable();
const ParamInfo& paramInfo (P id);
int findParamByKey (const std::string& key); // 見つからなければ -1
bool isUiSetting (P id); // 入力チャンネルなど（オートメーションの対象にせず、設定値初期化でも戻さない）

// ゲートの設定（3 章）
struct GateSettings
{
    bool on = true;
    float threshold = -42.0f;  // 開く線（dBFS）
    float hysteresis = 3.0f;   // 閉じる線を開く線よりこれだけ下げる（dB）
    float attackMs = 3.0f;
    float holdMs = 100.0f;
    float releaseMs = 300.0f;  // 閉じると必ず無音（減衰量のつまみは外した。3 章）
};

// コンプの設定（4 章）
struct CompSettings
{
    bool on = true;
    float threshold = -18.0f;  // dBFS（RMS）
    float ratio = 2.0f;
    float knee = 1.0f;         // dB
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
};

// ゲインの設定（5 章）
struct GainSettings
{
    bool on = true;
    float db = 6.0f;
};

// リバーブの設定（12 章）
struct ReverbSettings
{
    bool on = false;
    float levelDb = -18.0f;    // 響きの量（元の声は変えない）
    float timeS = 1.5f;        // 響きが 60dB 小さくなるまでの時間（秒）
    float predelayMs = 35.0f;
    float highCutHz = 6000.0f;
    float lowCutHz = 200.0f;
};

// リミッターの設定（6 章）
struct LimiterSettings
{
    bool on = true;
    float ceiling = -3.0f;     // 上限（dBFS。点と点の間の山も含めて）
    float releaseMs = 100.0f;
};

// 処理に渡す設定（パラメータの値を型の付いた形にしたもの）。
// ここの初期値は、fromValues で上書きするまでの仮の値（表の初期値と同じにしておく。Params.cpp の assert で確かめている）
struct Settings
{
    bool bypass = false;
    GateSettings gate;
    CompSettings comp;
    GainSettings gain;
    ReverbSettings reverb;
    LimiterSettings limiter;
    InputChannel inputChannel = InputChannel::automatic;

    static Settings fromValues (const std::array<float, kNumParams>& v);
};

std::array<float, kNumParams> defaultValues();

// ゲート・コンプ・ゲイン・リミッターを通した静的な特性（入る音の dBFS → 出る音の dBFS。8 章の「入出力の特性」）。
// オフの段は入れない。リバーブ（時間で変わる響き）は入れない。ゲートの開く線より下は -1000 を返す（閉じると無音）
float staticCurveDb (const Settings& s, float inDb);
// コンプの特性: 入る音（dB）に対して下げる量（dB、0 以上。4 章の式）
float compReductionDb (const CompSettings& c, float inDb);

} // namespace vcab
