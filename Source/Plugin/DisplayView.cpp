#include "DisplayView.h"
#include "EditorStyle.h"

#include <cmath>

using namespace editor;

namespace
{
constexpr int kTimerHz = 30;
constexpr float kFallDbPerFrame = 0.5f; // メーターの表示が下がる速さ（1 フレームあたり。約 15dB/秒）
constexpr float kGateShadeDb = -6.0f;   // ゲートの倍率がこれより下の区切りを「閉じていた」として塗る
constexpr int kTransferWidth = 250, kMeterWidth = 170, kGap = 8;
const char* const kMeterNames[4] = { "入力", "コンプ", "リミ", "出力" };

constexpr float kLineWidth = 1.6f; // 横線（つまみの値）の太さ

void solidLine (juce::Graphics& g, float x0, float x1, float y)
{
    g.fillRect (juce::Rectangle<float> (x0, y - 0.5f * kLineWidth, x1 - x0, kLineWidth));
}

void dashedLine (juce::Graphics& g, float x0, float x1, float y)
{
    const float dash[] = { 5.0f, 4.0f };
    g.drawDashedLine (juce::Line<float> (x0, y, x1, y), dash, 2, kLineWidth);
}
} // namespace

DisplayView::DisplayView (VcabLevelProcessor& p) : proc (p)
{
    history.assign (kHistoryFrames, vcab::MeterFrame {});
    settings = proc.currentSettings();
    startTimerHz (kTimerHz);
}

DisplayView::~DisplayView()
{
    stopTimer();
}

void DisplayView::resized()
{
    auto r = getLocalBounds();
    meterArea = r.removeFromRight (kMeterWidth);
    r.removeFromRight (kGap);
    transferArea = r.removeFromRight (kTransferWidth);
    r.removeFromRight (kGap);
    historyArea = r;
}

void DisplayView::timerCallback()
{
    settings = proc.currentSettings();
    float inPeak = -120.0f, outPeak = -120.0f, compPeak = 0.0f, limiterPeak = 0.0f;
    vcab::MeterFrame f;
    bool any = false;
    while (proc.meters().pop (f))
    {
        history[static_cast<size_t> (historyPos)] = f;
        historyPos = (historyPos + 1) % kHistoryFrames;
        inPeak = std::max (inPeak, f.inDb);
        outPeak = std::max (outPeak, f.outDb);
        compPeak = std::max (compPeak, f.compDb);
        limiterPeak = std::max (limiterPeak, f.limiterDb);
        latest = f;
        any = true;
    }
    if (! any)
    {
        repaint(); // 音が流れていなくても、つまみの変化（横線、特性）は描き直す
        return;
    }
    shownIn = std::max (inPeak, shownIn - kFallDbPerFrame);
    shownOut = std::max (outPeak, shownOut - kFallDbPerFrame);
    shownComp = std::max (compPeak, std::max (0.0f, shownComp - kFallDbPerFrame));
    shownLimiter = std::max (limiterPeak, std::max (0.0f, shownLimiter - kFallDbPerFrame));
    repaint();
}

juce::String DisplayView::getTooltip()
{
    const auto m = getMouseXYRelative();
    if (historyArea.contains (m))
        return utf8 ("直近 5 秒の流れ（右が今）。\n"
                     "青の塗りは入力、橙の線は出力の大きさ（10ms ごとの山）。上から伸びる黄はコンプ、赤はリミッターが削った量（0dB の線からの深さ）。\n"
                     "紫の塗りはゲートが閉じていた所。横線は、紫がゲートの開く線（実線）と閉じる線（点線）、黄の点線がコンプの閾値、赤が上限です。");
    if (transferArea.contains (m))
        return utf8 ("入る音の大きさ（横）に対して、出る音の大きさ（縦）。4 段を通した特性です（オフの段は入りません）。\n"
                     "ゲートの開く線より下は一番下（無音）まで下がり、コンプの閾値から傾きが緩み、ゲインの分だけ上がり、上限で頭打ちになります。\n"
                     "白い点は、いまの入力（コンプが見ている平均の大きさ）です。");
    if (meterArea.contains (m))
        return utf8 ("入力と出力の山の大きさ（dBFS）と、コンプ・リミッターがいま削っている量（dB。上から下へ伸びる）。\n"
                     "入力の紫の印はゲートの開く線、出力の赤の印は上限です。");
    return {};
}

void DisplayView::paint (juce::Graphics& g)
{
    for (auto a : { historyArea, transferArea, meterArea })
    {
        g.setColour (kGraph);
        g.fillRect (a);
    }
    paintHistory (g, historyArea.toFloat());
    paintTransfer (g, transferArea.toFloat());
    paintMeters (g, meterArea.toFloat());
}

