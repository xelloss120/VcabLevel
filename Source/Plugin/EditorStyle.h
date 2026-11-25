// 画面の色と大きさ（VcabDenoise・VcabShift と同じ値）
#pragma once

#include "LookAndFeel.h"

namespace editor
{

inline juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// 地の色
inline const juce::Colour kBackground { 0xff17202e };
inline const juce::Colour kPanel { 0xff1e2a3c };
inline const juce::Colour kText { 0xffe2e8f2 };
inline const juce::Colour kGraph { 0xff0f1724 }; // スペクトルとメーターの下地
inline const juce::Colour kGrid { 0x22ffffff };
inline const juce::Colour kDim { 0xff7a8494 };

// 線と印の色
inline const juce::Colour kInputColour { 0xff4fa3e0 };   // 入力: 青
inline const juce::Colour kOutputColour { 0xfff0a040 };  // 出力: 橙
inline const juce::Colour kGateColour { 0xffc8a0f0 };    // ゲート: 薄紫（VcabDenoise の声あり判定と同じ）
inline const juce::Colour kCompColour { 0xffe8c050 };    // コンプ: 黄（VcabTone の EQ の線と同じ）
inline const juce::Colour kLimiterColour { 0xffe87a8a }; // リミッター: 薄い赤（VcabDenoise の抑制量と同じ）
inline const juce::Colour kCurveColour { 0xff5cc8a0 };   // 入出力の特性: 緑（アイコンの帯も同じ色）

inline constexpr float kFont = VCLookAndFeel::kMinFontSize;
inline constexpr int kMargin = 8;

} // namespace editor
