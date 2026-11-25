// 画面のグラフ（8 章）。左から、時間の流れ（直近 5 秒）、入出力の特性、メーター（入力・コンプ・リミッター・出力）。
// 音の処理が 10ms ごとに書く値（vcab::MeterFrame）を、画面のタイマーで読んで描く。
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

class DisplayView : public juce::Component,
                    public juce::TooltipClient,
                    private juce::Timer
{
public:
    explicit DisplayView (VcabLevelProcessor&);
    ~DisplayView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String getTooltip() override; // マウスのある所（時間の流れ・入出力・メーター）の説明

    static constexpr int kHistoryFrames = 500; // 5 秒（10ms ごと）

private:
    static constexpr float kMinDb = -72.0f, kMaxDb = 0.0f; // 大きさの目盛り（dBFS）
    static constexpr float kMaxReductionDb = 24.0f;        // 削り量のメーターの目盛り

    VcabLevelProcessor& proc;
    vcab::Settings settings;

    // 時間の流れ（輪。historyPos が次に書く所）
    std::vector<vcab::MeterFrame> history;
    int historyPos = 0;
    vcab::MeterFrame latest;

    // メーターの表示（すぐ上がり、ゆっくり下がる）
    float shownIn = -120.0f, shownOut = -120.0f, shownComp = 0.0f, shownLimiter = 0.0f;

    juce::Rectangle<int> historyArea, transferArea, meterArea;

    void timerCallback() override;
    void paintHistory (juce::Graphics&, juce::Rectangle<float>);
    void paintTransfer (juce::Graphics&, juce::Rectangle<float>);
    void paintMeters (juce::Graphics&, juce::Rectangle<float>);
};