void DisplayView::paintHistory (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto legend = area.removeFromBottom (22.0f);
    const auto a = area.withTrimmedLeft (34.0f).withTrimmedRight (8.0f).withTrimmedTop (10.0f).withTrimmedBottom (4.0f);
    auto yFor = [&a] (float db) { return a.getY() + (kMaxDb - juce::jlimit (kMinDb, kMaxDb, db)) / (kMaxDb - kMinDb) * a.getHeight(); };
    g.setFont (juce::FontOptions (kFont - 4.0f));

    // 目盛り
    for (float db = kMaxDb; db >= kMinDb; db -= 12.0f)
    {
        const float y = yFor (db);
        g.setColour (kGrid);
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (kDim);
        g.drawText (juce::String (static_cast<int> (db)), juce::Rectangle<float> (area.getX(), y - 8.0f, 30.0f, 16.0f), juce::Justification::centredRight);
    }

    const float dx = a.getWidth() / static_cast<float> (kHistoryFrames - 1);
    auto frameAt = [this] (int i) -> const vcab::MeterFrame& // i = 0 が一番古い
    { return history[static_cast<size_t> ((historyPos + i) % kHistoryFrames)]; };

    // ゲートが閉じていた所
    if (settings.gate.on)
    {
        g.setColour (kGateColour.withAlpha (0.16f));
        for (int i = 0; i < kHistoryFrames; ++i)
            if (frameAt (i).gateDb < kGateShadeDb)
                g.fillRect (juce::Rectangle<float> (a.getX() + i * dx - 0.5f * dx, a.getY(), dx + 0.5f, a.getHeight()));
    }

    // 入力（塗り）と出力（線）、削り量（上から）
    juce::Path in, out, comp, limiter;
    in.startNewSubPath (a.getX(), a.getBottom());
    comp.startNewSubPath (a.getX(), a.getY());
    limiter.startNewSubPath (a.getX(), a.getY());
    for (int i = 0; i < kHistoryFrames; ++i)
    {
        const auto& f = frameAt (i);
        const float x = a.getX() + i * dx;
        in.lineTo (x, yFor (f.inDb));
        if (i == 0)
            out.startNewSubPath (x, yFor (f.outDb));
        else
            out.lineTo (x, yFor (f.outDb));
        comp.lineTo (x, yFor (-f.compDb));
        limiter.lineTo (x, yFor (-f.limiterDb));
    }
    in.lineTo (a.getRight(), a.getBottom());
    in.closeSubPath();
    comp.lineTo (a.getRight(), a.getY());
    comp.closeSubPath();
    limiter.lineTo (a.getRight(), a.getY());
    limiter.closeSubPath();
    g.setColour (kInputColour.withAlpha (0.35f));
    g.fillPath (in);
    g.setColour (kOutputColour);
    g.strokePath (out, juce::PathStrokeType (1.4f));
    if (settings.comp.on)
    {
        g.setColour (kCompColour.withAlpha (0.55f));
        g.fillPath (comp);
    }
    if (settings.limiter.on)
    {
        g.setColour (kLimiterColour.withAlpha (0.75f));
        g.fillPath (limiter);
    }

    // 横線
    if (settings.gate.on)
    {
        g.setColour (kGateColour);
        solidLine (g, a.getX(), a.getRight(), yFor (settings.gate.threshold));
        if (settings.gate.hysteresis > 0.0f)
            dashedLine (g, a.getX(), a.getRight(), yFor (settings.gate.threshold - settings.gate.hysteresis));
    }
    if (settings.comp.on)
    {
        g.setColour (kCompColour);
        dashedLine (g, a.getX(), a.getRight(), yFor (settings.comp.threshold));
    }
    if (settings.limiter.on)
    {
        g.setColour (kLimiterColour);
        solidLine (g, a.getX(), a.getRight(), yFor (settings.limiter.ceiling));
    }

    // 凡例
    struct Item { const char* text; juce::Colour colour; };
    const Item items[] = { { "入力", kInputColour }, { "出力", kOutputColour }, { "コンプ", kCompColour },
                           { "リミッター", kLimiterColour }, { "ゲート", kGateColour } };
    auto l = legend.withTrimmedLeft (34.0f);
    const juce::Font font { juce::FontOptions (kFont - 4.0f) };
    for (const auto& it : items)
    {
        const auto text = utf8 (it.text);
        g.setColour (it.colour);
        g.fillRect (juce::Rectangle<float> (10.0f, 4.0f).withCentre ({ l.getX() + 5.0f, l.getCentreY() }));
        l.removeFromLeft (14.0f);
        const float w = juce::GlyphArrangement::getStringWidth (font, text) + 2.0f;
        g.setColour (kDim);
        g.drawText (text, l.removeFromLeft (w), juce::Justification::centredLeft);
        l.removeFromLeft (10.0f);
    }
}

