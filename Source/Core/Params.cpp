#include "Params.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace vcab
{

namespace
{

// 時間や比のつまみは、範囲の両端の相乗平均を真ん中に置く（対数の目盛りに近い動き）
float geoCentre (float lo, float hi) { return std::sqrt (lo * hi); }

std::vector<ParamInfo> makeTable()
{
    using T = ParamType;
    std::vector<ParamInfo> t {
        { P::bypass, "bypass", "バイパス", T::toggle, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, "", {},
          "処理をせず、入力をそのまま出します（処理なしの音と比べるため）。遅延（1ms）はそのままです。中の処理は続けるので、戻してすぐに効きます。" },

        // ゲート
        { P::gateOn, "gate_on", "ゲート", T::toggle, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", {},
          "黙っている間の音（部屋の雑音、息、ファン）を消します。\n"
          "判定は音量を変える前の入力で行い、消すのは最後（リミッターの後）です。"
          "なので、ゲインや上限を動かしても閾値を直す必要はありません。" },
        { P::gateThreshold, "gate_threshold", "閾値", T::real, -80.0f, -20.0f, -48.0f, 0.5f, 0.0f, "dB", {},
          "入力の山がこれを超えたら開きます（開く線）。\n"
          "黙っているときの雑音より上、小さな声より下に置きます。グラフの紫の実線です。" },
        { P::gateHysteresis, "gate_hysteresis", "ヒステリシス", T::real, 0.0f, 12.0f, 3.0f, 0.5f, 0.0f, "dB", {},
          "閉じる線を、開く線よりこれだけ下げます（グラフの紫の点線）。\n"
          "語尾で音が線のまわりを上下しても、開け閉めを繰り返さない（パタつかない）ようにします。" },
        { P::gateAttack, "gate_attack", "アタック", T::real, 0.1f, 10.0f, 3.0f, 0.1f, geoCentre (0.1f, 10.0f), "ms", {},
          "開くときのフェードの長さ。短いと声の頭がはっきりし、長いとプツッという音が出にくくなります。\n"
          "判定は 1ms 先の音で行うので、声の頭は切れにくくなっています。" },
        { P::gateHold, "gate_hold", "ホールド", T::real, 0.0f, 500.0f, 100.0f, 1.0f, 0.0f, "ms", {},
          "閉じる線を下回ってから、閉じ始めるまで待つ時間。\n"
          "単語の間や「っ」のような短い切れ目で閉じないようにします。" },
        { P::gateRelease, "gate_release", "リリース", T::real, 10.0f, 2000.0f, 300.0f, 1.0f, geoCentre (10.0f, 2000.0f), "ms", {},
          "閉じるときのフェードの長さ。短いと語尾が切れたように聞こえ、長いと語尾の後の雑音が残ります。" },

        // コンプ
        { P::compOn, "comp_on", "コンプ", T::toggle, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", {},
          "大きな声を抑えて、声の大きさの差を縮めます。\n"
          "大きさは直近 10ms の平均（RMS）で見ます。母音のように続く大きさに反応し、子音の一瞬の山には反応しにくくなっています"
          "（一瞬の山はリミッターが止めます）。" },
        { P::compThreshold, "comp_threshold", "閾値", T::real, -40.0f, 0.0f, -18.0f, 0.5f, 0.0f, "dB", {},
          "声の平均（RMS）がこれを超えた分を抑えます。グラフの黄の点線です。\n"
          "普通の声ではほとんど効かず、大きな声で数 dB 効くくらいに置きます。" },
        { P::compRatio, "comp_ratio", "レシオ", T::real, 1.0f, 10.0f, 2.0f, 0.1f, geoCentre (1.0f, 10.0f), ":1", {},
          "閾値を超えた分を、どれだけ縮めるか。2:1 なら、超えた分が半分になります（4dB 超えたら 2dB 下げる）。" },
        { P::compKnee, "comp_knee", "ニー", T::real, 0.0f, 12.0f, 1.0f, 0.5f, 0.0f, "dB", {},
          "閾値の前後で、効き始めをなだらかにする幅（折れ線の角を丸める幅）。\n"
          "広いと効き始めが目立たず、狭いと閾値を超えた声にだけ、はっきり効きます。" },
        { P::compAttack, "comp_attack", "アタック", T::real, 0.1f, 50.0f, 6.0f, 0.1f, geoCentre (0.1f, 50.0f), "ms", {},
          "大きな声が来てから、抑え始める速さ。短いと声の頭から抑え、長いと声の頭の勢いを残します。" },
        { P::compRelease, "comp_release", "リリース", T::real, 10.0f, 1000.0f, 150.0f, 1.0f, geoCentre (10.0f, 1000.0f), "ms", {},
          "声が小さくなってから、抑えた分を戻す速さ。短いと音量が細かく揺れ、長いと大きな声の後の言葉まで小さくなります。" },

        // ゲイン
        { P::gainOn, "gain_on", "ゲイン", T::toggle, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", {},
          "音量を上げ下げします。コンプの後、リミッターの前にかかります（リミッターにどれだけ押し込むかを決めます）。" },
        { P::gain, "gain", "ゲイン", T::real, -12.0f, 12.0f, 6.0f, 0.1f, 0.0f, "dB", {},
          "上げ下げする量。上げすぎると、リミッターが常に効いて音が詰まって聞こえます。" },

        // リミッター
        { P::limiterOn, "limiter_on", "リミッター", T::toggle, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", {},
          "出る音が上限を超えないように、大きな山だけを瞬間的に下げます。\n"
          "1ms 先読みして、山の手前からなめらかに下げます。点と点の間にできる山（ISP）も見ています。\n"
          "オフにしても、遅延（1ms）は変わりません。" },
        { P::limiterCeiling, "limiter_ceiling", "上限", T::real, -12.0f, 0.0f, -3.0f, 0.1f, 0.0f, "dB", {},
          "出る音の上限。グラフの赤の線です。\n"
          "Discord などで圧縮したあとに山が少し越える分や、相手が音量を 100% より上げたときのために、少し余裕を取っておきます。" },
        { P::limiterRelease, "limiter_release", "リリース", T::real, 10.0f, 500.0f, 100.0f, 1.0f, geoCentre (10.0f, 500.0f), "ms", {},
          "下げた分を戻す速さ。短いと音量は稼げますが、細かく上下して濁ります。" },

        // 入力
        { P::inputChannel, "input_channel", "入力チャンネル", T::choice, 0.0f, 3.0f, 0.0f, 1.0f, 0.0f, "",
          { "自動", "1ch", "2ch", "平均" },
          "マイクの入力チャンネル。自動は、1ch と 2ch の大きいほうを使います（もう片方が 2 秒続けて 6dB 以上大きいと切り替え）。" },
    };

    // 表の並びと P の並びがそろっていることを確かめる（書き漏れ、並べ違いの防止）
    assert (static_cast<int> (t.size()) == kNumParams);
    for (int i = 0; i < kNumParams; ++i)
        assert (static_cast<int> (t[static_cast<size_t> (i)].id) == i);

    // Settings の仮の初期値が、表の初期値と同じことを確かめる（表だけ変えて、仮の値が古いまま残らないように）
    std::array<float, kNumParams> v {};
    for (const auto& p : t)
        v[static_cast<size_t> (p.id)] = p.defaultValue;
    const Settings a {};
    const Settings b = Settings::fromValues (v);
    assert (a.bypass == b.bypass && a.inputChannel == b.inputChannel
            && a.gate.on == b.gate.on && a.gate.threshold == b.gate.threshold && a.gate.hysteresis == b.gate.hysteresis
            && a.gate.attackMs == b.gate.attackMs && a.gate.holdMs == b.gate.holdMs && a.gate.releaseMs == b.gate.releaseMs
            && a.comp.on == b.comp.on && a.comp.threshold == b.comp.threshold && a.comp.ratio == b.comp.ratio
            && a.comp.knee == b.comp.knee && a.comp.attackMs == b.comp.attackMs && a.comp.releaseMs == b.comp.releaseMs
            && a.gain.on == b.gain.on && a.gain.db == b.gain.db
            && a.limiter.on == b.limiter.on && a.limiter.ceiling == b.limiter.ceiling && a.limiter.releaseMs == b.limiter.releaseMs);
    (void) a;
    (void) b;
    return t;
}

} // namespace

const std::vector<ParamInfo>& paramTable()
{
    static const std::vector<ParamInfo> table = makeTable();
    return table;
}

const ParamInfo& paramInfo (P id)
{
    return paramTable()[static_cast<size_t> (id)];
}

int findParamByKey (const std::string& key)
{
    for (const auto& p : paramTable())
        if (key == p.key)
            return static_cast<int> (p.id);
    return -1;
}

bool isUiSetting (P id)
{
    return id == P::inputChannel;
}

std::array<float, kNumParams> defaultValues()
{
    std::array<float, kNumParams> v {};
    for (const auto& p : paramTable())
        v[static_cast<size_t> (p.id)] = p.defaultValue;
    return v;
}

Settings Settings::fromValues (const std::array<float, kNumParams>& v)
{
    auto get = [&v] (P id) { return v[static_cast<size_t> (id)]; };
    auto on = [&] (P id) { return get (id) >= 0.5f; };

    Settings s;
    s.bypass = on (P::bypass);

    auto& g = s.gate;
    g.on = on (P::gateOn);
    g.threshold = get (P::gateThreshold);
    g.hysteresis = get (P::gateHysteresis);
    g.attackMs = get (P::gateAttack);
    g.holdMs = get (P::gateHold);
    g.releaseMs = get (P::gateRelease);

    auto& c = s.comp;
    c.on = on (P::compOn);
    c.threshold = get (P::compThreshold);
    c.ratio = std::max (1.0f, get (P::compRatio));
    c.knee = std::max (0.0f, get (P::compKnee));
    c.attackMs = get (P::compAttack);
    c.releaseMs = get (P::compRelease);

    s.gain.on = on (P::gainOn);
    s.gain.db = get (P::gain);

    auto& l = s.limiter;
    l.on = on (P::limiterOn);
    l.ceiling = get (P::limiterCeiling);
    l.releaseMs = get (P::limiterRelease);

    s.inputChannel = static_cast<InputChannel> (std::clamp (static_cast<int> (std::lround (get (P::inputChannel))), 0, 3));
    return s;
}

float compReductionDb (const CompSettings& c, float x)
{
    // 4 章の式。W はニー、R はレシオ
    const float t = c.threshold, w = c.knee, slope = 1.0f / c.ratio - 1.0f; // slope は 0 以下
    if (w > 0.0f && std::abs (x - t) <= 0.5f * w)
    {
        const float d = x - t + 0.5f * w;
        return -slope * d * d / (2.0f * w);
    }
    return x > t ? -slope * (x - t) : 0.0f;
}

float staticCurveDb (const Settings& s, float x)
{
    float y = x;
    if (s.comp.on)
        y -= compReductionDb (s.comp, x);
    if (s.gain.on)
        y += s.gain.db;
    if (s.limiter.on)
        y = std::min (y, s.limiter.ceiling);
    if (s.gate.on && x < s.gate.threshold)
        return -1000.0f; // 閉じると無音
    return y;
}

} // namespace vcab