void DisplayView::paintTransfer (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setFont (juce::FontOptions (kFont - 4.0f));
    g.setColour (kDim);
    g.drawText (utf8 ("入出力"), area.withHeight (20.0f).withTrimmedLeft (34.0f), juce::Justification::centredLeft);
    auto inner = area.withTrimmedLeft (34.0f).withTrimmedRight (8.0f).withTrimmedTop (22.0f).withTrimmedBottom (22.0f);
    const float side = std::min (inner.getWidth(), inner.getHeight());
    const auto a = juce::Rectangle<float> (inner.getX(), inner.getY(), side, side);
    auto xFor = [&a] (float db) { return a.getX() + (juce::jlimit (kMinDb, kMaxDb, db) - kMinDb) / (kMaxDb - kMinDb) * a.getWidth(); };
    auto yFor = [&a] (float db) { return a.getY() + (kMaxDb - juce::jlimit (kMinDb, kMaxDb, db)) / (kMaxDb - kMinDb) * a.getHeight(); };

    for (float db = kMaxDb; db >= kMinDb; db -= 12.0f)
    {
        g.setColour (kGrid);
        g.drawHorizontalLine (juce::roundToInt (yFor (db)), a.getX(), a.getRight());
        g.drawVerticalLine (juce::roundToInt (xFor (db)), a.getY(), a.getBottom());
        g.setColour (kDim);
        g.drawText (juce::String (static_cast<int> (db)), juce::Rectangle<float> (area.getX(), yFor (db) - 8.0f, 30.0f, 16.0f),
                    juce::Justification::centredRight);
        if (static_cast<int> (db) % 24 == 0)
            g.drawText (juce::String (static_cast<int> (db)), juce::Rectangle<float> (xFor (db) - 15.0f, a.getBottom() + 2.0f, 30.0f, 16.0f),
                        juce::Justification::centred);
    }
    // 素通しの線（目安）
    g.setColour (kGrid.withAlpha (0.25f));
    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);

    // 特性（0.25dB ごと。ゲートの段差は縦の線になる）
    juce::Path curve;
    for (int i = 0; i <= 288; ++i)
    {
        const float x = kMinDb + (kMaxDb - kMinDb) * static_cast<float> (i) / 288.0f;
        const float y = vcab::staticCurveDb (settings, x);
        if (i == 0)
            curve.startNewSubPath (xFor (x), yFor (y));
        else
            curve.lineTo (xFor (x), yFor (y));
    }
    g.saveState();
    g.reduceClipRegion (a.toNearestInt());
    g.setColour (kCurveColour);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
    // いまの入力
    // （特性の線が一番下より下にある所、ゲートが閉じる所では出さない）
    const float dotY = vcab::staticCurveDb (settings, latest.rmsDb);
    if (latest.rmsDb > kMinDb && dotY > kMinDb)
    {
        const float y = dotY;
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ xFor (latest.rmsDb), yFor (y) }));
    }
    g.restoreState();
}

void DisplayView::paintMeters (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setFont (juce::FontOptions (kFont - 4.0f));
    auto r = area.reduced (6.0f, 0.0f);
    const float colW = r.getWidth() / 4.0f;
    for (int c = 0; c < 4; ++c)
    {
        auto col = r.removeFromLeft (colW);
        auto valueBox = col.removeFromTop (22.0f);
        auto labelBox = col.removeFromBottom (22.0f);
        const auto bar = col.reduced (colW * 0.25f, 4.0f);
        g.setColour (kBackground);
        g.fillRect (bar);

        const bool level = c == 0 || c == 3;
        float value = 0.0f, fraction = 0.0f;
        juce::Colour colour;
        bool enabled = true;
        if (level)
        {
            value = c == 0 ? shownIn : shownOut;
            fraction = (juce::jlimit (kMinDb, kMaxDb, value) - kMinDb) / (kMaxDb - kMinDb);
            colour = c == 0 ? kInputColour : kOutputColour;
            g.setColour (colour);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * fraction));
            // 印: 入力はゲートの開く線、出力は上限
            const bool showMark = c == 0 ? settings.gate.on : settings.limiter.on;
            if (showMark)
            {
                const float db = c == 0 ? settings.gate.threshold : settings.limiter.ceiling;
                const float y = bar.getBottom() - bar.getHeight() * (juce::jlimit (kMinDb, kMaxDb, db) - kMinDb) / (kMaxDb - kMinDb);
                g.setColour (c == 0 ? kGateColour : kLimiterColour);
                g.fillRect (juce::Rectangle<float> (bar.getX() - 4.0f, y - 1.0f, bar.getWidth() + 8.0f, 2.0f));
            }
        }
        else
        {
            value = c == 1 ? shownComp : shownLimiter;
            enabled = c == 1 ? settings.comp.on : settings.limiter.on;
            fraction = juce::jlimit (0.0f, 1.0f, value / kMaxReductionDb);
            colour = c == 1 ? kCompColour : kLimiterColour;
            g.setColour (colour);
            g.fillRect (bar.withHeight (bar.getHeight() * fraction)); // 上から下へ
        }
        g.setColour (enabled ? kText : kDim);
        const auto text = level ? (value <= -119.0f ? juce::String ("-inf") : juce::String (value, 1))
                                : (value < 0.05f ? juce::String ("0.0") : "-" + juce::String (value, 1));
        g.drawText (text, valueBox, juce::Justification::centred);
        g.setColour (enabled ? kDim : kDim.withAlpha (0.5f));
        g.drawText (utf8 (kMeterNames[c]), labelBox, juce::Justification::centred);
    }
}
